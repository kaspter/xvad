#ifndef XVAD_ADAPTER_H
#define XVAD_ADAPTER_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif


////////////////////////////// 帧适配器 用于处理任意大小的音频块 //////////////////////////
typedef struct xvad_frame_adapter xvad_frame_adapter_t;
// 创建帧适配器
xvad_error_t xvad_frame_adapter_create(xvad_frame_adapter_t** adapter, xvad_handle_t* vad);

// 处理任意大小的音频块
// 输出：results数组，每个元素对应一帧的概率
// 返回：处理的帧数
size_t xvad_frame_adapter_process(xvad_frame_adapter_t* adapter, const int16_t* chunk,
    size_t chunk_size, float* results, size_t max_results);

// 重置帧适配器
xvad_error_t xvad_frame_adapter_reset(xvad_frame_adapter_t* adapter);

// 销毁帧适配器
void xvad_frame_adapter_destroy(xvad_frame_adapter_t* adapter);


#ifdef __cplusplus
}
#endif

#endif // XVAD_ADAPTER_H
