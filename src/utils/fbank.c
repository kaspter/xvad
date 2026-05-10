
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "fbank.h"

#define EPSILON 1e-10f
#define MEL_LOW_FREQ 0.0f
#define MEL_HIGH_FREQ 8000.0f

// FBank提取器内部结构体
struct fbank_extractor {
    fbank_config_t config;
    
    // 预计算参数
    float* window;             // 窗函数系数 [frame_length]
    float* mel_filterbank;     // Mel滤波器组 [num_mel_bins × (fft_size/2+1)]
    int* mel_filter_start;     // 每个滤波器的起始频率点 [num_mel_bins]
    int* mel_filter_end;       // 每个滤波器的结束频率点 [num_mel_bins]
    
    // 临时缓冲区（运行时复用）
    float* frame_float;        // 浮点帧缓冲区 [frame_length]
    float* fft_in;             // FFT输入缓冲区 [fft_size]
    float* fft_out;            // FFT输出缓冲区 [fft_size]
    float* power_spectrum;     // 功率谱缓冲区 [fft_size/2+1]
};

// ==================== 工具函数 ====================
// 频率转Mel刻度（HTK标准，与Kaldi一致）
static float freq_to_mel(float freq) {
    return 2595.0f * log10f(1.0f + freq / 700.0f);
}

// Mel刻度转频率
static float mel_to_freq(float mel) {
    return 700.0f * (powf(10.0f, mel / 2595.0f) - 1.0f);
}

// 生成窗函数
static void generate_window(
    float* window,
    size_t length,
    fbank_window_type_t type
) {
    switch (type) {
        case FBANK_WINDOW_HANN:
            for (size_t i = 0; i < length; i++) {
                window[i] = 0.5f - 0.5f * cosf(2.0f * (float)M_PI * i / (length - 1));
            }
            break;
            
        case FBANK_WINDOW_HAMMING:
            for (size_t i = 0; i < length; i++) {
                window[i] = 0.54f - 0.46f * cosf(2.0f * (float)M_PI * i / (length - 1));
            }
            break;
            
        case FBANK_WINDOW_POVERY:
            // Kaldi Povey窗：(0.5 - 0.5*cos(2πn/(N-1)))^0.85
            for (size_t i = 0; i < length; i++) {
                float hann = 0.5f - 0.5f * cosf(2.0f * (float)M_PI * i / (length - 1));
                window[i] = powf(hann, 0.85f);
            }
            break;
    }
}

// 生成Mel滤波器组（与Kaldi完全一致）
static void generate_mel_filterbank(
    float* filterbank,
    int* filter_start,
    int* filter_end,
    size_t num_bins,
    size_t fft_size,
    uint32_t sample_rate,
    float low_freq,
    float high_freq
) {
    size_t num_fft_bins = fft_size / 2 + 1;
    float nyquist = sample_rate / 2.0f;
    
    // 计算Mel刻度范围
    float mel_low = freq_to_mel(low_freq);
    float mel_high = freq_to_mel(high_freq);
    float mel_step = (mel_high - mel_low) / (num_bins + 1);
    
    // 生成Mel刻度点
    float* mel_points = (float*)malloc((num_bins + 2) * sizeof(float));
    float* freq_points = (float*)malloc((num_bins + 2) * sizeof(float));
    int* bin_points = (int*)malloc((num_bins + 2) * sizeof(int));
    
    for (size_t i = 0; i < num_bins + 2; i++) {
        mel_points[i] = mel_low + i * mel_step;
        freq_points[i] = mel_to_freq(mel_points[i]);
        bin_points[i] = (int)floor((fft_size + 1) * freq_points[i] / sample_rate);
    }
    
    // 生成每个滤波器
    for (size_t i = 0; i < num_bins; i++) {
        int left = bin_points[i];
        int center = bin_points[i+1];
        int right = bin_points[i+2];
        
        filter_start[i] = left;
        filter_end[i] = right;
        
        // 左半部分：上升沿
        for (int j = left; j < center; j++) {
            filterbank[i * num_fft_bins + j] = (float)(j - left) / (center - left);
        }
        
        // 右半部分：下降沿
        for (int j = center; j < right; j++) {
            filterbank[i * num_fft_bins + j] = (float)(right - j) / (right - center);
        }
        
        // 归一化滤波器面积（与Kaldi一致）
        float sum = 0.0f;
        for (int j = left; j < right; j++) {
            sum += filterbank[i * num_fft_bins + j];
        }
        if (sum > 0.0f) {
            for (int j = left; j < right; j++) {
                filterbank[i * num_fft_bins + j] /= sum;
            }
        }
    }
    
    free(mel_points);
    free(freq_points);
    free(bin_points);
}

