#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "xvad.h"

#if 0
#include "gnn.h"

#define SILERO_SAMPLE_RATE    16000
#define SILERO_FRAME_SIZE     512
#define SILERO_STATE_DIM      64
#define SILERO_NUM_LAYERS     2
#define SILERO_INPUT_NAME     "input"
#define SILERO_H_NAME         "h"
#define SILERO_C_NAME         "c"
#define SILERO_OUTPUT_NAME    "output"
#define SILERO_OUTPUT_H_NAME  "output_h"
#define SILERO_OUTPUT_C_NAME  "output_c"

// Silero VAD 后端数据结构
typedef struct {
    MNNInterpreter* interpreter;
    MNNSession* session;
    
    // 输入张量
    MNNTensor* input_tensor;
    MNNTensor* h_input_tensor;
    MNNTensor* c_input_tensor;
    
    // 输出张量
    MNNTensor* output_tensor;
    MNNTensor* h_output_tensor;
    MNNTensor* c_output_tensor;
    
    // 状态缓存（持久化保存）
    float h_state[SILERO_NUM_LAYERS * 1 * SILERO_STATE_DIM];
    float c_state[SILERO_NUM_LAYERS * 1 * SILERO_STATE_DIM];
} silero_vad_data_t;

// Silero VAD 能力声明
const xvad_capabilities_t xvad_silero_caps = {
    .sample_rate = SILERO_SAMPLE_RATE,
    .frame_size = SILERO_FRAME_SIZE,
    .frame_duration_ms = 32,
    .supports_probability = 1
};

// 内部函数：重置状态
static void silero_reset_state(silero_vad_data_t* data) {
    memset(data->h_state, 0, sizeof(data->h_state));
    memset(data->c_state, 0, sizeof(data->c_state));
}

// 创建 Silero VAD 实例
static xvad_error_t silero_vad_create(void** backend, const void* config) {
    (void)config; // 暂不支持自定义配置
    
    silero_vad_data_t* data = (silero_vad_data_t*)malloc(sizeof(silero_vad_data_t));
    if (!data) return XVAD_ERROR_MEMORY_ALLOC_FAILED;
    
    memset(data, 0, sizeof(silero_vad_data_t));
    
    // 1. 加载 MNN 模型
    data->interpreter = MNNInterpreter_createFromFile("model/silero_vad_int8.mnn");
    if (!data->interpreter) {
        free(data);
        return XVAD_ERROR_MODEL_LOAD_FAILED;
    }
    
    // 2. 创建 MNN 会话（单线程最优）
    MNNSessionConfig sess_cfg;
    memset(&sess_cfg, 0, sizeof(sess_cfg));
    sess_cfg.numThread = 1;
    sess_cfg.type = MNN_FORWARD_CPU;
    sess_cfg.saveTensors = 1; // 必须开启，否则无法获取输出状态
    
    data->session = MNNInterpreter_createSession(data->interpreter, &sess_cfg);
    if (!data->session) {
        MNNInterpreter_destroy(data->interpreter);
        free(data);
        return XVAD_ERROR_BACKEND_ERROR;
    }
    
    // 3. 获取所有输入输出张量
    data->input_tensor = MNNInterpreter_getSessionInput(data->interpreter, data->session, SILERO_INPUT_NAME);
    data->h_input_tensor = MNNInterpreter_getSessionInput(data->interpreter, data->session, SILERO_H_NAME);
    data->c_input_tensor = MNNInterpreter_getSessionInput(data->interpreter, data->session, SILERO_C_NAME);
    
    data->output_tensor = MNNInterpreter_getSessionOutput(data->interpreter, data->session, SILERO_OUTPUT_NAME);
    data->h_output_tensor = MNNInterpreter_getSessionOutput(data->interpreter, data->session, SILERO_OUTPUT_H_NAME);
    data->c_output_tensor = MNNInterpreter_getSessionOutput(data->interpreter, data->session, SILERO_OUTPUT_C_NAME);
    
    // 4. 固定所有张量形状（避免动态形状开销）
    int input_dims[] = {1, 1, SILERO_FRAME_SIZE};
    int state_dims[] = {SILERO_NUM_LAYERS, 1, SILERO_STATE_DIM};
    
    MNNInterpreter_resizeTensor(data->interpreter, data->input_tensor, input_dims, 3);
    MNNInterpreter_resizeTensor(data->interpreter, data->h_input_tensor, state_dims, 3);
    MNNInterpreter_resizeTensor(data->interpreter, data->c_input_tensor, state_dims, 3);
    MNNInterpreter_resizeSession(data->interpreter, data->session);
    
    // 5. 初始化状态
    silero_reset_state(data);
    
    *backend = data;
    return XVAD_OK;
}

// 处理单帧音频
static xvad_error_t silero_vad_process(void* backend, const int16_t* frame, float* probability) {
    silero_vad_data_t* data = (silero_vad_data_t*)backend;
    
    // 1. 输入转换：int16 PCM → float32 [-1.0, 1.0]
    float* input_ptr = MNNTensor_host<float>(data->input_tensor);
    for (int i = 0; i < SILERO_FRAME_SIZE; i++) {
        input_ptr[i] = frame[i] / 32768.0f;
    }
    
    // 2. 填充 LSTM 状态
    float* h_input_ptr = MNNTensor_host<float>(data->h_input_tensor);
    float* c_input_ptr = MNNTensor_host<float>(data->c_input_tensor);
    memcpy(h_input_ptr, data->h_state, sizeof(data->h_state));
    memcpy(c_input_ptr, data->c_state, sizeof(data->c_state));
    
    // 3. 执行 MNN 推理
    MNNInterpreter_runSession(data->interpreter, data->session);
    
    // 4. 获取输出概率（裁剪到 [0,1] 范围）
    *probability = MNNTensor_host<float>(data->output_tensor)[0];
    *probability = fmaxf(0.0f, fminf(1.0f, *probability));
    
    // 5. 更新 LSTM 状态（保存到缓存供下一帧使用）
    const float* h_output_ptr = MNNTensor_host<float>(data->h_output_tensor);
    const float* c_output_ptr = MNNTensor_host<float>(data->c_output_tensor);
    memcpy(data->h_state, h_output_ptr, sizeof(data->h_state));
    memcpy(data->c_state, c_output_ptr, sizeof(data->c_state));
    
    return XVAD_OK;
}

// 重置 VAD 状态
static xvad_error_t silero_vad_reset(void* backend) {
    silero_vad_data_t* data = (silero_vad_data_t*)backend;
    silero_reset_state(data);
    return XVAD_OK;
}

// 销毁 Silero VAD 实例
static void silero_vad_destroy(void* backend) {
    silero_vad_data_t* data = (silero_vad_data_t*)backend;
    if (data->session) {
        MNNInterpreter_releaseSession(data->interpreter, data->session);
    }
    if (data->interpreter) {
        MNNInterpreter_destroy(data->interpreter);
    }
    free(data);
}

// 注册 Silero VAD 后端接口
const xvad_backend_interface_t xvad_silero_interface = {
    .create = silero_vad_create,
    .process = silero_vad_process,
    .reset = silero_vad_reset,
    .destroy = silero_vad_destroy
};

#endif
