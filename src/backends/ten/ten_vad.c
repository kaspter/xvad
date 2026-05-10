#include "../../../include/xvad.h"
#include <stdlib.h>
#include <string.h>

#if 0
#include "gnn.h"

#define TEN_VAD_SAMPLE_RATE 16000
#define TEN_VAD_FRAME_SIZE 256
#define TEN_VAD_FEATURE_DIM 41
#define TEN_VAD_CONTEXT_FRAMES 3

// TEN-VAD 后端数据
typedef struct {
    MNNInterpreter* interpreter;
    MNNSession* session;
    MNNTensor* input_tensor;
    MNNTensor* output_tensor;
    
    float feature_cache[TEN_VAD_CONTEXT_FRAMES * TEN_VAD_FEATURE_DIM];
    int cache_ptr;
    
    fbank_extractor_t* fbank;
    pitch_extractor_t* pitch;
} ten_vad_data_t;

// TEN-VAD 能力
const xvad_capabilities_t xvad_ten_vad_caps = {
    .sample_rate = TEN_VAD_SAMPLE_RATE,
    .frame_size = TEN_VAD_FRAME_SIZE,
    .frame_duration_ms = 16,
    .supports_probability = 1
};

static xvad_error_t ten_vad_create(void** backend, const void* config) {
    ten_vad_data_t* data = (ten_vad_data_t*)malloc(sizeof(ten_vad_data_t));
    if (!data) return XVAD_ERROR_MEMORY_ALLOC_FAILED;
    
    memset(data, 0, sizeof(ten_vad_data_t));
    
    // 创建MNN解释器
    data->interpreter = MNNInterpreter_createFromFile("model/ten_vad_int8.mnn");
    if (!data->interpreter) {
        free(data);
        return XVAD_ERROR_MODEL_LOAD_FAILED;
    }
    
    // 创建会话
    MNNSessionConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.numThread = 1;
    cfg.type = MNN_FORWARD_CPU;
    data->session = MNNInterpreter_createSession(data->interpreter, &cfg);
    if (!data->session) {
        MNNInterpreter_destroy(data->interpreter);
        free(data);
        return XVAD_ERROR_BACKEND_ERROR;
    }
    
    // 获取输入输出张量
    data->input_tensor = MNNInterpreter_getSessionInput(data->interpreter, data->session, "input");
    data->output_tensor = MNNInterpreter_getSessionOutput(data->interpreter, data->session, "output");
    
    // 固定输入形状 [1, 3, 41]
    int dims[] = {1, TEN_VAD_CONTEXT_FRAMES, TEN_VAD_FEATURE_DIM};
    MNNInterpreter_resizeTensor(data->interpreter, data->input_tensor, dims, 3);
    MNNInterpreter_resizeSession(data->interpreter, data->session);

	// 预定义配置：TEN-VAD FBank 特征提取器参数 （严格匹配官方 Kaldi 参数）
	const fbank_config_t FBANK_CONFIG_TEN_VAD = {
		.sample_rate = TEN_VAD_SAMPLE_RATE,
		.frame_length = 256,
		.frame_shift = 128,
		.fft_size = 512,
		.num_mel_bins = 40,
		.preemph_coeff = 0.97f,
		.window_type = FBANK_WINDOW_HANN,
		.remove_dc_offset = 1,
		.use_log_fbank = 1,
		.use_energy = 1  // TEN-VAD需要40维FBank+1维能量
	};

    // 初始化特征提取器
    data->fbank = fbank_extractor_create(&fbank_cfg);
    if (!data->fbank) {
        MNNInterpreter_releaseSession(data->interpreter, data->session);
        MNNInterpreter_destroy(data->interpreter);
        free(data);
        return XVAD_ERROR_BACKEND_ERROR;
    }

	// 预定义配置：TEN-VAD 标准参数
	const pitch_config_t PITCH_CONFIG_TEN_VAD = {
		.sample_rate = 16000,
		.frame_length = 256,
		.frame_shift = 128,
		.min_pitch_hz = 80.0f,
		.max_pitch_hz = 400.0f,
		.voicing_threshold = 0.3f
	};

    data->pitch = pitch_extractor_create(&PITCH_CONFIG_TEN_VAD);
    if (!data->pitch) {
        fbank_extractor_destroy(data->fbank);
        MNNInterpreter_releaseSession(data->interpreter, data->session);
        MNNInterpreter_destroy(data->interpreter);
        free(data);
        return XVAD_ERROR_BACKEND_ERROR;
    }

    *backend = data;
    return XVAD_OK;
}

static xvad_error_t ten_vad_process(void* backend, const int16_t* frame, float* prob) {
    ten_vad_data_t* data = (ten_vad_data_t*)backend;
    
    // 提取40维FBank特征
    float fbank[40];
    fbank_extractor_process(data->fbank, frame, fbank);
    
    // 提取1维Pitch特征
    float pitch_hz, voicing_prob;
    pitch_extractor_process(data->pitch, frame, &pitch_hz, &voicing_prob);

    // 拼接成41维特征
    float feature[TEN_VAD_FEATURE_DIM];
    memcpy(feature, fbank, 40 * sizeof(float));
    feature[40] = pitch_hz / 400.0f;  // 归一化到0~1范围（与官方一致）;

    // 更新特征缓存
    memcpy(
        data->feature_cache + data->cache_ptr * TEN_VAD_FEATURE_DIM,
        feature,
        TEN_VAD_FEATURE_DIM * sizeof(float)
    );
    data->cache_ptr = (data->cache_ptr + 1) % TEN_VAD_CONTEXT_FRAMES;
    
    // 填充MNN输入
    float* input_ptr = MNNTensor_host<float>(data->input_tensor);
    for (int i = 0; i < TEN_VAD_CONTEXT_FRAMES; i++) {
        int idx = (data->cache_ptr + i) % TEN_VAD_CONTEXT_FRAMES;
        memcpy(
            input_ptr + i * TEN_VAD_FEATURE_DIM,
            data->feature_cache + idx * TEN_VAD_FEATURE_DIM,
            TEN_VAD_FEATURE_DIM * sizeof(float)
        );
    }
    
    // 执行推理
    MNNInterpreter_runSession(data->interpreter, data->session);
    
    // 获取输出概率
    *prob = MNNTensor_host<float>(data->output_tensor)[0];
    *prob = (*prob > 1.0f) ? 1.0f : (*prob < 0.0f) ? 0.0f : *prob;
    
    return XVAD_OK;
}

static xvad_error_t ten_vad_reset(void* backend) {
    ten_vad_data_t* data = (ten_vad_data_t*)backend;
    memset(data->feature_cache, 0, sizeof(data->feature_cache));
    data->cache_ptr = 0;
    return XVAD_OK;
}

static void ten_vad_destroy(void* backend) {
    ten_vad_data_t* data = (ten_vad_data_t*)backend;
    if (data->fbank) fbank_extractor_destroy(data->fbank);
    if (data->pitch) pitch_extractor_destroy(data->pitch);
    if (data->session) MNNInterpreter_releaseSession(data->interpreter, data->session);
    if (data->interpreter) MNNInterpreter_destroy(data->interpreter);
    free(data);
}

const xvad_backend_interface_t xvad_ten_vad_interface = {
    .create = ten_vad_create,
    .process = ten_vad_process,
    .reset = ten_vad_reset,
    .destroy = ten_vad_destroy
};
#endif
