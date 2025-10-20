/**
 * @file vtx_frame.h
 * @brief VTX Frame & Memory Pool Management
 *
 * 帧与内存池统一管理模块，负责：
 * - 帧缓冲区管理（包含数据）
 * - 内存池化管理（引用计数）
 * - 分片接收与重组
 *
 * 设计理念：
 * - 零拷贝：frame直接包含数据缓冲区指针
 * - 池化管理：预分配frame对象，避免频繁分配
 * - 原子引用计数：支持无锁多线程访问
 * - 两种池：媒体帧池（512KB data）和控制帧池（128B data）
 * - I帧缓存：直接在tx/rx结构中保留最后一帧指针，无需独立结构
 */

#ifndef VTX_FRAME_H
#define VTX_FRAME_H

#include "vtx_types.h"
#include "vtx_spinlock.h"
#include "list.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdatomic.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========== 常量定义 ========== */

#define VTX_CTRL_FRAME_DATA_SIZE  128          /* 控制帧数据大小 */
#define VTX_MEDIA_FRAME_DATA_SIZE VTX_MAX_FRAME_SIZE  /* 媒体帧数据大小（512KB） */
#define VTX_FRAME_POOL_INIT_SIZE  2            /* 初始frame数量 */

/* ========== 帧状态 ========== */

/**
 * @brief 帧状态
 */
typedef enum {
    VTX_FRAME_STATE_FREE,       /* 空闲（在池中） */
    VTX_FRAME_STATE_RECEIVING,  /* 正在接收 */
    VTX_FRAME_STATE_COMPLETE,   /* 接收完成 */
    VTX_FRAME_STATE_SENDING,    /* 正在发送/等待确认 */
} vtx_frame_state_t;

/* ========== 帧结构（统一frame和pkg） ========== */

/**
 * @brief 帧缓冲区（同时也是内存池管理单元）
 *
 * 设计说明：
 * - frame既是帧结构，也是数据缓冲区
 * - 使用list.h双向链表管理（池管理/队列管理）
 * - 原子引用计数，支持多线程共享
 * - data指针指向动态分配的数据缓冲区
 */
typedef struct vtx_frame {
    struct list_head list;           /* 链表节点（池管理/队列管理） */
    atomic_int       refcount;       /* 原子引用计数 */
    vtx_frame_state_t state;         /* 帧状态 */

    /* 帧信息 */
    uint16_t         frame_id;       /* 帧ID */
    vtx_frame_type_t frame_type;     /* 帧类型 */
    uint16_t         total_frags;    /* 总分片数 */
    uint16_t         recv_frags;     /* 已接收分片数 */
    size_t           data_size;      /* 实际数据大小 */
    size_t           data_capacity;  /* 数据缓冲区容量 */

    /* 分片跟踪 */
    uint8_t*         bitmap;         /* 分片接收位图（动态分配） */

    /* 时间戳 */
    uint64_t         first_recv_ms;  /* 首次接收时间 */
    uint64_t         last_recv_ms;   /* 最后接收时间 */
    uint64_t         send_time_ms;   /* 发送时间（用于重传超时） */
    uint8_t          retrans_count;  /* 重传次数 */

    /* 帧数据（动态分配） */
    uint8_t*         data;           /* 帧数据缓冲区指针 */
} vtx_frame_t;

/* ========== 内存池（frame池） ========== */

/**
 * @brief 帧内存池（不透明类型）
 *
 * 注意：
 * - 使用自旋锁保护空闲链表
 * - 支持动态扩展（按需分配新frame）
 * - 完整定义在vtx_frame.c中
 */
typedef struct vtx_frame_pool vtx_frame_pool_t;

/**
 * @brief 创建帧内存池
 *
 * @param initial_size 初始frame数量
 * @param data_size 每个frame的数据缓冲区大小
 * @return vtx_frame_pool_t* 成功返回内存池对象，失败返回NULL
 *
 * 注意：
 * - 预分配initial_size个frame，每个frame的data容量为data_size
 * - 内存池可按需动态扩展
 * - 建议：媒体帧池使用VTX_MEDIA_FRAME_DATA_SIZE（512KB）
 *         控制帧池使用VTX_CTRL_FRAME_DATA_SIZE（128B）
 */
