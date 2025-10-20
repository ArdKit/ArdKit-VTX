/*
 * Copyright 2025 ArdKit
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
/**
 * @file vtx_frame.c
 * @brief VTX Frame & Memory Pool Implementation
 */

#include "vtx_frame.h"
#include "vtx_error.h"
#include "vtx_log.h"
#include "vtx_mem.h"
#include <string.h>
#include <sys/time.h>

/* ========== 内存池结构 ========== */

/**
 * @brief 帧内存池完整定义
 */
struct vtx_frame_pool {
    struct list_head   free_list;    /* 空闲frame链表 */
    size_t             free_count;   /* 空闲frame数量 */
    size_t             total_count;  /* 总frame数量 */
    size_t             data_size;    /* 每个frame的data大小 */
    vtx_spinlock_t     lock;         /* 自旋锁 */

    /* 统计信息 */
    size_t             peak_count;   /* 峰值使用数量 */
    size_t             total_allocs; /* 总分配次数 */
    size_t             total_frees;  /* 总释放次数 */
};

/**
 * @brief 分片内存池完整定义
 */
/* Slab大小常量 */
#define VTX_FRAG_SLAB_SIZE_1    1
#define VTX_FRAG_SLAB_SIZE_32   32
#define VTX_FRAG_SLAB_SIZE_128  128
#define VTX_FRAG_SLAB_SIZE_256  256
#define VTX_FRAG_SLAB_SIZE_512  512
#define VTX_FRAG_SLAB_COUNT     5

/* Slab池结构 */
struct vtx_frag_pool {
    struct list_head   slab_free[VTX_FRAG_SLAB_COUNT];  /* 各大小的空闲slab链表 */
    size_t             slab_free_count[VTX_FRAG_SLAB_COUNT];  /* 各大小的空闲数量 */
    size_t             slab_total_count[VTX_FRAG_SLAB_COUNT]; /* 各大小的总数量 */
    uint16_t           slab_sizes[VTX_FRAG_SLAB_COUNT];  /* slab大小数组 */
    vtx_spinlock_t     lock;         /* 自旋锁 */

    /* 统计信息 */
    size_t             total_allocs; /* 总分配次数 */
    size_t             total_frees;  /* 总释放次数 */
};

/* ========== 辅助函数 ========== */

/**
 * @brief 获取当前时间（毫秒）
 */
static uint64_t vtx_get_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

/**
 * @brief 分配一个新frame
 *
 * @param data_size 数据缓冲区大小
 * @return vtx_frame_t* 成功返回frame，失败返回NULL
 */
static vtx_frame_t* vtx_frame_alloc(size_t data_size) {
    vtx_frame_t* frame = (vtx_frame_t*)vtx_calloc(1, sizeof(vtx_frame_t));
    if (!frame) {
        vtx_log_error("Failed to allocate frame");
        return NULL;
    }

    /* 分配数据缓冲区 */
    frame->data = (uint8_t*)vtx_malloc(data_size);
    if (!frame->data) {
        vtx_log_error("Failed to allocate frame data buffer: %zu bytes", data_size);
        vtx_free(frame);
        return NULL;
    }

    /* 初始化frame */
    INIT_LIST_HEAD(&frame->list);
    atomic_init(&frame->refcount, 0);
    frame->state = VTX_FRAME_STATE_FREE;
    frame->data_capacity = data_size;
    frame->retran = NULL;
    frame->data_size = 0;

    return frame;
}

/**
 * @brief 释放frame
 *
 * @param frame frame对象
 */
static void vtx_frame_free(vtx_frame_t* frame) {
    if (!frame) {
        return;
    }

    /* 释放retran（如果有的话，应该已在reset中释放） */
    if (frame->retran) {
        vtx_log_warn("Frame being freed still has retran allocated");
        vtx_free(frame->retran);
        frame->retran = NULL;
    }

    /* 释放数据缓冲区 */
    if (frame->data) {
        vtx_free(frame->data);
        frame->data = NULL;
    }

    vtx_free(frame);
}

/* ========== 内存池管理 ========== */

