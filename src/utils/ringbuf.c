#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef RINGBUF_THREAD_SAFE
#include <pthread.h>
#endif

#include "ringbuf.h"

// 环形缓冲区内部结构体
struct ringbuf {
    uint8_t* buffer;		// 数据缓冲区
    size_t capacity;		// 总容量（字节数，2的幂次）
    size_t mask;		// 位掩码（capacity-1）
    size_t read_ptr;		// 读指针
    size_t write_ptr;		// 写指针
    ringbuf_overwrite_policy_t policy; // 覆盖策略
    int thread_safe;		// 是否启用线程安全

#ifdef RINGBUF_THREAD_SAFE
    pthread_mutex_t mutex;	// 互斥锁
#endif
};

// 计算大于等于n的最小2的幂次
static size_t next_power_of_two(size_t n)
{
    if (n == 0)
        return 1;
    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
#if SIZE_MAX > 0xFFFFFFFF
    n |= n >> 32;
#endif
    return n + 1;
}

ringbuf_t* ringbuf_create(size_t capacity, ringbuf_overwrite_policy_t policy,
    int thread_safe)
{
    if (capacity == 0)
        return NULL;

    ringbuf_t* rb = (ringbuf_t*)malloc(sizeof(ringbuf_t));
    if (!rb)
        return NULL;

    memset(rb, 0, sizeof(ringbuf_t));

    // 自动对齐到2的幂次
    rb->capacity = next_power_of_two(capacity);
    rb->mask = rb->capacity - 1;
    rb->policy = policy;
    rb->thread_safe = thread_safe;

    // 分配数据缓冲区
    rb->buffer = (uint8_t*)malloc(rb->capacity);
    if (!rb->buffer) {
        free(rb);
        return NULL;
    }

#ifdef RINGBUF_THREAD_SAFE
    // 初始化互斥锁
    if (thread_safe) {
        if (pthread_mutex_init(&rb->mutex, NULL) != 0) {
            free(rb->buffer);
            free(rb);
            return NULL;
        }
    }
#endif

    return rb;
}

size_t ringbuf_write(ringbuf_t* rb, const void* data, size_t size)
{
    if (!rb || !data || size == 0)
        return 0;

#ifdef RINGBUF_THREAD_SAFE
    if (rb->thread_safe)
        pthread_mutex_lock(&rb->mutex);
#endif

    size_t free_space = ringbuf_free_space(rb);
    size_t write_size = size;

    // 处理缓冲区满的情况
    if (write_size > free_space) {
        if (rb->policy == RINGBUF_REJECT_NEW) {
            write_size = free_space;
            if (write_size == 0) {
#ifdef RINGBUF_THREAD_SAFE
                if (rb->thread_safe)
                    pthread_mutex_unlock(&rb->mutex);
#endif
                return 0;
            }
        } else {
            // 覆盖旧数据：移动读指针
            size_t overflow = write_size - free_space;
            rb->read_ptr = (rb->read_ptr + overflow) & rb->mask;
        }
    }

    // 计算可写的连续字节数
    size_t contiguous = rb->capacity - rb->write_ptr;
    size_t first_part = (write_size < contiguous) ? write_size : contiguous;
    size_t second_part = write_size - first_part;

    // 写入第一部分（到缓冲区末尾）
    memcpy(rb->buffer + rb->write_ptr, data, first_part);

    // 写入第二部分（从缓冲区开头）
    if (second_part > 0) {
        memcpy(rb->buffer, (const uint8_t*)data + first_part, second_part);
    }

    // 更新写指针
    rb->write_ptr = (rb->write_ptr + write_size) & rb->mask;

#ifdef RINGBUF_THREAD_SAFE
    if (rb->thread_safe)
        pthread_mutex_unlock(&rb->mutex);
#endif

    return write_size;
}