// 简单FFT实现（可替换为NEON加速版本）
// 此处为简化实现，工程中请替换为kissfft或neon-fft
// 基2快速傅里叶变换（FFT）
static void fft(float* in, float* out, size_t size) {
    // 位反转置换
    size_t j = 0;
    for (size_t i = 1; i < size - 1; i++) {
        size_t bit = size >> 1;
        for (; j >= bit; bit >>= 1) {
            j -= bit;
        }
        j += bit;
        
        if (i < j) {
            float temp_real = in[i*2];
            float temp_imag = in[i*2 + 1];
            in[i*2] = in[j*2];
            in[i*2 + 1] = in[j*2 + 1];
            in[j*2] = temp_real;
            in[j*2 + 1] = temp_imag;
        }
    }
    
    // 蝶形运算
    for (size_t len = 2; len <= size; len <<= 1) {
        float ang = -2.0f * (float)M_PI / len;
        float wlen_real = cosf(ang);
        float wlen_imag = sinf(ang);
        
        for (size_t i = 0; i < size; i += len) {
            float w_real = 1.0f;
            float w_imag = 0.0f;
            
            for (size_t j = 0; j < len / 2; j++) {
                size_t u = i + j;
                size_t v = i + j + len / 2;
                
                float u_real = in[u*2];
                float u_imag = in[u*2 + 1];
                float v_real = in[v*2] * w_real - in[v*2 + 1] * w_imag;
                float v_imag = in[v*2] * w_imag + in[v*2 + 1] * w_real;
                
                in[u*2] = u_real + v_real;
                in[u*2 + 1] = u_imag + v_imag;
                in[v*2] = u_real - v_real;
                in[v*2 + 1] = u_imag - v_imag;
                
                float next_w_real = w_real * wlen_real - w_imag * wlen_imag;
                float next_w_imag = w_real * wlen_imag + w_imag * wlen_real;
                w_real = next_w_real;
                w_imag = next_w_imag;
            }
        }
    }
    
    memcpy(out, in, size * 2 * sizeof(float));
}


