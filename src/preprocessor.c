#include "xvad.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifdef PREPROCESSOR_ENABLE_RNNOISE
#include "third_party/rnnoise/rnnoise.h"
#endif

#define RNNOISE_FRAME_SIZE 480 // RNNoise固定帧长：30ms@16kHz
#define MAX_NORMALIZE_GAIN 10.0f // 最大增益限制（20dB）

// 一阶IIR高通滤波器状态
typedef struct {
    float a1; // 反馈系数
    float b0, b1; // 前馈系数
    float x_prev; // 上一帧输入
    float y_prev; // 上一帧输出
} high_pass_filter_t;

// 自适应幅值归一化状态
typedef struct {
    float target_amp; // 目标幅值（对应目标dBFS）
    float max_gain; // 最大增益
    float current_gain; // 当前增益
    float attack_coeff; // 攻击系数（增益快速下降）
    float release_coeff; // 释放系数（增益缓慢上升）
    float peak_hold; // 峰值保持
} normalize_t;

// 预处理句柄结构体
struct xvad_preprocessor {
    xvad_preprocessor_config_t config;
    uint32_t sample_rate;

    // 子模块状态
    high_pass_filter_t* hp_filter;
#ifdef PREPROCESSOR_ENABLE_RNNOISE
    DenoiseState* rnnoise_state;
#endif
    normalize_t* normalizer;

    // RNNoise帧对齐缓冲区
    int16_t rnnoise_buf[RNNOISE_FRAME_SIZE];
    size_t rnnoise_buf_ptr;

    // 输出缓冲区（处理任意大小输入）
    int16_t* out_buf;
    size_t out_buf_size;
};

// ==================== 一阶IIR高通滤波器实现 ====================
static high_pass_filter_t* high_pass_filter_create(int cutoff_hz, uint32_t sample_rate)
{
    if (cutoff_hz <= 0 || sample_rate <= 0)
        return NULL;

    high_pass_filter_t* filter = (high_pass_filter_t*)malloc(sizeof(high_pass_filter_t));
    if (!filter)
        return NULL;

    memset(filter, 0, sizeof(high_pass_filter_t));

    // 计算滤波器系数（一阶巴特沃斯高通）
    float wc = 2.0f * (float)M_PI * cutoff_hz / sample_rate;
    float alpha = sinf(wc) / (1.0f + cosf(wc));

    filter->b0 = (1.0f + cosf(wc)) / 2.0f;
    filter->b1 = -(1.0f + cosf(wc)) / 2.0f;
    filter->a1 = -alpha;

    return filter;
}

static void high_pass_filter_process(high_pass_filter_t* filter, const int16_t* in, int16_t* out, size_t size)
{
    if (!filter || !in || !out || size == 0)
        return;

    for (size_t i = 0; i < size; i++) {
        float x = in[i] / 32768.0f;
        float y = filter->b0 * x + filter->b1 * filter->x_prev - filter->a1 * filter->y_prev;

        // 限制输出范围，防止溢出
        y = fmaxf(-1.0f, fminf(1.0f, y));

        out[i] = (int16_t)(y * 32767.0f);

        filter->x_prev = x;
        filter->y_prev = y;
    }
}

static void high_pass_filter_reset(high_pass_filter_t* filter)
{
    if (!filter)
        return;
    filter->x_prev = 0.0f;
    filter->y_prev = 0.0f;
}

static void high_pass_filter_destroy(high_pass_filter_t* filter)
{
    free(filter);
}

// ==================== 自适应幅值归一化实现 ====================
static normalize_t* normalize_create(float target_dbfs, float max_gain)
{
    if (target_dbfs >= 0.0f || max_gain <= 0.0f)
        return NULL;

    normalize_t* norm = (normalize_t*)malloc(sizeof(normalize_t));
    if (!norm)
        return NULL;

    memset(norm, 0, sizeof(normalize_t));

    // 转换dBFS到线性幅值
    norm->target_amp = powf(10.0f, target_dbfs / 20.0f);
    norm->max_gain = max_gain;
    norm->current_gain = 1.0f;

    // 攻击和释放时间常数（攻击10ms，释放100ms@16kHz）
    norm->attack_coeff = expf(-1.0f / (16000.0f * 0.01f));
    norm->release_coeff = expf(-1.0f / (16000.0f * 0.1f));

    norm->peak_hold = 0.0f;

    return norm;
}