size_t ringbuf_read(ringbuf_t* rb, void* data, size_t size)
{
    if (!rb || !data || size == 0)
        return 0;

#ifdef RINGBUF_THREAD_SAFE
    if (rb->thread_safe)
        pthread_mutex_lock(&rb->mutex);
#endif

    size_t available = ringbuf_available(rb);
    size_t read_size = (size < available) ? size : available;

    if (read_size == 0) {
#ifdef RINGBUF_THREAD_SAFE
        if (rb->thread_safe)
            pthread_mutex_unlock(&rb->mutex);
#endif
        return 0;
    }

    // 计算可读的连续字节数
    size_t contiguous = rb->capacity - rb->read_ptr;
    size_t first_part = (read_size < contiguous) ? read_size : contiguous;
    size_t second_part = read_size - first_part;

    // 读取第一部分（到缓冲区末尾）
    memcpy(data, rb->buffer + rb->read_ptr, first_part);

    // 读取第二部分（从缓冲区开头）
    if (second_part > 0) {
        memcpy((uint8_t*)data + first_part, rb->buffer, second_part);
    }

    // 更新读指针
    rb->read_ptr = (rb->read_ptr + read_size) & rb->mask;

#ifdef RINGBUF_THREAD_SAFE
    if (rb->thread_safe)
        pthread_mutex_unlock(&rb->mutex);
#endif

    return read_size;
}

size_t ringbuf_peek(ringbuf_t* rb, void* data, size_t size)
{
    if (!rb || !data || size == 0)
        return 0;

#ifdef RINGBUF_THREAD_SAFE
    if (rb->thread_safe)
        pthread_mutex_lock(&rb->mutex);
#endif

    size_t available = ringbuf_available(rb);
    size_t peek_size = (size < available) ? size : available;

    if (peek_size == 0) {
#ifdef RINGBUF_THREAD_SAFE
        if (rb->thread_safe)
            pthread_mutex_unlock(&rb->mutex);
#endif
        return 0;
    }

    // 计算可查看的连续字节数
    size_t contiguous = rb->capacity - rb->read_ptr;
    size_t first_part = (peek_size < contiguous) ? peek_size : contiguous;
    size_t second_part = peek_size - first_part;

    // 查看第一部分
    memcpy(data, rb->buffer + rb->read_ptr, first_part);

    // 查看第二部分
    if (second_part > 0) {
        memcpy((uint8_t*)data + first_part, rb->buffer, second_part);
    }

#ifdef RINGBUF_THREAD_SAFE
    if (rb->thread_safe)
        pthread_mutex_unlock(&rb->mutex);
#endif

    return peek_size;
}

size_t ringbuf_available(const ringbuf_t* rb)
{
    if (!rb)
        return 0;
    return (rb->write_ptr - rb->read_ptr) & rb->mask;
}

size_t ringbuf_free_space(const ringbuf_t* rb)
{
    if (!rb)
        return 0;
    return rb->capacity - ringbuf_available(rb);
}

size_t ringbuf_capacity(const ringbuf_t* rb)
{
    return rb ? rb->capacity : 0;
}

int ringbuf_is_empty(const ringbuf_t* rb)
{
    return ringbuf_available(rb) == 0;
}

int ringbuf_is_full(const ringbuf_t* rb)
{
    return ringbuf_free_space(rb) == 0;
}

void ringbuf_reset(ringbuf_t* rb)
{
    if (!rb)
        return;

#ifdef RINGBUF_THREAD_SAFE
    if (rb->thread_safe)
        pthread_mutex_lock(&rb->mutex);
#endif

    rb->read_ptr = 0;
    rb->write_ptr = 0;

#ifdef RINGBUF_THREAD_SAFE
    if (rb->thread_safe)
        pthread_mutex_unlock(&rb->mutex);
#endif
}

void ringbuf_destroy(ringbuf_t* rb)
{
    if (!rb)
        return;

#ifdef RINGBUF_THREAD_SAFE
    if (rb->thread_safe) {
        pthread_mutex_destroy(&rb->mutex);
    }
#endif

    if (rb->buffer) {
        free(rb->buffer);
    }

    free(rb);
}
