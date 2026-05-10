#include "xvad.h"
#include <stdlib.h>
#include <string.h>

// 后端接口结构体（C语言多态实现）
typedef struct {
    xvad_error_t (*create)(void** backend, const void* config);
    xvad_error_t (*process)(void* backend, const int16_t* frame, float* prob);
    xvad_error_t (*reset)(void* backend);
    void (*destroy)(void* backend);
} xvad_backend_interface_t;

// VAD句柄结构体
struct xvad_handle {
    xvad_backend_t backend_type;
    void* backend_data;
    const xvad_backend_interface_t* backend_interface;
    xvad_capabilities_t caps;
};

// 各后端接口声明
extern const xvad_backend_interface_t xvad_webrtc_interface;
extern const xvad_backend_interface_t xvad_ten_vad_interface;
extern const xvad_backend_interface_t xvad_silero_interface;
extern const xvad_backend_interface_t xvad_firered_interface;

// 各后端能力声明
extern const xvad_capabilities_t xvad_webrtc_caps;
extern const xvad_capabilities_t xvad_ten_vad_caps;
extern const xvad_capabilities_t xvad_silero_caps;
extern const xvad_capabilities_t xvad_firered_caps;

xvad_error_t xvad_create(xvad_handle_t** handle, xvad_backend_t backend, const void* config)
{
    if (!handle)
        return XVAD_ERROR_INVALID_PARAM;

    *handle = (xvad_handle_t*)malloc(sizeof(xvad_handle_t));
    if (!*handle)
        return XVAD_ERROR_MEMORY_ALLOC_FAILED;

    memset(*handle, 0, sizeof(xvad_handle_t));
    (*handle)->backend_type = backend;

    // 绑定后端接口和能力
    switch (backend) {
    case XVAD_BACKEND_WEBRTC:
        (*handle)->backend_interface = &xvad_webrtc_interface;
        (*handle)->caps = xvad_webrtc_caps;
        break;
    case XVAD_BACKEND_TEN_VAD:
        (*handle)->backend_interface = &xvad_ten_vad_interface;
        (*handle)->caps = xvad_ten_vad_caps;
        break;
    case XVAD_BACKEND_SILERO:
        (*handle)->backend_interface = &xvad_silero_interface;
        (*handle)->caps = xvad_silero_caps;
        break;
    case XVAD_BACKEND_FIRERED:
        (*handle)->backend_interface = &xvad_firered_interface;
        (*handle)->caps = xvad_firered_caps;
        break;
    default:
        free(*handle);
        *handle = NULL;
        return XVAD_ERROR_INVALID_PARAM;
    }

    // 创建后端实例
    xvad_error_t err = (*handle)->backend_interface->create(
        &(*handle)->backend_data,
        config);

    if (err != XVAD_OK) {
        free(*handle);
        *handle = NULL;
        return err;
    }

    return XVAD_OK;
}

xvad_error_t xvad_get_capabilities(xvad_backend_t backend, xvad_capabilities_t* caps)
{
    if (!caps)
        return XVAD_ERROR_INVALID_PARAM;

    switch (backend) {
    case XVAD_BACKEND_WEBRTC:
        *caps = xvad_webrtc_caps;
        break;
    case XVAD_BACKEND_TEN_VAD:
        *caps = xvad_ten_vad_caps;
        break;
    case XVAD_BACKEND_SILERO:
        *caps = xvad_silero_caps;
        break;
    case XVAD_BACKEND_FIRERED:
        *caps = xvad_firered_caps;
        break;
    default:
        return XVAD_ERROR_INVALID_PARAM;
    }

    return XVAD_OK;
}

// 在xvad.c中添加这个函数实现
xvad_error_t xvad_get_backend_type(xvad_handle_t* handle, xvad_backend_t* backend_type)
{
    if (!handle || !backend_type)
        return XVAD_ERROR_INVALID_PARAM;

    *backend_type = handle->backend_type;
    return XVAD_OK;
}

xvad_error_t xvad_process_frame(
    xvad_handle_t* handle,
    const int16_t* frame,
    float* probability)
{
    if (!handle || !frame || !probability)
        return XVAD_ERROR_INVALID_PARAM;
    return handle->backend_interface->process(handle->backend_data, frame, probability);
}

xvad_error_t xvad_reset(xvad_handle_t* handle)
{
    if (!handle)
        return XVAD_ERROR_INVALID_PARAM;
    return handle->backend_interface->reset(handle->backend_data);
}

void xvad_destroy(xvad_handle_t* handle)
{
    if (!handle)
        return;
    handle->backend_interface->destroy(handle->backend_data);
    free(handle);
}

// 预定义预处理配置
const xvad_preprocessor_config_t XVAD_PREPROCESSOR_RAW_MIC = {
    .high_pass_hz = 80,
    .denoise = 1,
    .normalize_dbfs = -20.0f
};

const xvad_preprocessor_config_t XVAD_PREPROCESSOR_TELEPHONY = {
    .high_pass_hz = 200,
    .denoise = 0,
    .normalize_dbfs = 0.0f
};