static void normalize_process(normalize_t* norm, const int16_t* in, int16_t* out, size_t size)
{
    if (!norm || !in || !out || size == 0)
        return;

    for (size_t i = 0; i < size; i++) {
        float x = fabsf(in[i] / 32768.0f);

        // 更新峰值保持
        if (x > norm->peak_hold) {
            norm->peak_hold = x;
        } else {
            norm->peak_hold *= norm->release_coeff;
        }

        // 计算目标增益
        float target_gain = 1.0f;
        if (norm->peak_hold > 0.0001f) { // 避免除以零
            target_gain = norm->target_amp / norm->peak_hold;
        }

        // 限制最大增益
        target_gain = fminf(target_gain, norm->max_gain);

        // 平滑增益变化
        if (target_gain < norm->current_gain) {
            // 攻击：快速下降
            norm->current_gain = norm->attack_coeff * norm->current_gain + (1.0f - norm->attack_coeff) * target_gain;
        } else {
            // 释放：缓慢上升
            norm->current_gain = norm->release_coeff * norm->current_gain + (1.0f - norm->release_coeff) * target_gain;
        }

        // 应用增益
        float y = in[i] * norm->current_gain;

        // 限制输出范围，防止削波
        y = fmaxf(-32768.0f, fminf(32767.0f, y));

        out[i] = (int16_t)y;
    }
}

static void normalize_reset(normalize_t* norm)
{
    if (!norm)
        return;
    norm->current_gain = 1.0f;
    norm->peak_hold = 0.0f;
}

static void normalize_destroy(normalize_t* norm)
{
    free(norm);
}

// ==================== 预处理核心实现 ====================
xvad_error_t xvad_preprocessor_create(
    xvad_preprocessor_t** preprocessor,
    const xvad_preprocessor_config_t* config,
    uint32_t sample_rate)
{
    if (!preprocessor || !config || sample_rate != 16000) {
        return XVAD_ERROR_INVALID_PARAM;
    }

    *preprocessor = (xvad_preprocessor_t*)malloc(sizeof(xvad_preprocessor_t));
    if (!*preprocessor)
        return XVAD_ERROR_MEMORY_ALLOC_FAILED;

    memset(*preprocessor, 0, sizeof(xvad_preprocessor_t));
    (*preprocessor)->config = *config;
    (*preprocessor)->sample_rate = sample_rate;

    // 初始化高通滤波器
    if (config->high_pass_hz > 0) {
        (*preprocessor)->hp_filter = high_pass_filter_create(config->high_pass_hz, sample_rate);
        if (!(*preprocessor)->hp_filter) {
            xvad_preprocessor_destroy(*preprocessor);
            return XVAD_ERROR_BACKEND_ERROR;
        }
    }

    // 初始化RNNoise降噪
#ifdef PREPROCESSOR_ENABLE_RNNOISE
    if (config->denoise) {
        (*preprocessor)->rnnoise_state = rnnoise_create(NULL);
        if (!(*preprocessor)->rnnoise_state) {
            xvad_preprocessor_destroy(*preprocessor);
            return XVAD_ERROR_BACKEND_ERROR;
        }
    }
#endif

    // 初始化幅值归一化
    if (config->normalize_dbfs < 0.0f) {
        (*preprocessor)->normalizer = normalize_create(config->normalize_dbfs, config->max_gain);
        if (!(*preprocessor)->normalizer) {
            xvad_preprocessor_destroy(*preprocessor);
            return XVAD_ERROR_BACKEND_ERROR;
        }
    }

    // 初始化输出缓冲区（最大支持1024样本输入）
    (*preprocessor)->out_buf_size = 1024;
    (*preprocessor)->out_buf = (int16_t*)malloc((*preprocessor)->out_buf_size * sizeof(int16_t));
    if (!(*preprocessor)->out_buf) {
        xvad_preprocessor_destroy(*preprocessor);
        return XVAD_ERROR_MEMORY_ALLOC_FAILED;
    }

    return XVAD_OK;
}