vtx_frame_pool_t* vtx_frame_pool_create(size_t initial_size, size_t data_size) {
    if (data_size == 0) {
        vtx_log_error("Invalid data_size: 0");
        return NULL;
    }

    vtx_frame_pool_t* pool = (vtx_frame_pool_t*)vtx_calloc(1, sizeof(vtx_frame_pool_t));
    if (!pool) {
        vtx_log_error("Failed to allocate frame pool");
        return NULL;
    }

    /* 初始化池 */
    INIT_LIST_HEAD(&pool->free_list);
    pool->data_size = data_size;
    vtx_spinlock_init(&pool->lock);

    /* 预分配frames */
    for (size_t i = 0; i < initial_size; i++) {
        vtx_frame_t* frame = vtx_frame_alloc(data_size);
        if (!frame) {
            vtx_log_warn("Failed to preallocate frame %zu/%zu", i, initial_size);
            break;
        }
        list_add_tail(&frame->list, &pool->free_list);
        pool->free_count++;
        pool->total_count++;
    }

    vtx_log_info("Frame pool created: initial=%zu, data_size=%zu",
                 pool->free_count, data_size);

    return pool;
}

void vtx_frame_pool_destroy(vtx_frame_pool_t* pool) {
    if (!pool) {
        return;
    }

    vtx_spinlock_lock(&pool->lock);

    /* 释放所有空闲frames */
    while (!list_empty(&pool->free_list)) {
        vtx_frame_t* frame = list_first_entry(&pool->free_list,
                                               vtx_frame_t, list);
        list_del(&frame->list);
        vtx_frame_free(frame);
    }

    size_t leaked = pool->total_count - pool->free_count;
    if (leaked > 0) {
        vtx_log_warn("Frame pool destroyed with %zu leaked frames", leaked);
    }

    vtx_spinlock_unlock(&pool->lock);
    vtx_spinlock_destroy(&pool->lock);

    vtx_log_info("Frame pool destroyed: total=%zu, leaked=%zu",
                 pool->total_count, leaked);

    vtx_free(pool);
}

vtx_frame_t* vtx_frame_pool_acquire(vtx_frame_pool_t* pool) {
    if (!pool) {
        return NULL;
    }

    vtx_spinlock_lock(&pool->lock);

    vtx_frame_t* frame = NULL;

    /* 从空闲链表获取 */
    if (!list_empty(&pool->free_list)) {
        frame = list_first_entry(&pool->free_list, vtx_frame_t, list);
        list_del_init(&frame->list);
        pool->free_count--;
    }

    vtx_spinlock_unlock(&pool->lock);

    /* 如果池为空，分配新frame */
    if (!frame) {
        frame = vtx_frame_alloc(pool->data_size);
        if (!frame) {
            return NULL;
        }

        vtx_spinlock_lock(&pool->lock);
        pool->total_count++;
        vtx_spinlock_unlock(&pool->lock);

        vtx_log_debug("Frame pool expanded: total=%zu", pool->total_count);
    }

    /* 初始化frame状态 */
    atomic_store(&frame->refcount, 1);
    frame->state = VTX_FRAME_STATE_FREE;

    /* 更新统计 */
    vtx_spinlock_lock(&pool->lock);
    pool->total_allocs++;
    size_t used = pool->total_count - pool->free_count;
    if (used > pool->peak_count) {
        pool->peak_count = used;
    }
    vtx_spinlock_unlock(&pool->lock);

    return frame;
}

void vtx_frame_pool_release(vtx_frame_pool_t* pool, vtx_frame_t* frame) {
    if (!pool || !frame) {
        return;
    }

    /* 重置frame状态 */
    vtx_frame_reset(frame);

    /* 归还到空闲链表 */
    vtx_spinlock_lock(&pool->lock);
    list_add_tail(&frame->list, &pool->free_list);
    pool->free_count++;
    pool->total_frees++;
    vtx_spinlock_unlock(&pool->lock);
}

/* ========== 分片池管理 ========== */

/**
 * @brief 将num_frags向上取整到slab大小
 *
 * @param num_frags 实际需要的分片数
 * @return int slab索引，-1表示超出范围
 */
static int vtx_frag_get_slab_index(uint16_t num_frags) {
    if (num_frags <= VTX_FRAG_SLAB_SIZE_1) {
        return 0;
    } else if (num_frags <= VTX_FRAG_SLAB_SIZE_32) {
        return 1;
    } else if (num_frags <= VTX_FRAG_SLAB_SIZE_128) {
        return 2;
    } else if (num_frags <= VTX_FRAG_SLAB_SIZE_256) {
        return 3;
    } else if (num_frags <= VTX_FRAG_SLAB_SIZE_512) {
        return 4;
    } else {
        return -1;  /* 超出最大支持 */
    }
}

