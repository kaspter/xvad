
#include <stdlib.h>
#include <string.h>
#include <math.h>


#include "pitch.h"

#define EPSILON 1e-10f

// Pitch提取器内部结构体
struct pitch_extractor {
    pitch_config_t config;
    
    // 预计算参数
    float* window;             // 汉宁窗系数 [frame_length]
    size_t min_lag;            // 最小基音周期（样本数）
    size_t max_lag;            // 最大基音周期（样本数）
    
    // 临时缓冲区
    float* frame_float;        // 浮点帧缓冲区 [frame_length]
    float* autocorr;           // 自相关缓冲区 [max_lag+1]
    
    // 历史状态（用于平滑）
    float prev_pitch;          // 上一帧基音频率
    float prev_voicing;        // 上一帧浊音概率
};

// ==================== 工具函数 ====================
// 计算自相关函数（优化版，仅计算需要的lag范围）
static void compute_autocorr(
    const float* x,
    float* autocorr,
    size_t n,
    size_t min_lag,
    size_t max_lag
) {
    // 计算零延迟自相关（能量）
    float energy = 0.0f;
    for (size_t i = 0; i < n; i++) {
        energy += x[i] * x[i];
    }
    autocorr[0] = energy;
    
    // 计算需要的延迟范围
    for (size_t lag = min_lag; lag <= max_lag; lag++) {
        float sum = 0.0f;
        for (size_t i = 0; i < n - lag; i++) {
            sum += x[i] * x[i + lag];
        }
        autocorr[lag] = sum;
    }
}

// 寻找自相关峰值
static size_t find_peak(
    const float* autocorr,
    size_t min_lag,
    size_t max_lag,
    float* peak_value
) {
    size_t peak_lag = min_lag;
    float max_val = autocorr[min_lag];
    
    for (size_t lag = min_lag + 1; lag <= max_lag; lag++) {
        if (autocorr[lag] > max_val) {
            max_val = autocorr[lag];
            peak_lag = lag;
        }
    }
    
    *peak_value = max_val;
    return peak_lag;
}

// 抛物线插值，提高基音频率精度
static float parabolic_interpolation(
    const float* autocorr,
    size_t peak_lag
) {
    if (peak_lag == 0) return 0.0f;
    
    float y0 = autocorr[peak_lag - 1];
    float y1 = autocorr[peak_lag];
    float y2 = autocorr[peak_lag + 1];
    
    // 抛物线顶点位置
    float delta = (y2 - y0) / (2.0f * (2.0f * y1 - y0 - y2));
    
    return peak_lag + delta;
}

// ==================== 公共接口实现 ====================


pitch_extractor_t* pitch_extractor_create(const pitch_config_t* config) {
    if (!config) return NULL;
    
    pitch_extractor_t* extractor = (pitch_extractor_t*)malloc(sizeof(pitch_extractor_t));
    if (!extractor) return NULL;
    
    memset(extractor, 0, sizeof(pitch_extractor_t));
    extractor->config = *config;
    
    // 计算基音周期范围（样本数）
    extractor->min_lag = (size_t)floor(config->sample_rate / config->max_pitch_hz);
    extractor->max_lag = (size_t)ceil(config->sample_rate / config->min_pitch_hz);
    
    // 分配内存
    extractor->window = (float*)malloc(config->frame_length * sizeof(float));
    extractor->frame_float = (float*)malloc(config->frame_length * sizeof(float));
    extractor->autocorr = (float*)malloc((extractor->max_lag + 1) * sizeof(float));
    
    // 检查内存分配
    if (!extractor->window || !extractor->frame_float || !extractor->autocorr) {
        pitch_extractor_destroy(extractor);
        return NULL;
    }
    
    // 预计算汉宁窗
    for (size_t i = 0; i < config->frame_length; i++) {
        extractor->window[i] = 0.5f - 0.5f * cosf(2.0f * (float)M_PI * i / (config->frame_length - 1));
    }
    
    // 初始化状态
    pitch_extractor_reset(extractor);
    
    return extractor;
}

void pitch_extractor_process(
    pitch_extractor_t* extractor,
    const int16_t* frame,
    float* pitch_hz,
    float* voicing_prob
) {
    const pitch_config_t* config = &extractor->config;
    
    // 1. int16 → float 转换
    for (size_t i = 0; i < config->frame_length; i++) {
        extractor->frame_float[i] = frame[i] / 32768.0f;
    }
    
    // 2. 预加重（与FBank一致）
    for (size_t i = config->frame_length - 1; i > 0; i--) {
        extractor->frame_float[i] -= 0.97f * extractor->frame_float[i-1];
    }
    extractor->frame_float[0] -= 0.97f * 0.0f;
    
    // 3. 加窗
    for (size_t i = 0; i < config->frame_length; i++) {
        extractor->frame_float[i] *= extractor->window[i];
    }
    
    // 4. 计算自相关函数
    compute_autocorr(
        extractor->frame_float,
        extractor->autocorr,
        config->frame_length,
        extractor->min_lag,
        extractor->max_lag
    );
    
    // 5. 归一化自相关
    float energy = extractor->autocorr[0];
    if (energy < EPSILON) {
        // 静音帧
        *pitch_hz = 0.0f;
        *voicing_prob = 0.0f;
        extractor->prev_pitch = 0.0f;
        extractor->prev_voicing = 0.0f;
        return;
    }
    
    for (size_t lag = extractor->min_lag; lag <= extractor->max_lag; lag++) {
        extractor->autocorr[lag] /= energy;
    }
    
    // 6. 寻找自相关峰值
    float peak_val;
    size_t peak_lag = find_peak(
        extractor->autocorr,
        extractor->min_lag,
        extractor->max_lag,
        &peak_val
    );
    
    // 7. 抛物线插值，提高频率精度
    float precise_lag = parabolic_interpolation(extractor->autocorr, peak_lag);
    
    // 8. 计算基音频率
    float pitch = config->sample_rate / precise_lag;
    
    // 9. 计算浊音概率
    float voicing = peak_val;
    voicing = fmaxf(0.0f, fminf(1.0f, voicing));
    
    // 10. 浊音判决
    if (voicing < config->voicing_threshold) {
        // 清音帧
        *pitch_hz = 0.0f;
        *voicing_prob = voicing;
    } else {
        // 浊音帧
        *pitch_hz = pitch;
        *voicing_prob = voicing;
    }
    
    // 11. 时间平滑（可选，TEN-VAD官方未使用）
    // *pitch_hz = 0.7f * (*pitch_hz) + 0.3f * extractor->prev_pitch;
    // *voicing_prob = 0.7f * (*voicing_prob) + 0.3f * extractor->prev_voicing;
    
    // 更新历史状态
    extractor->prev_pitch = *pitch_hz;
    extractor->prev_voicing = *voicing_prob;
}

void pitch_extractor_reset(pitch_extractor_t* extractor) {
    if (!extractor) return;
    extractor->prev_pitch = 0.0f;
    extractor->prev_voicing = 0.0f;
}

void pitch_extractor_destroy(pitch_extractor_t* extractor) {
    if (!extractor) return;
    
    if (extractor->window) free(extractor->window);
    if (extractor->frame_float) free(extractor->frame_float);
    if (extractor->autocorr) free(extractor->autocorr);
    
    free(extractor);
}
