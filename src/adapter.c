#include "utils/ringbuf.h"
#include "xvad.h"
#include "adapter.h"
#include <stdlib.h>
#include <string.h>

struct xvad_frame_adapter {
    xvad_handle_t* vad;
    ringbuf_t* ringbuf;
    size_t frame_size;
    int16_t* frame_buffer;
};

xvad_error_t xvad_frame_adapter_create(xvad_frame_adapter_t** adapter, xvad_handle_t* vad)
{
    if (!adapter || !vad)
        return XVAD_ERROR_INVALID_PARAM;

    xvad_backend_t backend;
    xvad_error_t err = xvad_get_backend_type(vad, &backend);
    if (err != XVAD_OK)
        return err;

    xvad_capabilities_t caps;
    err = xvad_get_capabilities(backend, &caps);
    if (err != XVAD_OK)
        return err;

    *adapter = (xvad_frame_adapter_t*)malloc(sizeof(xvad_frame_adapter_t));
    if (!*adapter)
        return XVAD_ERROR_MEMORY_ALLOC_FAILED;

    (*adapter)->vad = vad;
    (*adapter)->frame_size = caps.frame_size;

    // 创建环形缓冲区（存储4帧数据）
    // 实时音频使用覆盖旧数据模式, 单线程使用，无需线程安全
    (*adapter)->ringbuf = ringbuf_create(4 * caps.frame_size * sizeof(int16_t), RINGBUF_OVERWRITE_OLD, 0);
    if (!(*adapter)->ringbuf) {
        free(*adapter);
        return XVAD_ERROR_MEMORY_ALLOC_FAILED;
    }

    // 帧缓冲区
    (*adapter)->frame_buffer = (int16_t*)malloc(caps.frame_size * sizeof(int16_t));
    if (!(*adapter)->frame_buffer) {
        ringbuf_destroy((*adapter)->ringbuf);
        free(*adapter);
        return XVAD_ERROR_MEMORY_ALLOC_FAILED;
    }

    return XVAD_OK;
}

size_t xvad_frame_adapter_process(xvad_frame_adapter_t* adapter, const int16_t* chunk, size_t chunk_size, float* results, size_t max_results)
{
    if (!adapter || !chunk || !results || max_results == 0)
        return 0;

    // 写入环形缓冲区
    ringbuf_write(adapter->ringbuf, chunk, chunk_size * sizeof(int16_t));

    size_t frames_processed = 0;

    // 处理所有完整帧
    while (ringbuf_available(adapter->ringbuf) >= adapter->frame_size * sizeof(int16_t) && frames_processed < max_results) {

        // 读取一帧
        ringbuf_read(adapter->ringbuf, adapter->frame_buffer, adapter->frame_size * sizeof(int16_t));

        // 处理帧
        float prob;
        xvad_process_frame(adapter->vad, adapter->frame_buffer, &prob);

        results[frames_processed++] = prob;
    }

    return frames_processed;
}

xvad_error_t xvad_frame_adapter_reset(xvad_frame_adapter_t* adapter)
{
    if (!adapter)
        return XVAD_ERROR_INVALID_PARAM;
    ringbuf_reset(adapter->ringbuf);
    xvad_reset(adapter->vad);
    return XVAD_OK;
}

void xvad_frame_adapter_destroy(xvad_frame_adapter_t* adapter)
{
    if (!adapter)
        return;
    ringbuf_destroy(adapter->ringbuf);
    free(adapter->frame_buffer);
    free(adapter);
}