// ==================== 公共接口实现 ====================
// 预定义配置：TEN-VAD 标准参数
const fbank_config_t FBANK_CONFIG_TEN_VAD = {
    .sample_rate = 16000,
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

// 预定义配置：FireRedVAD 标准参数
const fbank_config_t FBANK_CONFIG_FIRERED_VAD = {
    .sample_rate = 16000,
    .frame_length = 400,
    .frame_shift = 160,
    .fft_size = 512,
    .num_mel_bins = 80,
    .preemph_coeff = 0.97f,
    .window_type = FBANK_WINDOW_POVERY,
    .remove_dc_offset = 1,
    .use_log_fbank = 1,
    .use_energy = 0  // FireRedVAD不需要能量特征
};

fbank_extractor_t* fbank_extractor_create(const fbank_config_t* config) {
    if (!config) return NULL;
    
    fbank_extractor_t* extractor = (fbank_extractor_t*)malloc(sizeof(fbank_extractor_t));
    if (!extractor) return NULL;
    
    memset(extractor, 0, sizeof(fbank_extractor_t));
    extractor->config = *config;
    
    size_t num_fft_bins = config->fft_size / 2 + 1;
    
    // 分配内存
    extractor->window = (float*)malloc(config->frame_length * sizeof(float));
    extractor->mel_filterbank = (float*)calloc(config->num_mel_bins * num_fft_bins, sizeof(float));
    extractor->mel_filter_start = (int*)malloc(config->num_mel_bins * sizeof(int));
    extractor->mel_filter_end = (int*)malloc(config->num_mel_bins * sizeof(int));
    
    extractor->frame_float = (float*)malloc(config->frame_length * sizeof(float));
    extractor->fft_in = (float*)malloc(config->fft_size * sizeof(float));
    extractor->fft_out = (float*)malloc(config->fft_size * sizeof(float));
    extractor->power_spectrum = (float*)malloc(num_fft_bins * sizeof(float));
    
    // 检查内存分配
    if (!extractor->window || !extractor->mel_filterbank || 
        !extractor->mel_filter_start || !extractor->mel_filter_end ||
        !extractor->frame_float || !extractor->fft_in || 
        !extractor->fft_out || !extractor->power_spectrum) {
        fbank_extractor_destroy(extractor);
        return NULL;
    }
    
    // 预计算窗函数
    generate_window(extractor->window, config->frame_length, config->window_type);
    
    // 预计算Mel滤波器组
    generate_mel_filterbank(
        extractor->mel_filterbank,
        extractor->mel_filter_start,
        extractor->mel_filter_end,
        config->num_mel_bins,
        config->fft_size,
        config->sample_rate,
        MEL_LOW_FREQ,
        MEL_HIGH_FREQ
    );
    
    return extractor;
}

void fbank_extractor_process(
    fbank_extractor_t* extractor,
    const int16_t* frame,
    float* fbank_out
) {
    const fbank_config_t* config = &extractor->config;
    size_t num_fft_bins = config->fft_size / 2 + 1;
    float energy = 0.0f;
    
    // 1. int16 → float 转换
    for (size_t i = 0; i < config->frame_length; i++) {
        extractor->frame_float[i] = frame[i] / 32768.0f;
    }
    
    // 2. 去除直流偏移
    if (config->remove_dc_offset) {
        float dc = 0.0f;
        for (size_t i = 0; i < config->frame_length; i++) {
            dc += extractor->frame_float[i];
        }
        dc /= config->frame_length;
        
        for (size_t i = 0; i < config->frame_length; i++) {
            extractor->frame_float[i] -= dc;
        }
    }
    
    // 3. 预加重
    if (config->preemph_coeff > 0.0f) {
        float prev = 0.0f;
        for (size_t i = config->frame_length - 1; i > 0; i--) {
            extractor->frame_float[i] -= config->preemph_coeff * extractor->frame_float[i-1];
        }
        extractor->frame_float[0] -= config->preemph_coeff * prev;
    }
    
    // 4. 加窗
    for (size_t i = 0; i < config->frame_length; i++) {
        extractor->frame_float[i] *= extractor->window[i];
    }
    
    // 5. 计算能量（加窗后）
    if (config->use_energy) {
        energy = 0.0f;
        for (size_t i = 0; i < config->frame_length; i++) {
            energy += extractor->frame_float[i] * extractor->frame_float[i];
        }
        energy = logf(energy + EPSILON);
    }
    
    // 6. FFT
    memset(extractor->fft_in, 0, config->fft_size * sizeof(float));
    memcpy(extractor->fft_in, extractor->frame_float, config->frame_length * sizeof(float));
    fft(extractor->fft_in, extractor->fft_out, config->fft_size);
    
    // 7. 计算功率谱
    for (size_t i = 0; i < num_fft_bins; i++) {
        float real = extractor->fft_out[i*2];
        float imag = extractor->fft_out[i*2 + 1];
        extractor->power_spectrum[i] = real*real + imag*imag;
    }
    
    // 8. 应用Mel滤波器组
    for (size_t i = 0; i < config->num_mel_bins; i++) {
        float sum = 0.0f;
        int start = extractor->mel_filter_start[i];
        int end = extractor->mel_filter_end[i];
        
        for (int j = start; j < end; j++) {
            sum += extractor->power_spectrum[j] * extractor->mel_filterbank[i * num_fft_bins + j];
        }
        
        // 对数转换
        if (config->use_log_fbank) {
            fbank_out[i] = logf(sum + EPSILON);
        } else {
            fbank_out[i] = sum;
        }
    }
    
    // 9. 添加能量特征（如果需要）
    if (config->use_energy) {
        fbank_out[config->num_mel_bins] = energy;
    }
}

void fbank_extractor_reset(fbank_extractor_t* extractor) {
    // FBank提取器无状态，无需重置
    (void)extractor;
}

void fbank_extractor_destroy(fbank_extractor_t* extractor) {
    if (!extractor) return;
    
    if (extractor->window) free(extractor->window);
    if (extractor->mel_filterbank) free(extractor->mel_filterbank);
    if (extractor->mel_filter_start) free(extractor->mel_filter_start);
    if (extractor->mel_filter_end) free(extractor->mel_filter_end);
    if (extractor->frame_float) free(extractor->frame_float);
    if (extractor->fft_in) free(extractor->fft_in);
    if (extractor->fft_out) free(extractor->fft_out);
    if (extractor->power_spectrum) free(extractor->power_spectrum);
    
    free(extractor);
}