xvad_error_t xvad_preprocessor_process(
    xvad_preprocessor_t* preprocessor,
    const int16_t* in,
    int16_t* out,
    size_t size)
{
    if (!preprocessor || !in || !out || size == 0) {
        return XVAD_ERROR_INVALID_PARAM;
    }

    // 确保输出缓冲区足够大
    if (size > preprocessor->out_buf_size) {
        preprocessor->out_buf = (int16_t*)realloc(preprocessor->out_buf, size * sizeof(int16_t));
        if (!preprocessor->out_buf)
            return XVAD_ERROR_MEMORY_ALLOC_FAILED;
        preprocessor->out_buf_size = size;
    }

    const int16_t* current_in = in;
    int16_t* current_out = preprocessor->out_buf;

    // 1. 高通滤波
    if (preprocessor->hp_filter) {
        high_pass_filter_process(preprocessor->hp_filter, current_in, current_out, size);
        current_in = current_out;
    }

    // 2. RNNoise降噪（处理帧对齐）
#ifdef PREPROCESSOR_ENABLE_RNNOISE
    if (preprocessor->rnnoise_state) {
        size_t processed = 0;

        while (processed < size) {
            size_t remaining = size - processed;
            size_t to_copy = fminf(remaining, RNNOISE_FRAME_SIZE - preprocessor->rnnoise_buf_ptr);

            // 复制到RNNoise缓冲区
            memcpy(
                preprocessor->rnnoise_buf + preprocessor->rnnoise_buf_ptr,
                current_in + processed,
                to_copy * sizeof(int16_t));

            preprocessor->rnnoise_buf_ptr += to_copy;
            processed += to_copy;

            // 缓冲区满，处理一帧
            if (preprocessor->rnnoise_buf_ptr == RNNOISE_FRAME_SIZE) {
                float in_float[RNNOISE_FRAME_SIZE];
                float out_float[RNNOISE_FRAME_SIZE];

                // int16 → float
                for (int i = 0; i < RNNOISE_FRAME_SIZE; i++) {
                    in_float[i] = preprocessor->rnnoise_buf[i] / 32768.0f;
                }

                // RNNoise处理
                rnnoise_process_frame(preprocessor->rnnoise_state, out_float, in_float);

                // float → int16
                for (int i = 0; i < RNNOISE_FRAME_SIZE; i++) {
                    out_float[i] = fmaxf(-1.0f, fminf(1.0f, out_float[i]));
                    preprocessor->rnnoise_buf[i] = (int16_t)(out_float[i] * 32767.0f);
                }

                // 复制到输出
                memcpy(
                    current_out + (processed - RNNOISE_FRAME_SIZE),
                    preprocessor->rnnoise_buf,
                    RNNOISE_FRAME_SIZE * sizeof(int16_t));

                preprocessor->rnnoise_buf_ptr = 0;
            }
        }

        current_in = current_out;
    }
#endif

    // 3. 幅值归一化
    if (preprocessor->normalizer) {
        normalize_process(preprocessor->normalizer, current_in, current_out, size);
        current_in = current_out;
    }

    // 复制最终结果到输出
    memcpy(out, current_in, size * sizeof(int16_t));

    return XVAD_OK;
}

xvad_error_t xvad_preprocessor_reset(xvad_preprocessor_t* preprocessor)
{
    if (!preprocessor)
        return XVAD_ERROR_INVALID_PARAM;

    if (preprocessor->hp_filter) {
        high_pass_filter_reset(preprocessor->hp_filter);
    }
#ifdef PREPROCESSOR_ENABLE_RNNOISE
    if (preprocessor->rnnoise_state) {
        rnnoise_reset(preprocessor->rnnoise_state);
        preprocessor->rnnoise_buf_ptr = 0;
        memset(preprocessor->rnnoise_buf, 0, sizeof(preprocessor->rnnoise_buf));
    }
#endif
    if (preprocessor->normalizer) {
        normalize_reset(preprocessor->normalizer);
    }

    return XVAD_OK;
}

void xvad_preprocessor_destroy(xvad_preprocessor_t* preprocessor)
{
    if (!preprocessor)
        return;

    if (preprocessor->hp_filter) {
        high_pass_filter_destroy(preprocessor->hp_filter);
    }
#ifdef PREPROCESSOR_ENABLE_RNNOISE
    if (preprocessor->rnnoise_state) {
        rnnoise_destroy(preprocessor->rnnoise_state);
    }
#endif
    if (preprocessor->normalizer) {
        normalize_destroy(preprocessor->normalizer);
    }

    if (preprocessor->out_buf) {
        free(preprocessor->out_buf);
    }

    free(preprocessor);
}

// 预定义配置
const xvad_preprocessor_config_t XVAD_PREPROCESSOR_RAW_MIC = {
    .high_pass_hz = 80,
    .denoise = 1,
    .normalize_dbfs = -20.0f,
    .max_gain = MAX_NORMALIZE_GAIN
};

const xvad_preprocessor_config_t XVAD_PREPROCESSOR_TELEPHONY = {
    .high_pass_hz = 200,
    .denoise = 0,
    .normalize_dbfs = -16.0f,
    .max_gain = MAX_NORMALIZE_GAIN
};
