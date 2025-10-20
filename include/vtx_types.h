/**
 * @file vtx_types.h
 * @brief VTX Type Definitions
 *
 * VTX协议的核心数据类型定义
 */

#ifndef VTX_TYPES_H
#define VTX_TYPES_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========== 帧类型定义 ========== */

/**
 * @brief 媒体帧类型
 */
typedef enum {
    VTX_FRAME_I   = 1,  /* I帧（关键帧）- 需要重传保护 */
    VTX_FRAME_P   = 2,  /* P帧（预测帧）- 丢失不重传 */
    VTX_FRAME_SPS = 3,  /* SPS（序列参数集）- 需要重传保护 */
    VTX_FRAME_PPS = 4,  /* PPS（图像参数集）- 需要重传保护 */
    VTX_FRAME_A   = 5,  /* 音频帧 - 丢失不重传 */
} vtx_frame_type_t;

/**
 * @brief 数据帧类型（控制与用户数据）
 */
typedef enum {
    VTX_DATA_CONNECT    = 0x10,  /* 连接请求 */
    VTX_DATA_DISCONNECT = 0x11,  /* 断开连接 */
    VTX_DATA_ACK        = 0x12,  /* 确认应答 */
    VTX_DATA_HEARTBEAT  = 0x13,  /* 心跳包 */
    VTX_DATA_USER       = 0x14,  /* 用户数据（可靠传输） */
    VTX_DATA_START      = 0x15,  /* 开始媒体传输 */
    VTX_DATA_STOP       = 0x16,  /* 停止媒体传输 */
} vtx_data_type_t;

/**
 * @brief 包标志位
 */
typedef enum {
    VTX_FLAG_LAST_FRAG  = (1 << 0),  /* 最后一个分片 */
    VTX_FLAG_RETRANS    = (1 << 1),  /* 重传标记 */
} vtx_packet_flags_t;

/* ========== 数据包结构 ========== */

/* 包头大小常量（先定义，后续结构体使用） */
#ifdef VTX_DEBUG
#define VTX_PACKET_HEADER_SIZE 22
#else
#define VTX_PACKET_HEADER_SIZE 14
#endif

#define VTX_PACKET_HEADER_MAX_SIZE 22

/**
 * @brief VTX数据包头部
 *
 * Release模式: 14字节
 * Debug模式:   22字节 (增加8字节时间戳)
 */
typedef struct {
    uint32_t seq_num;        /* 序列号（全局递增，用于检测丢包） */
    uint16_t frame_id;       /* 帧ID（同一帧的所有分片共享） */
    uint8_t  frame_type;     /* 帧类型: vtx_frame_type_t 或 vtx_data_type_t */
    uint8_t  flags;          /* 标志位: vtx_packet_flags_t */
    uint16_t frag_index;     /* 分片索引（从0开始） */
    uint16_t total_frags;    /* 总分片数 */
    uint16_t payload_size;   /* 本分片载荷大小 */
    uint16_t checksum;       /* CRC16校验 */
#ifdef VTX_DEBUG
    uint64_t timestamp_ms;   /* 发送时间戳（仅DEBUG模式，用于延迟测量） */
#endif
} vtx_packet_header_t;

/* ========== 配置结构 ========== */

/**
 * @brief 发送端配置
 */
typedef struct {
    const char* bind_addr;    /* 绑定地址，NULL表示INADDR_ANY */
    uint16_t    bind_port;    /* 绑定端口 */
    uint16_t    mtu;          /* MTU大小，默认1400字节 */
    uint32_t    send_buf_size; /* 发送缓冲区大小 */
    uint32_t    retrans_timeout_ms; /* I帧分片重传超时（默认5ms） */
    uint8_t     max_retrans;  /* I帧分片最大重传次数（默认3次） */
    uint32_t    data_retrans_timeout_ms; /* DATA包重传超时（默认30ms） */
    uint8_t     data_max_retrans; /* DATA包最大重传次数（默认3次） */
#ifdef VTX_DEBUG
    float       drop_rate;    /* 丢包模拟率（0.0-1.0） */
#endif
} vtx_tx_config_t;

/**
 * @brief 接收端配置
 */