vtx_frag_pool_t* vtx_frag_pool_create(void) {
    vtx_frag_pool_t* pool = (vtx_frag_pool_t*)vtx_calloc(1, sizeof(vtx_frag_pool_t));
    if (!pool) {
        vtx_log_error("Failed to allocate frag pool");
        return NULL;
    }

    /* 初始化slab大小数组 */
    pool->slab_sizes[0] = VTX_FRAG_SLAB_SIZE_1;
    pool->slab_sizes[1] = VTX_FRAG_SLAB_SIZE_32;
    pool->slab_sizes[2] = VTX_FRAG_SLAB_SIZE_128;
    pool->slab_sizes[3] = VTX_FRAG_SLAB_SIZE_256;
    pool->slab_sizes[4] = VTX_FRAG_SLAB_SIZE_512;

    /* 初始化各slab的空闲链表 */
    for (int i = 0; i < VTX_FRAG_SLAB_COUNT; i++) {
        INIT_LIST_HEAD(&pool->slab_free[i]);
        pool->slab_free_count[i] = 0;
        pool->slab_total_count[i] = 0;
    }

    vtx_spinlock_init(&pool->lock);

    vtx_log_info("Frag pool created with slab sizes: 1, 32, 128, 256, 512");

    return pool;
}

void vtx_frag_pool_destroy(vtx_frag_pool_t* pool) {
    if (!pool) {
        return;
    }

    vtx_spinlock_lock(&pool->lock);

    size_t total_leaked = 0;

    /* 释放所有slab */
    for (int i = 0; i < VTX_FRAG_SLAB_COUNT; i++) {
        vtx_frag_header_t* header;
        vtx_frag_header_t* tmp;

        list_for_each_entry_safe(header, tmp, &pool->slab_free[i], list) {
            list_del(&header->list);
            vtx_free(header);
        }

        size_t leaked = pool->slab_total_count[i] - pool->slab_free_count[i];
        total_leaked += leaked;

        if (leaked > 0) {
            vtx_log_warn("Frag pool slab[%d] (size=%u): leaked=%zu",
                        i, pool->slab_sizes[i], leaked);
        }
    }

    vtx_spinlock_unlock(&pool->lock);
    vtx_spinlock_destroy(&pool->lock);

    vtx_log_info("Frag pool destroyed: total_allocs=%zu, total_frees=%zu, leaked=%zu",
                pool->total_allocs, pool->total_frees, total_leaked);

    vtx_free(pool);
}

vtx_frag_header_t* vtx_frag_pool_acquire(vtx_frag_pool_t* pool, uint16_t num_frags) {
    if (!pool || num_frags == 0) {
        return NULL;
    }

    /* 确定slab索引 */
    int slab_idx = vtx_frag_get_slab_index(num_frags);
    if (slab_idx < 0) {
        vtx_log_error("num_frags %u exceeds max slab size %u",
                     num_frags, VTX_FRAG_SLAB_SIZE_512);
        return NULL;
    }

    uint16_t capacity = pool->slab_sizes[slab_idx];

    vtx_spinlock_lock(&pool->lock);

    vtx_frag_header_t* header = NULL;

    /* 尝试从空闲链表获取 */
    if (!list_empty(&pool->slab_free[slab_idx])) {
        header = list_first_entry(&pool->slab_free[slab_idx],
                                  vtx_frag_header_t, list);
        list_del(&header->list);
        pool->slab_free_count[slab_idx]--;
    } else {
        /* 空闲链表为空，分配新slab */
        vtx_spinlock_unlock(&pool->lock);

        size_t alloc_size = sizeof(vtx_frag_header_t) + capacity * sizeof(vtx_frag_t);
        header = (vtx_frag_header_t*)vtx_malloc(alloc_size);
        if (!header) {
            vtx_log_error("Failed to allocate frag slab: size=%zu", alloc_size);
            return NULL;
        }

        vtx_spinlock_lock(&pool->lock);
        pool->slab_total_count[slab_idx]++;
    }

    pool->total_allocs++;

    vtx_spinlock_unlock(&pool->lock);

    /* 初始化header */
    INIT_LIST_HEAD(&header->list);
    header->capacity = capacity;
    header->num = num_frags;

    /* 清零所有frags */
    memset(header->frag, 0, capacity * sizeof(vtx_frag_t));

    return header;
}