vtx_frame_pool_t* vtx_frame_pool_create(size_t initial_size, size_t data_size);

/**
 * @brief 销毁帧内存池
 *
 * @param pool 内存池对象
 *
 * 注意：
 * - 释放所有frame（包括正在使用的）
 * - 如果有frame仍被引用，会打印警告（DEBUG模式）
 */
void vtx_frame_pool_destroy(vtx_frame_pool_t* pool);

/**
 * @brief 从池中获取frame
 *
 * @param pool 内存池对象
 * @return vtx_frame_t* 成功返回frame，失败返回NULL
 *
 * 注意：
 * - 如果池为空，自动分配新frame
 * - 返回的frame引用计数为1
 * - 使用完毕后需调用vtx_frame_release释放
 */
vtx_frame_t* vtx_frame_pool_acquire(vtx_frame_pool_t* pool);

/**
 * @brief 归还frame到池中
 *
 * @param pool 内存池对象
 * @param frame frame对象
 *
 * 注意：
 * - 仅当引用计数为0时才真正归还到池中
 * - 不应直接调用此函数，应使用vtx_frame_release
 */
void vtx_frame_pool_release(vtx_frame_pool_t* pool, vtx_frame_t* frame);

/* ========== frame引用计数 ========== */

/**
 * @brief 增加frame引用计数
 *
 * @param frame frame对象
 * @return vtx_frame_t* 返回frame本身（方便链式调用）
 *
 * 注意：
 * - 原子操作，线程安全
 * - 每次引用frame都应调用此函数
 */
vtx_frame_t* vtx_frame_retain(vtx_frame_t* frame);

/**
 * @brief 减少frame引用计数
 *
 * @param pool 内存池对象（可为NULL）
 * @param frame frame对象
 *
 * 注意：
 * - 原子操作，线程安全
 * - 当引用计数降为0时，自动归还到池中（如果pool非NULL）
 * - 如果pool为NULL，引用计数降为0时直接释放内存
 */
void vtx_frame_release(vtx_frame_pool_t* pool, vtx_frame_t* frame);

/**
 * @brief 获取frame引用计数
 *
 * @param frame frame对象
 * @return 当前引用计数
 *
 * 注意：仅用于调试，不应依赖此值做逻辑判断
 */
int vtx_frame_get_refcount(vtx_frame_t* frame);

/* ========== frame数据访问 ========== */

/**
 * @brief 获取frame数据指针
 *
 * @param frame frame对象
 * @return 数据指针
 */
static inline uint8_t* vtx_frame_data(vtx_frame_t* frame) {
    return frame->data;
}

/**
 * @brief 获取frame数据大小
 *
 * @param frame frame对象
 * @return 数据大小（字节）
 */
static inline size_t vtx_frame_size(vtx_frame_t* frame) {
    return frame->data_size;
}

/**
 * @brief 设置frame数据大小
 *
 * @param frame frame对象
 * @param size 数据大小（字节）
 *
 * 注意：size不应超过frame->data_capacity
 */
static inline void vtx_frame_set_size(vtx_frame_t* frame, size_t size) {
    if (size <= frame->data_capacity) {
        frame->data_size = size;
    }
}

/**
 * @brief 获取frame数据容量
 *
 * @param frame frame对象
 * @return 数据容量（字节）
 */
static inline size_t vtx_frame_capacity(vtx_frame_t* frame) {
    return frame->data_capacity;
}

/**
 * @brief 从frame中复制数据到缓冲区
 *
 * @param frame frame对象
 * @param offset frame中的起始偏移量（字节）
 * @param dst 目标缓冲区
 * @param size 要复制的大小（字节）
 * @return 实际复制的字节数，失败返回0
 *
 * 注意：
 * - 检查边界：offset + size 不能超过 frame->data_size
 * - 如果超出边界，返回0且不复制任何数据
 * - 参数检查：frame、dst 不能为NULL，size > 0
 */
