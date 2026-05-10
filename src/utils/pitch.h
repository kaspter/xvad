#ifndef XVAD_PITCH_H
#define XVAD_PITCH_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

//////////////////////////////////////////////////////////////////////////
// Pitch 配置结构体
typedef struct {
    uint32_t sample_rate;      // 采样率（Hz）
    size_t frame_length;       // 帧长（样本数）
    size_t frame_shift;        // 帧移（样本数）
    float min_pitch_hz;        // 最小基音频率（Hz）
    float max_pitch_hz;        // 最大基音频率（Hz）
    float voicing_threshold;   // 浊音判决阈值
} pitch_config_t;

// Pitch 提取器句柄
typedef struct pitch_extractor pitch_extractor_t;

// 创建Pitch提取器
pitch_extractor_t* pitch_extractor_create(const pitch_config_t* config);

// 处理一帧音频，输出基音频率（Hz）和浊音概率
// 输出：pitch_hz - 基音频率（静音/清音时为0）
//       voicing_prob - 浊音概率（0.0~1.0）
void pitch_extractor_process(pitch_extractor_t* extractor, const int16_t* frame, float* pitch_hz, float* voicing_prob);

// 重置Pitch提取器状态
void pitch_extractor_reset(pitch_extractor_t* extractor);

// 销毁Pitch提取器
void pitch_extractor_destroy(pitch_extractor_t* extractor);

/////////////////////////////////////////////////////////////

#ifdef __cplusplus
}
#endif

#endif // XVAD_PITCH_H
