/**
 * @file vtx_mem.h
 * @brief VTX Memory Management Interface
 *
 * 内存管理特性：
 * - 分配的内存自动初始化为0
 * - 提供 vtx_malloc, vtx_free, vtx_realloc, vtx_strdup 等接口
 *
 * 调试模式（定义 MEM_DEBUG）：
 * - 内存块带头/尾MAGIC，用于检测越界
 * - 支持内存使用统计
 * - 支持配置总内存使用上限
 * - 释放时检测越界并打印调用栈
 *
 * 发布模式（未定义 MEM_DEBUG）：
 * - 直接使用 libc 的内存管理接口
 * - 无额外开销
 */

#ifndef VTX_MEM_H
#define VTX_MEM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 内存统计信息（仅 MEM_DEBUG 模式可用） */
typedef struct {
    uint64_t total_alloc;        /* 总分配次数 */
    uint64_t total_free;         /* 总释放次数 */
    uint64_t current_bytes;      /* 当前使用字节数 */
    uint64_t peak_bytes;         /* 峰值字节数 */
    uint64_t total_bytes;        /* 累计分配字节数 */
    uint64_t boundary_errors;    /* 越界错误次数 */
    uint64_t double_free_errors; /* 重复释放错误次数 */
} vtx_mem_stats_t;

/**
 * @brief 初始化内存管理模块
 * @param limit_bytes 内存使用上限（字节），0表示无限制（仅 MEM_DEBUG 有效）
 * @return 0成功，负数表示错误码
 *
 * 注意：非 MEM_DEBUG 模式下此函数为空操作，总是返回成功
 */
int vtx_mem_init(uint64_t limit_bytes);

/**
 * @brief 销毁内存管理模块
 *
 * 注意：MEM_DEBUG 模式下会检查内存泄漏
 */
void vtx_mem_fini(void);

/**
 * @brief 分配内存
 * @param size 分配大小（字节）
 * @return 内存指针，失败返回NULL
 *
 * 注意：分配的内存已初始化为0
 */
void *vtx_malloc(size_t size);

/**
 * @brief 分配并初始化内存为0
 * @param nmemb 元素个数
 * @param size 每个元素大小
 * @return 内存指针，失败返回NULL
 */
void *vtx_calloc(size_t nmemb, size_t size);

/**
 * @brief 重新分配内存
 * @param ptr 原内存指针
 * @param size 新大小（字节）
 * @return 新内存指针，失败返回NULL
 *
 * 注意：新增的内存已初始化为0
 */
void *vtx_realloc(void *ptr, size_t size);

/**
 * @brief 复制字符串
 * @param s 源字符串
 * @return 新字符串指针，失败返回NULL
 */
char *vtx_strdup(const char *s);

/**
 * @brief 复制指定长度的字符串
 * @param s 源字符串
 * @param n 最大长度
 * @return 新字符串指针，失败返回NULL
 */
char *vtx_strndup(const char *s, size_t n);

/**
 * @brief 释放内存
 * @param ptr 内存指针
 *
 * 注意（MEM_DEBUG 模式）：
 * - 检测到越界会打印错误日志和调用栈
 * - 检测重复释放并打印错误
 * - ptr 为 NULL 时安全返回
 */
void vtx_free(void *ptr);

/* ========== 调试接口（仅 MEM_DEBUG 模式可用） ========== */

#ifdef MEM_DEBUG

/**
 * @brief 获取内存统计信息
 * @param stats 统计信息输出结构
 * @return 0成功，负数表示错误码
 */
int vtx_mem_get_stats(vtx_mem_stats_t *stats);

/**
 * @brief 重置内存统计信息（不包括current_bytes）
 * @return 0成功，负数表示错误码
 */
int vtx_mem_reset_stats(void);

/**
 * @brief 设置内存使用上限
 * @param limit_bytes 内存使用上限（字节），0表示无限制
 * @return 0成功，负数表示错误码
 */
int vtx_mem_set_limit(uint64_t limit_bytes);

/**
 * @brief 获取内存使用上限
 * @return 内存使用上限（字节），0表示无限制
 */
uint64_t vtx_mem_get_limit(void);

/**
 * @brief 打印内存统计信息
 */
void vtx_mem_print_stats(void);

/**
 * @brief 检查是否有内存泄漏
 * @return 0表示无泄漏，正数表示泄漏的内存块数量
 */
int vtx_mem_check_leak(void);

/**
 * @brief 打印所有未释放的内存块信息（用于调试）
 */
void vtx_mem_dump_leaks(void);

#endif /* MEM_DEBUG */

#ifdef __cplusplus
}
#endif

#endif /* VTX_MEM_H */