typedef struct {
    const char* server_addr;  /* 服务器地址 */
    uint16_t    server_port;  /* 服务器端口 */
    uint16_t    mtu;          /* MTU大小，默认1400字节 */
    uint32_t    recv_buf_size; /* 接收缓冲区大小 */
    uint32_t    frame_timeout_ms; /* 帧接收超时（默认100ms） */
} vtx_rx_config_t;

/* ========== 统计结构 ========== */

/**
 * @brief 发送端统计信息
 */
typedef struct {
    uint64_t total_frames;      /* 总发送帧数 */
    uint64_t total_i_frames;    /* I帧数量 */
    uint64_t total_p_frames;    /* P帧数量 */
    uint64_t total_packets;     /* 总发送包数 */
    uint64_t total_bytes;       /* 总发送字节数 */
    uint64_t retrans_packets;   /* 重传包数 */
    uint64_t retrans_bytes;     /* 重传字节数 */
    uint64_t dropped_frames;    /* 丢弃帧数（发送失败） */
    uint32_t current_bitrate;   /* 当前比特率（bps） */
    uint32_t avg_frame_size;    /* 平均帧大小（字节） */
    float    retrans_rate;      /* 重传率 */
} vtx_tx_stats_t;

/**
 * @brief 接收端统计信息
 */
typedef struct {
    uint64_t total_frames;      /* 总接收帧数 */
    uint64_t total_i_frames;    /* I帧数量 */
    uint64_t total_p_frames;    /* P帧数量 */
    uint64_t total_packets;     /* 总接收包数 */
    uint64_t total_bytes;       /* 总接收字节数 */
    uint64_t lost_packets;      /* 丢失包数 */
    uint64_t dup_packets;       /* 重复包数 */
    uint64_t incomplete_frames; /* 不完整帧数（超时丢弃） */
    uint32_t current_bitrate;   /* 当前比特率（bps） */
    uint32_t avg_frame_size;    /* 平均帧大小（字节） */
    float    loss_rate;         /* 丢包率 */
#ifdef VTX_DEBUG
    uint32_t avg_latency_ms;    /* 平均延迟（毫秒） */
    uint32_t max_latency_ms;    /* 最大延迟（毫秒） */
#endif
} vtx_rx_stats_t;

/**
 * @brief 接收帧回调（接收端使用）
 *
 * @param frame_data 帧数据（只读，回调返回后失效）
 * @param frame_size 帧大小
 * @param frame_type 帧类型
 * @param userdata 用户数据
 * @return 0成功，负数表示错误
 *
 * 注意：frame_data 指针在回调返回后失效，需要拷贝数据
 */
typedef int (*vtx_on_frame_fn)(
    const uint8_t* frame_data,
    size_t frame_size,
    vtx_frame_type_t frame_type,
    void* userdata);

/**
 * @brief 数据帧回调
 *
 * @param data_type 数据帧类型
 * @param data 数据帧数据（可能为NULL）
 * @param size 数据大小
 * @param userdata 用户数据
 * @return 0成功，负数表示错误
 */
typedef int (*vtx_on_data_fn)(
    vtx_data_type_t data_type,
    const uint8_t* data,
    size_t size,
    void* userdata);

/**
 * @brief 连接事件回调
 *
 * @param connected true表示已连接，false表示已断开
 * @param userdata 用户数据
 */
typedef void (*vtx_on_connect_fn)(
    bool connected,
    void* userdata);

/**
 * @brief 媒体控制回调（TX端使用）
 *
 * @param start true表示开始发送媒体，false表示停止发送媒体
 * @param userdata 用户数据
 */
typedef void (*vtx_on_media_fn)(
    bool start,
    void* userdata);

/* ========== 常量定义 ========== */

#define VTX_DEFAULT_MTU           1400
#define VTX_MAX_FRAME_SIZE        (512 * 1024)  /* 512KB */
#define VTX_DEFAULT_SEND_BUF      (2 * 1024 * 1024)  /* 2MB */
#define VTX_DEFAULT_RECV_BUF      (2 * 1024 * 1024)  /* 2MB */
#define VTX_DEFAULT_RETRANS_TIMEOUT_MS  5
#define VTX_DEFAULT_MAX_RETRANS   3
#define VTX_DEFAULT_DATA_RETRANS_TIMEOUT_MS 30
#define VTX_DEFAULT_FRAME_TIMEOUT_MS 100

#ifdef __cplusplus
}
#endif

#endif /* VTX_TYPES_H */