void vtx_frag_pool_release(vtx_frag_pool_t* pool, vtx_frag_header_t* header) {
    if (!pool || !header) {
        return;
    }

    uint16_t capacity = header->capacity;

    /* 确定slab索引 */
    int slab_idx = vtx_frag_get_slab_index(capacity);
    if (slab_idx < 0 || pool->slab_sizes[slab_idx] != capacity) {
        vtx_log_error("Invalid frag header capacity: %u", capacity);
        vtx_free(header);
        return;
    }

    /* 清零header */
    memset(header, 0, sizeof(vtx_frag_header_t) + capacity * sizeof(vtx_frag_t));

    /* 归还到对应slab的空闲链表 */
    vtx_spinlock_lock(&pool->lock);
    INIT_LIST_HEAD(&header->list);
    list_add_tail(&header->list, &pool->slab_free[slab_idx]);
    pool->slab_free_count[slab_idx]++;
    pool->total_frees++;
    vtx_spinlock_unlock(&pool->lock);
}

/* ========== frame引用计数 ========== */

vtx_frame_t* vtx_frame_retain(vtx_frame_t* frame) {
    if (!frame) {
        return NULL;
    }

    atomic_fetch_add(&frame->refcount, 1);
    return frame;
}

void vtx_frame_release(vtx_frame_pool_t* pool, vtx_frame_t* frame) {
    if (!frame) {
        return;
    }

    int old_refcount = atomic_fetch_sub(&frame->refcount, 1);
    if (old_refcount <= 0) {
        vtx_log_error("Frame refcount underflow");
        return;
    }

    /* 引用计数降为0，归还到池中 */
    if (old_refcount == 1) {
        if (pool) {
            vtx_frame_pool_release(pool, frame);
        } else {
            vtx_frame_free(frame);
        }
    }
}

int vtx_frame_get_refcount(vtx_frame_t* frame) {
    if (!frame) {
        return 0;
    }
    return atomic_load(&frame->refcount);
}

/* ========== frame数据复制 ========== */

size_t vtx_frame_copyfrom(
    const vtx_frame_t* frame,
    size_t offset,
    uint8_t* dst,
    size_t size)
{
    /* 参数检查 */
    if (!frame || !dst || size == 0) {
        return 0;
    }

    if (!frame->data) {
        return 0;
    }

    /* 边界检查：offset + size 不能超过 data_size */
    if (offset >= frame->data_size) {
        return 0;
    }

    /* 计算实际可复制的大小 */
    size_t available = frame->data_size - offset;
    if (size > available) {
        vtx_log_warn("copyfrom: requested %zu bytes at offset %zu, but only %zu available",
                     size, offset, available);
        return 0;  /* 严格检查：要求的大小超出边界则失败 */
    }

    /* 复制数据 */
    memcpy(dst, frame->data + offset, size);
    return size;
}

size_t vtx_frame_copyto(
    vtx_frame_t* frame,
    size_t offset,
    const uint8_t* src,
    size_t size)
{
    /* 参数检查 */
    if (!frame || !src || size == 0) {
        return 0;
    }

    if (!frame->data) {
        return 0;
    }

    /* 边界检查：offset + size 不能超过 data_capacity */
    if (offset >= frame->data_capacity) {
        return 0;
    }

    size_t available = frame->data_capacity - offset;
    if (size > available) {
        vtx_log_warn("copyto: requested %zu bytes at offset %zu, but capacity only allows %zu",
                     size, offset, available);
        return 0;  /* 严格检查：要求的大小超出容量则失败 */
    }

    /* 复制数据 */
    memcpy(frame->data + offset, src, size);

    /* 更新 data_size：设置为 max(data_size, offset + size) */
    size_t new_size = offset + size;
    if (new_size > frame->data_size) {
        frame->data_size = new_size;
    }

    return size;
}

/* ========== frame分片管理 ========== */

int vtx_frame_init_recv(
    vtx_frame_t* frame,
    vtx_frag_pool_t* frag_pool,
    uint16_t frame_id,
    vtx_frame_type_t frame_type,
    uint16_t total_frags)
{
    if (!frame || !frag_pool || total_frags == 0) {
        return VTX_ERR_INVALID_PARAM;
    }

    /* 设置frame信息 */
    frame->frame_id = frame_id;
    frame->frame_type = frame_type;
    frame->total_frags = total_frags;
    frame->recv_frags = 0;
    frame->data_size = 0;
    frame->state = VTX_FRAME_STATE_RECEIVING;
    frame->retrans_count = 0;

    /* 从frag_pool分配retran用于跟踪接收状态 */
    frame->retran = vtx_frag_pool_acquire(frag_pool, total_frags);
    if (!frame->retran) {
        vtx_log_error("Failed to allocate retran for %u frags", total_frags);
        return VTX_ERR_NO_MEMORY;
    }

    /* 初始化所有frags为未接收状态 */
    for (uint16_t i = 0; i < total_frags; i++) {
        frame->retran->frag[i].frag_index = i;
        frame->retran->frag[i].received = false;
        frame->retran->frag[i].retrans_count = 0;
        frame->retran->frag[i].send_time_ms = 0;
        frame->retran->frag[i].seq_num = 0;
    }

    /* 记录时间戳 */
    frame->first_recv_ms = vtx_get_time_ms();
    frame->last_recv_ms = frame->first_recv_ms;

    return VTX_OK;
}

