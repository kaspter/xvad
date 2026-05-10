


#if 0
#include "MNN/Interpreter.h"
#include "MNN/Tensor.h"
#include "../../utils/fbank.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "xvad.h"
#include "src/xvad_private.h"

#define FIRERED_SAMPLE_RATE    16000
#define FIRERED_FRAME_SIZE     400    // 25ms 窗长
#define FIRERED_HOP_SIZE       160    // 10ms 帧移
#define FIRERED_FFT_SIZE       512
#define FIRERED_FBANK_DIM      80
#define FIRERED_INPUT_NAME     "input"
#define FIRERED_OUTPUT_NAME    "output"

// FireRedVAD 后端数据结构
typedef struct {
    MNNInterpreter* interpreter;
    MNNSession* session;
    
    // 输入输出张量
    MNNTensor* input_tensor;
    MNNTensor* output_tensor;
    
    // 音频滑动窗口缓存（存储历史样本）
    int16_t audio_cache[FIRERED_FRAME_SIZE];
    size_t cache_ptr;
    
    // FBank 特征提取器
    fbank_extractor_t* fbank;
} firered_vad_data_t;

// FireRedVAD 能力声明
const xvad_capabilities_t xvad_firered_caps = {
    .sample_rate = FIRERED_SAMPLE_RATE,
    .frame_size = FIRERED_HOP_SIZE,  // 对外暴露帧移，用户只需输入160样本/帧
    .frame_duration_ms = 10,
    .supports_probability = 1
};

// 内部函数：重置状态
static void firered_reset_state(firered_vad_data_t* data) {
    memset(data->audio_cache, 0, sizeof(data->audio_cache));
    data->cache_ptr = 0;
    fbank_extractor_reset(data->fbank);
}

// 创建 FireRedVAD 实例
static xvad_error_t firered_vad_create(void** backend, const void* config) {
    (void)config; // 暂不支持自定义配置
    
    firered_vad_data_t* data = (firered_vad_data_t*)malloc(sizeof(firered_vad_data_t));
    if (!data) return XVAD_ERROR_MEMORY_ALLOC_FAILED;
    
    memset(data, 0, sizeof(firered_vad_data_t));
    
    // 1. 加载 MNN 模型
    data->interpreter = MNNInterpreter_createFromFile("model/firered_vad_int8.mnn");
    if (!data->interpreter) {
        free(data);
        return XVAD_ERROR_MODEL_LOAD_FAILED;
    }
    
    // 2. 创建 MNN 会话（单线程最优）
    MNNSessionConfig sess_cfg;
    memset(&sess_cfg, 0, sizeof(sess_cfg));
    sess_cfg.numThread = 1;
    sess_cfg.type = MNN_FORWARD_CPU;
    
    data->session = MNNInterpreter_createSession(data->interpreter, &sess_cfg);
    if (!data->session) {
        MNNInterpreter_destroy(data->interpreter);
        free(data);
        return XVAD_ERROR_BACKEND_ERROR;
    }
    
    // 3. 获取输入输出张量
    data->input_tensor = MNNInterpreter_getSessionInput(data->interpreter, data->session, FIRERED_INPUT_NAME);
    data->output_tensor = MNNInterpreter_getSessionOutput(data->interpreter, data->session, FIRERED_OUTPUT_NAME);
    
    // 4. 固定输入形状
    int input_dims[] = {1, 1, FIRERED_FBANK_DIM};
    MNNInterpreter_resizeTensor(data->interpreter, data->input_tensor, input_dims, 3);
    MNNInterpreter_resizeSession(data->interpreter, data->session);
    
    // 5. 初始化 FBank 特征提取器（严格匹配官方 Kaldi 参数）
    fbank_config_t fbank_cfg = {
        .sample_rate = FIRERED_SAMPLE_RATE,
        .frame_length = FIRERED_FRAME_SIZE,
        .frame_shift = FIRERED_HOP_SIZE,
        .fft_size = FIRERED_FFT_SIZE,
        .num_mel_bins = FIRERED_FBANK_DIM,
        .preemph_coeff = 0.97f,
        .window_type = FBANK_WINDOW_POVEY,
        .remove_dc_offset = 1,
        .use_log_fbank = 1,
        .use_energy = 0
    };
    
    data->fbank = fbank_extractor_create(&fbank_cfg);
    if (!data->fbank) {
        MNNInterpreter_releaseSession(data->interpreter, data->session);
        MNNInterpreter_destroy(data->interpreter);
        free(data);
        return XVAD_ERROR_BACKEND_ERROR;
    }
    
    // 6. 初始化状态
    firered_reset_state(data);
    
    *backend = data;
    return XVAD_OK;
}

// 处理单帧音频（用户输入160样本，内部滑动窗口拼接成400样本）
static xvad_error_t firered_vad_process(void* backend, const int16_t* frame, float* probability) {
    firered_vad_data_t* data = (firered_vad_data_t*)backend;
    
    // 1. 滑动窗口：移出旧数据，移入新数据
    memmove(
        data->audio_cache,
        data->audio_cache + FIRERED_HOP_SIZE,
        (FIRERED_FRAME_SIZE - FIRERED_HOP_SIZE) * sizeof(int16_t)
    );
    memcpy(
        data->audio_cache + (FIRERED_FRAME_SIZE - FIRERED_HOP_SIZE),
        frame,
        FIRERED_HOP_SIZE * sizeof(int16_t)
    );
    
    // 2. 提取 80 维 FBank 特征
    float fbank[FIRERED_FBANK_DIM];
    fbank_extractor_process(data->fbank, data->audio_cache, fbank);
    
    // 3. 填充 MNN 输入
    float* input_ptr = MNNTensor_host<float>(data->input_tensor);
    memcpy(input_ptr, fbank, FIRERED_FBANK_DIM * sizeof(float));
    
    // 4. 执行 MNN 推理
    MNNInterpreter_runSession(data->interpreter, data->session);
    
    // 5. 获取输出概率（Sigmoid 激活，裁剪到 [0,1]）
    *probability = MNNTensor_host<float>(data->output_tensor)[0];
    *probability = 1.0f / (1.0f + expf(-*probability)); // 官方模型输出logits，需手动Sigmoid
    *probability = fmaxf(0.0f, fminf(1.0f, *probability));
    
    return XVAD_OK;
}

// 重置 VAD 状态
static xvad_error_t firered_vad_reset(void* backend) {
    firered_vad_data_t* data = (firered_vad_data_t*)backend;
    firered_reset_state(data);
    return XVAD_OK;
}

// 销毁 FireRedVAD 实例
static void firered_vad_destroy(void* backend) {
    firered_vad_data_t* data = (firered_vad_data_t*)backend;
    if (data->fbank) fbank_extractor_destroy(data->fbank);
    if (data->session) {
        MNNInterpreter_releaseSession(data->interpreter, data->session);
    }
    if (data->interpreter) {
        MNNInterpreter_destroy(data->interpreter);
    }
    free(data);
}

// 注册 FireRedVAD 后端接口
const xvad_backend_interface_t xvad_firered_interface = {
    .create = firered_vad_create,
    .process = firered_vad_process,
    .reset = firered_vad_reset,
    .destroy = firered_vad_destroy
};
#endif
