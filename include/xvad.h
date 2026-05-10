#ifndef XVAD_H
#define XVAD_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// 错误码定义
typedef enum {
    XVAD_OK = 0,
    XVAD_ERROR_INVALID_PARAM,
    XVAD_ERROR_INVALID_SAMPLE_RATE,
    XVAD_ERROR_INVALID_FRAME_SIZE,
    XVAD_ERROR_MODEL_LOAD_FAILED,
    XVAD_ERROR_BACKEND_ERROR,
    XVAD_ERROR_MEMORY_ALLOC_FAILED
} xvad_error_t;

// 后端类型
typedef enum {
    XVAD_BACKEND_WEBRTC,
    XVAD_BACKEND_TEN_VAD,
    XVAD_BACKEND_SILERO,
    XVAD_BACKEND_FIRERED
} xvad_backend_t;

// WebRTC VAD 模式
typedef enum {
    XVAD_WEBRTC_MODE_NORMAL,
    XVAD_WEBRTC_MODE_LOW_BITRATE,
    XVAD_WEBRTC_MODE_AGGRESSIVE,
    XVAD_WEBRTC_MODE_VERY_AGGRESSIVE
} xvad_webrtc_mode_t;

// 预处理配置
typedef struct {
    int high_pass_hz;        // 高通滤波截止频率，0表示禁用
    int denoise;             // 是否启用RNNoise降噪
    float normalize_dbfs;    // 归一化到目标dBFS，0表示禁用
    float max_gain;          // 归一化最大增益（防止静音时放大噪声）
} xvad_preprocessor_config_t;

// 后端能力
typedef struct {
    uint32_t sample_rate;        // 支持的采样率
    size_t frame_size;           // 要求的帧大小（样本数）
    uint32_t frame_duration_ms;  // 帧时长（毫秒）
    int supports_probability;    // 是否支持连续概率输出
} xvad_capabilities_t;

// VAD 句柄
typedef struct xvad_handle xvad_handle_t;

// 帧适配器句柄
typedef struct xvad_frame_adapter xvad_frame_adapter_t;

// 预处理句柄
typedef struct xvad_preprocessor xvad_preprocessor_t;

// 创建VAD实例
xvad_error_t xvad_create(xvad_handle_t** handle, xvad_backend_t backend, const void* config);

// 获取后端能力
xvad_error_t xvad_get_capabilities(xvad_backend_t backend, xvad_capabilities_t* caps);

// 获取VAD实例的后端类型
xvad_error_t xvad_get_backend_type(xvad_handle_t* handle, xvad_backend_t* backend_type);

// 处理一帧音频
// 输入：frame_size个16bit PCM样本
// 输出：语音概率（0.0~1.0）
xvad_error_t xvad_process_frame(xvad_handle_t* handle, const int16_t* frame, float* probability);

// 重置VAD内部状态
xvad_error_t xvad_reset(xvad_handle_t* handle);

// 销毁VAD实例
void xvad_destroy(xvad_handle_t* handle);

// 创建帧适配器
xvad_error_t xvad_frame_adapter_create(xvad_frame_adapter_t** adapter, xvad_handle_t* vad);

// 处理任意大小的音频块
// 输出：results数组，每个元素对应一帧的概率
// 返回：处理的帧数
size_t xvad_frame_adapter_process(xvad_frame_adapter_t* adapter,
    const int16_t* chunk,
    size_t chunk_size,
    float* results,
    size_t max_results
);

// 重置帧适配器
xvad_error_t xvad_frame_adapter_reset(xvad_frame_adapter_t* adapter);

// 销毁帧适配器
void xvad_frame_adapter_destroy(xvad_frame_adapter_t* adapter);

// 创建预处理器
xvad_error_t xvad_preprocessor_create(
    xvad_preprocessor_t** preprocessor,
    const xvad_preprocessor_config_t* config,
    uint32_t sample_rate
);

// 预处理音频
xvad_error_t xvad_preprocessor_process(
    xvad_preprocessor_t* preprocessor,
    const int16_t* in,
    int16_t* out,
    size_t size
);

// 销毁预处理器
void xvad_preprocessor_destroy(xvad_preprocessor_t* preprocessor);

// 预定义配置
extern const xvad_preprocessor_config_t XVAD_PREPROCESSOR_RAW_MIC;
extern const xvad_preprocessor_config_t XVAD_PREPROCESSOR_TELEPHONY;

#ifdef __cplusplus
}
#endif

#endif // XVAD_H