bool vtx_frame_is_complete(const vtx_frame_t* frame) {
    if (!frame) {
        return false;
    }
    return frame->recv_frags == frame->total_frags;
}

bool vtx_frame_has_frag(const vtx_frame_t* frame, uint16_t frag_index) {
    if (!frame || !frame->retran || frag_index >= frame->total_frags) {
        return false;
    }

    return frame->retran->frag[frag_index].received;
}

size_t vtx_frame_get_missing_frags(
    const vtx_frame_t* frame,
    uint16_t* missing,
    size_t max_missing)
{
    if (!frame || !missing) {
        return 0;
    }

    size_t count = 0;
    for (uint16_t i = 0; i < frame->total_frags; i++) {
        if (!vtx_frame_has_frag(frame, i)) {
            if (count < max_missing) {
                missing[count] = i;
            }
            count++;
        }
    }

    return count;
}

int vtx_frame_mark_frag_received(vtx_frame_t* frame, uint16_t frag_index) {
    if (!frame || !frame->retran) {
        return VTX_ERR_INVALID_PARAM;
    }

    if (frag_index >= frame->total_frags) {
        return VTX_ERR_INVALID_PARAM;
    }

    /* 检查是否已接收 */
    if (frame->retran->frag[frag_index].received) {
        return VTX_OK;  /* 重复分片，忽略 */
    }

    /* 标记为已接收 */
    frame->retran->frag[frag_index].received = true;

    /* 更新计数和时间戳 */
    frame->recv_frags++;
    frame->last_recv_ms = vtx_get_time_ms();

    /* 检查是否完整 */
    if (vtx_frame_is_complete(frame)) {
        frame->state = VTX_FRAME_STATE_COMPLETE;
    }

    return VTX_OK;
}

void vtx_frame_reset(vtx_frame_t* frame) {
    if (!frame) {
        return;
    }

    /* 释放retran（注意：调用者应在此之前通过frag_pool释放retran） */
    if (frame->retran) {
        vtx_log_warn("Frame reset with retran still allocated - caller should release to pool first");
        /* 这里不释放，因为应该由调用者通过vtx_frag_pool_release归还 */
        frame->retran = NULL;
    }

    /* 重置字段 */
    atomic_store(&frame->refcount, 0);
    frame->state = VTX_FRAME_STATE_FREE;
    frame->frame_id = 0;
    frame->frame_type = 0;
    frame->total_frags = 0;
    frame->recv_frags = 0;
    frame->data_size = 0;
    frame->first_recv_ms = 0;
    frame->last_recv_ms = 0;
    frame->send_time_ms = 0;
    frame->retrans_count = 0;

    /* data缓冲区保留，不释放 */
}

/* ========== 帧队列管理 ========== */

vtx_frame_queue_t* vtx_frame_queue_create(
    vtx_frame_pool_t* pool,
    uint32_t timeout_ms)
{
    vtx_frame_queue_t* queue = (vtx_frame_queue_t*)vtx_calloc(1, sizeof(vtx_frame_queue_t));
    if (!queue) {
        vtx_log_error("Failed to allocate frame queue");
        return NULL;
    }

    INIT_LIST_HEAD(&queue->frames);
    queue->count = 0;
    queue->pool = pool;
    queue->timeout_ms = timeout_ms;
    vtx_spinlock_init(&queue->lock);

    return queue;
}

void vtx_frame_queue_destroy(vtx_frame_queue_t* queue) {
    if (!queue) {
        return;
    }

    vtx_spinlock_lock(&queue->lock);

    /* 释放所有frames */
    while (!list_empty(&queue->frames)) {
        vtx_frame_t* frame = list_first_entry(&queue->frames,
                                               vtx_frame_t, list);
        list_del(&frame->list);
        vtx_frame_release(queue->pool, frame);
        queue->count--;
    }

    vtx_spinlock_unlock(&queue->lock);
    vtx_spinlock_destroy(&queue->lock);

    vtx_free(queue);
}

