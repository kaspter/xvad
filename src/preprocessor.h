#ifndef XVAD_PREPROCESSOR_H
#define XVAD_PREPROCESSOR_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

///////////////////////////////////////////////////////////////////////////////////////
// 预处理句柄
typedef struct xvad_preprocessor xvad_preprocessor_t;

// 预处理配置
typedef struct {
    int high_pass_hz;        // 高通滤波截止频率，0表示禁用
    int denoise;             // 是否启用RNNoise降噪
    float normalize_dbfs;    // 归一化到目标dBFS，0表示禁用
    float max_gain;          // 归一化最大增益（防止静音时放大噪声）
} xvad_preprocessor_config_t;

// 创建预处理器
xvad_error_t xvad_preprocessor_create(xvad_preprocessor_t** preprocessor, const xvad_preprocessor_config_t* config, uint32_t sample_rate);

// 预处理音频
xvad_error_t xvad_preprocessor_process(xvad_preprocessor_t* preprocessor, const int16_t* in, int16_t* out, size_t size);

// 销毁预处理器
void xvad_preprocessor_destroy(xvad_preprocessor_t* preprocessor);

// 预定义配置
extern const xvad_preprocessor_config_t XVAD_PREPROCESSOR_RAW_MIC;
extern const xvad_preprocessor_config_t XVAD_PREPROCESSOR_TELEPHONY;


#ifdef __cplusplus
}
#endif

#endif // XVAD_UTILS_H
