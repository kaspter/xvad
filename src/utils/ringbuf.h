#ifndef XVAD_RINGBUF_H
#define XVAD_RINGBUF_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/////////////////////////////////////////////////////////////

// 环形缓冲区覆盖策略
typedef enum {
    RINGBUF_OVERWRITE_OLD, // 缓冲区满时覆盖最旧的数据（默认，适合实时音频）
    RINGBUF_REJECT_NEW     // 缓冲区满时拒绝写入新数据
} ringbuf_overwrite_policy_t;

// 环形缓冲区句柄
typedef struct ringbuf ringbuf_t;

// 创建环形缓冲区
// capacity: 缓冲区容量（字节数），会自动向上取整到最近的2的幂次
// policy: 覆盖策略
// thread_safe: 是否启用线程安全（需要pthread支持）
ringbuf_t* ringbuf_create(size_t capacity, ringbuf_overwrite_policy_t policy, int thread_safe);

// 写入数据到环形缓冲区
// 返回：实际写入的字节数
size_t ringbuf_write(ringbuf_t* rb, const void* data, size_t size);

// 从环形缓冲区读取数据
// 返回：实际读取的字节数
size_t ringbuf_read(ringbuf_t* rb, void* data, size_t size);

// 查看环形缓冲区中的数据（不移动读指针）
// 返回：实际查看的字节数
size_t ringbuf_peek(ringbuf_t* rb, void* data, size_t size);

// 获取环形缓冲区中可读数据的字节数
size_t ringbuf_available(const ringbuf_t* rb);

// 获取环形缓冲区中剩余可写空间的字节数
size_t ringbuf_free_space(const ringbuf_t* rb);

// 获取环形缓冲区的总容量（字节数）
size_t ringbuf_capacity(const ringbuf_t* rb);

// 检查环形缓冲区是否为空
int ringbuf_is_empty(const ringbuf_t* rb);

// 检查环形缓冲区是否已满
int ringbuf_is_full(const ringbuf_t* rb);

// 重置环形缓冲区（清空所有数据）
void ringbuf_reset(ringbuf_t* rb);

// 销毁环形缓冲区
void ringbuf_destroy(ringbuf_t* rb);
#ifdef __cplusplus
}
#endif

#endif // XVAD_RINGBUF_H