void vtx_frame_queue_push(vtx_frame_queue_t* queue, vtx_frame_t* frame) {
    if (!queue || !frame) {
        return;
    }

    vtx_spinlock_lock(&queue->lock);

    /* 增加引用计数 */
    vtx_frame_retain(frame);

    /* 添加到队列尾部 */
    list_add_tail(&frame->list, &queue->frames);
    queue->count++;

    vtx_spinlock_unlock(&queue->lock);
}

vtx_frame_t* vtx_frame_queue_pop(vtx_frame_queue_t* queue) {
    if (!queue) {
        return NULL;
    }

    vtx_spinlock_lock(&queue->lock);

    vtx_frame_t* frame = NULL;
    if (!list_empty(&queue->frames)) {
        frame = list_first_entry(&queue->frames, vtx_frame_t, list);
        list_del_init(&frame->list);
        queue->count--;
    }

    vtx_spinlock_unlock(&queue->lock);

    return frame;
}

vtx_frame_t* vtx_frame_queue_find(
    vtx_frame_queue_t* queue,
    uint16_t frame_id)
{
    if (!queue) {
        return NULL;
    }

    vtx_spinlock_lock(&queue->lock);

    vtx_frame_t* result = NULL;
    vtx_frame_t* frame;
    list_for_each_entry(frame, &queue->frames, list) {
        if (frame->frame_id == frame_id) {
            result = frame;
            break;
        }
    }

    vtx_spinlock_unlock(&queue->lock);

    return result;
}

void vtx_frame_queue_remove(vtx_frame_queue_t* queue, vtx_frame_t* frame) {
    if (!queue || !frame) {
        return;
    }

    vtx_spinlock_lock(&queue->lock);

    /* 检查frame是否在队列中 */
    if (!list_empty(&frame->list)) {
        list_del_init(&frame->list);
        queue->count--;

        /* 减少引用计数（对应push时的retain） */
        vtx_frame_release(queue->pool, frame);
    }

    vtx_spinlock_unlock(&queue->lock);
}

size_t vtx_frame_queue_cleanup_timeout(
    vtx_frame_queue_t* queue,
    uint64_t now_ms)
{
    if (!queue || queue->timeout_ms == 0) {
        return 0;
    }

    size_t cleaned = 0;

    vtx_spinlock_lock(&queue->lock);

    vtx_frame_t* frame;
    vtx_frame_t* tmp;
    list_for_each_entry_safe(frame, tmp, &queue->frames, list) {
        uint64_t elapsed = now_ms - frame->first_recv_ms;
        if (elapsed >= queue->timeout_ms) {
            vtx_log_debug("Frame timeout: id=%u, elapsed=%llu ms",
                         frame->frame_id, (unsigned long long)elapsed);

            list_del(&frame->list);
            queue->count--;
            vtx_frame_release(queue->pool, frame);
            cleaned++;
        }
    }

    vtx_spinlock_unlock(&queue->lock);

    return cleaned;
}

/* ========== 内存池统计 ========== */

int vtx_frame_pool_get_stats(
    vtx_frame_pool_t* pool,
    vtx_frame_pool_stats_t* stats)
{
    if (!pool || !stats) {
        return VTX_ERR_INVALID_PARAM;
    }

    vtx_spinlock_lock(&pool->lock);

    stats->total_frames = pool->total_count;
    stats->free_frames = pool->free_count;
    stats->used_frames = pool->total_count - pool->free_count;
    stats->peak_frames = pool->peak_count;
    stats->total_allocs = pool->total_allocs;
    stats->total_frees = pool->total_frees;
    stats->data_size = pool->data_size;

    vtx_spinlock_unlock(&pool->lock);

    return VTX_OK;
}

void vtx_frame_pool_print_stats(vtx_frame_pool_t* pool) {
    if (!pool) {
        return;
    }

    vtx_frame_pool_stats_t stats;
    if (vtx_frame_pool_get_stats(pool, &stats) != VTX_OK) {
        return;
    }

    fprintf(stderr, "[POOL] total=%zu free=%zu used=%zu peak=%zu "
            "allocs=%zu frees=%zu data_size=%zu\n",
            stats.total_frames, stats.free_frames, stats.used_frames,
            stats.peak_frames, stats.total_allocs, stats.total_frees,
            stats.data_size);
}