size_t vtx_frame_copyfrom(
    const vtx_frame_t* frame,
    size_t offset,
    uint8_t* dst,
    size_t size);

/**
 * @brief 复制数据到frame中
 *
 * @param frame frame对象
 * @param offset frame中的起始偏移量（字节）
 * @param src 源数据缓冲区
 * @param size 要复制的大小（字节）
 * @return 实际复制的字节数，失败返回0
 *
 * 注意：
 * - 检查边界：offset + size 不能超过 frame->data_capacity
 * - 如果超出边界，返回0且不复制任何数据
 * - 自动更新 frame->data_size：设置为 max(data_size, offset+size)
 * - 参数检查：frame、src 不能为NULL，size > 0
 */
size_t vtx_frame_copyto(
    vtx_frame_t* frame,
    size_t offset,
    const uint8_t* src,
    size_t size);

/* ========== frame分片管理 ========== */

/**
 * @brief 初始化frame用于接收
 *
 * @param frame frame对象
 * @param frame_id 帧ID
 * @param frame_type 帧类型
 * @param total_frags 总分片数
 * @return 0成功，负数表示错误码
 *
 * 注意：
 * - 自动分配bitmap
 * - 重置recv_frags和data_size
 * - 设置状态为RECEIVING
 */
int vtx_frame_init_recv(
    vtx_frame_t* frame,
    uint16_t frame_id,
    vtx_frame_type_t frame_type,
    uint16_t total_frags);

/**
 * @brief 检查frame是否完整
 *
 * @param frame frame对象
 * @return true完整，false不完整
 */
bool vtx_frame_is_complete(const vtx_frame_t* frame);

/**
 * @brief 检查分片是否已接收
 *
 * @param frame frame对象
 * @param frag_index 分片索引
 * @return true已接收，false未接收
 */
bool vtx_frame_has_frag(const vtx_frame_t* frame, uint16_t frag_index);

/**
 * @brief 获取丢失的分片索引列表
 *
 * @param frame frame对象
 * @param missing 输出：丢失分片索引数组（由调用者分配）
 * @param max_missing 数组最大容量
 * @return 丢失分片数量
 *
 * 注意：
 * - missing数组应至少为total_frags大小
 * - 返回值可能大于max_missing，此时数组仅存储前max_missing个
 */
size_t vtx_frame_get_missing_frags(
    const vtx_frame_t* frame,
    uint16_t* missing,
    size_t max_missing);

/**
 * @brief 标记分片已接收
 *
 * @param frame frame对象
 * @param frag_index 分片索引
 * @return 0成功，负数表示错误码
 *
 * 注意：
 * - 更新bitmap
 * - 增加recv_frags计数
 * - 检查是否接收完整
 */
int vtx_frame_mark_frag_received(vtx_frame_t* frame, uint16_t frag_index);

/**
 * @brief 重置frame状态
 *
 * @param frame frame对象
 *
 * 注意：
 * - 释放bitmap
 * - 重置所有字段为初始状态
 * - 通常在归还到池中时调用
 */
void vtx_frame_reset(vtx_frame_t* frame);

/* ========== 帧队列管理 ========== */

/**
 * @brief 帧队列
 *
 * 注意：
 * - 维护帧列表（可用于接收队列、发送队列、重传队列等）
 * - 自动处理帧超时和丢弃
 * - 使用自旋锁保护，线程安全
 */
typedef struct {
    struct list_head   frames;       /* 帧链表 */
    size_t             count;        /* 帧数量 */
    vtx_frame_pool_t*  pool;         /* 内存池 */
    uint32_t           timeout_ms;   /* 帧超时时间（0表示无超时） */
    vtx_spinlock_t     lock;         /* 自旋锁 */
} vtx_frame_queue_t;

