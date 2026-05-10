#ifndef XVAD_FBANK_H
#define XVAD_FBANK_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// 窗函数类型
typedef enum {
    FBANK_WINDOW_HANN,     // 汉宁窗（TEN-VAD用）
    FBANK_WINDOW_HAMMING,  // 汉明窗
    FBANK_WINDOW_POVERY    // Povey窗（Kaldi标准，FireRedVAD用）
} fbank_window_type_t;

// FBank 配置结构体
typedef struct {
    uint32_t sample_rate;      // 采样率（Hz）
    size_t frame_length;       // 帧长（样本数）
    size_t frame_shift;        // 帧移（样本数）
    size_t fft_size;           // FFT大小（必须是2的幂）
    size_t num_mel_bins;       // Mel滤波器数量
    float preemph_coeff;       // 预加重系数（通常0.97）
    fbank_window_type_t window_type; // 窗函数类型
    int remove_dc_offset;      // 是否去除直流偏移
    int use_log_fbank;         // 是否使用对数FBank
    int use_energy;            // 是否在最后一维添加能量特征
} fbank_config_t;

// FBank 提取器句柄
typedef struct fbank_extractor fbank_extractor_t;

// 预定义配置：TEN-VAD 标准参数
extern const fbank_config_t FBANK_CONFIG_TEN_VAD;

// 预定义配置：FireRedVAD 标准参数
extern const fbank_config_t FBANK_CONFIG_FIRERED_VAD;

// 创建FBank提取器
fbank_extractor_t* fbank_extractor_create(const fbank_config_t* config);

// 处理一帧音频，输出FBank特征
// 输入：frame_length个16bit PCM样本
// 输出：num_mel_bins（+1如果use_energy）个float特征
void fbank_extractor_process(
    fbank_extractor_t* extractor,
    const int16_t* frame,
    float* fbank_out
);

// 重置FBank提取器状态
void fbank_extractor_reset(fbank_extractor_t* extractor);

// 销毁FBank提取器
void fbank_extractor_destroy(fbank_extractor_t* extractor);

/////////////////////////////////////////////////////////////

#ifdef __cplusplus
}
#endif

#endif // XVAD_FBANK_H
