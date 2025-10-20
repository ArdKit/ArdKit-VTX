/**
 * @file vtx_log.h
 * @brief VTX Logging Interface
 *
 * 日志格式：[LEVEL [ERR-CODE]] message
 * 日志级别：DEBUG(0) < INFO(1) < WARN(2) < ERROR(3) < FATAL(4)
 *
 * 实现：直接使用fprintf输出到stderr
 */

#ifndef VTX_LOG_H
#define VTX_LOG_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========== 日志级别定义 ========== */

typedef enum {
    VTX_LOG_DEBUG = 0,
    VTX_LOG_INFO  = 1,
    VTX_LOG_WARN  = 2,
    VTX_LOG_ERROR = 3,
    VTX_LOG_FATAL = 4,
} vtx_log_level_t;

/**
 * @brief DEBUG级别日志（不需要错误码）
 * @param fmt 格式字符串
 * @param ... 可变参数
 */
#ifdef VTX_DEBUG
#define vtx_log_debug(fmt, ...) \
    fprintf(stderr, "[DEBUG] " fmt "\n", ##__VA_ARGS__)
#else
#define vtx_log_debug(fmt, ...) ((void)0)
#endif

/**
 * @brief INFO级别日志（不需要错误码）
 * @param fmt 格式字符串
 * @param ... 可变参数
 */
#define vtx_log_info(fmt, ...) \
    fprintf(stderr, "[INFO] " fmt "\n", ##__VA_ARGS__)

/**
 * @brief WARN级别日志（简化版，无错误码）
 * @param fmt 格式字符串
 * @param ... 可变参数
 */
#define vtx_log_warn(fmt, ...) \
    fprintf(stderr, "[WARN] " fmt "\n", ##__VA_ARGS__)

/**
 * @brief ERROR级别日志（简化版，无错误码）
 * @param fmt 格式字符串
 * @param ... 可变参数
 */
#define vtx_log_error(fmt, ...) \
    fprintf(stderr, "[ERROR] " fmt "\n", ##__VA_ARGS__)

/**
 * @brief FATAL级别日志（简化版，无错误码）
 * @param fmt 格式字符串
 * @param ... 可变参数
 */
#define vtx_log_fatal(fmt, ...) \
    fprintf(stderr, "[FATAL] " fmt "\n", ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* VTX_LOG_H */