/**
 * @brief 创建帧队列
 *
 * @param pool 内存池
 * @param timeout_ms 帧超时时间（0表示无超时）
 * @return vtx_frame_queue_t* 成功返回帧队列，失败返回NULL
 */
vtx_frame_queue_t* vtx_frame_queue_create(
    vtx_frame_pool_t* pool,
    uint32_t timeout_ms);

/**
 * @brief 销毁帧队列
 *
 * @param queue 帧队列
 */
void vtx_frame_queue_destroy(vtx_frame_queue_t* queue);

/**
 * @brief 添加frame到队列尾部
 *
 * @param queue 帧队列
 * @param frame frame对象
 *
 * 注意：
 * - frame的引用计数会增加
 * - 线程安全
 */
void vtx_frame_queue_push(vtx_frame_queue_t* queue, vtx_frame_t* frame);

/**
 * @brief 从队列头部取出frame
 *
 * @param queue 帧队列
 * @return vtx_frame_t* 成功返回frame，队列为空返回NULL
 *
 * 注意：
 * - frame的引用计数不变
 * - 调用者负责释放frame
 * - 线程安全
 */
vtx_frame_t* vtx_frame_queue_pop(vtx_frame_queue_t* queue);

/**
 * @brief 根据frame_id查找frame
 *
 * @param queue 帧队列
 * @param frame_id 帧ID
 * @return vtx_frame_t* 成功返回frame，未找到返回NULL
 *
 * 注意：
 * - frame的引用计数不变
 * - frame仍在队列中
 * - 线程安全
 */
vtx_frame_t* vtx_frame_queue_find(
    vtx_frame_queue_t* queue,
    uint16_t frame_id);

/**
 * @brief 从队列中移除frame
 *
 * @param queue 帧队列
 * @param frame frame对象
 *
 * 注意：
 * - 从链表中移除，但不释放frame
 * - 调用者负责释放frame（vtx_frame_release）
 * - 线程安全
 */
void vtx_frame_queue_remove(vtx_frame_queue_t* queue, vtx_frame_t* frame);

/**
 * @brief 检查并清理超时帧
 *
 * @param queue 帧队列
 * @param now_ms 当前时间戳（毫秒）
 * @return 清理的帧数量
 *
 * 注意：
 * - 自动释放超时的frame
 * - 线程安全
 */
size_t vtx_frame_queue_cleanup_timeout(
    vtx_frame_queue_t* queue,
    uint64_t now_ms);

/**
 * @brief 获取队列中frame数量
 *
 * @param queue 帧队列
 * @return frame数量
 */
static inline size_t vtx_frame_queue_count(vtx_frame_queue_t* queue) {
    return queue->count;
}

/**
 * @brief 检查队列是否为空
 *
 * @param queue 帧队列
 * @return true为空，false不为空
 */
static inline bool vtx_frame_queue_empty(vtx_frame_queue_t* queue) {
    return queue->count == 0;
}

/* ========== 内存池统计 ========== */

/**
 * @brief 内存池统计信息
 */
typedef struct {
    size_t total_frames;     /* 总frame数量 */
    size_t free_frames;      /* 空闲frame数量 */
    size_t used_frames;      /* 使用中frame数量 */
    size_t peak_frames;      /* 峰值frame数量 */
    size_t total_allocs;     /* 总分配次数 */
    size_t total_frees;      /* 总释放次数 */
    size_t data_size;        /* 每个frame的data大小 */
} vtx_frame_pool_stats_t;

/**
 * @brief 获取内存池统计信息
 *
 * @param pool 内存池对象
 * @param stats 统计信息输出结构
 * @return 0成功，负数表示错误码
 */
int vtx_frame_pool_get_stats(
    vtx_frame_pool_t* pool,
    vtx_frame_pool_stats_t* stats);

/**
 * @brief 打印内存池统计信息
 *
 * @param pool 内存池对象
 */
void vtx_frame_pool_print_stats(vtx_frame_pool_t* pool);

#ifdef __cplusplus
}
#endif

#endif /* VTX_FRAME_H */
