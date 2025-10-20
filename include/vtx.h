/**
 * @file vtx.h
 * @brief VTX (Video Transmission over eXtended UDP) API
 *
 * VTX是一个基于UDP的实时视频传输协议库，专为低延迟、高可靠性的
 * 视频传输场景设计。
 *
 * 核心特性：
 * - 基于UDP + 选择性重传ARQ + 自适应FEC
 * - I帧分片快速重传，P帧丢弃策略
 * - 零拷贝设计，高效内存管理
 * - 跨平台支持（Linux/macOS）
 *
 * @version 1.0.0
 * @date 2025-01-19
 */

#ifndef VTX_H
#define VTX_H

#include "vtx_types.h"
#include "vtx_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========== 不透明句柄类型 ========== */

typedef struct vtx_tx vtx_tx_t;  /* 发送端句柄 */
typedef struct vtx_rx vtx_rx_t;  /* 接收端句柄 */

/* 前向声明 */
struct vtx_frame;

/* ========== 初始化配置 ========== */

/**
 * @brief VTX初始化配置
 */
typedef struct {
    uint64_t mem_limit_bytes;  /* 内存使用上限（字节），0表示无限制 */
} vtx_init_config_t;

/**
 * @brief 版本信息结构
 */
typedef struct {
    int major;  /* 主版本号 */
    int minor;  /* 次版本号 */
    int build;  /* 构建版本号 */
} vtx_version_t;

/* ========== 初始化/销毁 ========== */

/**
 * @brief 初始化VTX库
 * @param config 初始化配置（可为NULL使用默认值）
 * @return 0成功，负数表示错误码
 *
 * 注意：
 * - 使用VTX库前必须调用此函数
 * - 重复调用返回VTX_ERR_ALREADY_INIT
 * - config为NULL时使用默认配置（无内存限制）
 */
int vtx_init(const vtx_init_config_t* config);

/**
 * @brief 销毁VTX库
 *
 * 注意：
 * - 释放所有全局资源
 * - 调用后不能再使用VTX API
 * - 应在所有TX/RX对象销毁后调用
 */
void vtx_fini(void);

/**
 * @brief 检查VTX库是否已初始化
 * @return 1表示已初始化，0表示未初始化
 */
int vtx_is_initialized(void);

/* ========== 发送端API ========== */

/**
 * @brief 创建发送端
 *
 * @param config 配置参数（不可为NULL）
 * @param data_fn 数据帧回调（可为NULL）
 * @param media_fn 媒体控制回调（可为NULL，接收START/STOP通知）
 * @param userdata 用户数据（传递给回调）
 * @return vtx_tx_t* 成功返回发送端对象，失败返回NULL
 *
 * 注意：
 * - 发送端创建后处于监听状态，需调用 vtx_tx_accept() 接受连接
 * - media_fn用于接收客户端的START/STOP控制指令
 */
vtx_tx_t* vtx_tx_create(
    const vtx_tx_config_t* config,
    vtx_on_data_fn data_fn,
    vtx_on_media_fn media_fn,
    void* userdata);

/**
 * @brief 启动监听
 *
 * @param tx 发送端对象
 * @return 0成功，负数表示错误码
 *
 * 注意：调用后发送端开始监听指定端口，等待接收端连接
 */
int vtx_tx_listen(vtx_tx_t* tx);

/**
 * @brief 接受连接（阻塞）
 *
 * @param tx 发送端对象
 * @param timeout_ms 超时时间（毫秒），0表示永久等待
 * @return 0成功，VTX_ERR_TIMEOUT表示超时，其他负数表示错误码
 *
 * 注意：
 * - 此函数阻塞直到有接收端连接或超时
 * - 连接建立后，自动启动发送线程
 */
int vtx_tx_accept(vtx_tx_t* tx, uint32_t timeout_ms);

/**
 * @brief 轮询事件（非阻塞）
 *
 * @param tx 发送端对象
 * @param timeout_ms 超时时间（毫秒），0表示立即返回
 * @return 1有事件，0超时/无事件，负数表示错误码
 *
 * 注意：用于阻塞模式下检查连接状态、接收控制帧等
 */
int vtx_tx_poll(vtx_tx_t* tx, uint32_t timeout_ms);

/**
 * @brief 发送数据（可靠传输）
 *
 * @param tx 发送端对象
 * @param data 数据缓冲区
 * @param size 数据大小
 * @return 0成功，负数表示错误码
 *
 * 注意：
 * - 此函数用于发送用户数据帧（可靠传输）
 * - 数据将被自动重传直到确认或达到最大重传次数
 * - 内部会自动分配frame，复制数据
 */
int vtx_tx_send(vtx_tx_t* tx, const uint8_t* data, size_t size);

/**
 * @brief 分配媒体帧（用于发送媒体数据）
 *
 * @param tx 发送端对象
 * @return vtx_frame_t* 成功返回frame对象，失败返回NULL
 *
 * 注意：
 * - 从media帧池分配frame（容量512KB）
 * - 使用完毕后必须调用 vtx_tx_free_frame() 释放
 * - frame的data缓冲区已分配好，可直接写入数据
 */
struct vtx_frame* vtx_tx_alloc_media_frame(vtx_tx_t* tx);

/**
 * @brief 释放frame
 *
 * @param tx 发送端对象
 * @param frame frame对象
 *
 * 注意：
 * - 归还frame到对应的内存池
 * - frame指针在调用后失效
 */
void vtx_tx_free_frame(vtx_tx_t* tx, struct vtx_frame* frame);

/**
 * @brief 发送媒体帧
 *
 * @param tx 发送端对象
 * @param frame 媒体帧（必须从 vtx_tx_alloc_media_frame() 分配）
 * @return 0成功，负数表示错误码
 *
 * 注意：
 * - frame必须已填充好数据（frame->data和frame->data_size）
 * - frame->frame_type必须设置（VTX_FRAME_I/P/SPS/PPS/A）
 * - 发送后frame会被TX持有，应用层不应再访问
 * - TX会在适当时机自动释放frame
 */
int vtx_tx_send_media(vtx_tx_t* tx, struct vtx_frame* frame);

/**
 * @brief 关闭连接
 *
 * @param tx 发送端对象
 * @return 0成功，负数表示错误码
 */
int vtx_tx_close(vtx_tx_t* tx);

/**
 * @brief 获取统计信息
 *
 * @param tx 发送端对象
 * @param stats 统计信息输出结构
 * @return 0成功，负数表示错误码
 */
int vtx_tx_get_stats(vtx_tx_t* tx, vtx_tx_stats_t* stats);

/**
 * @brief 销毁发送端
 *
 * @param tx 发送端对象
 *
 * 注意：
 * - 自动关闭连接（如果未关闭）
 * - 释放所有资源
 * - tx指针在调用后失效
 */
void vtx_tx_destroy(vtx_tx_t* tx);

/* ========== 接收端API ========== */

/**
 * @brief 创建接收端
 *
 * @param config 配置参数（不可为NULL）
 * @param frame_fn 接收帧回调（不可为NULL）
 * @param data_fn 数据帧回调（可为NULL）
 * @param connect_fn 连接事件回调（可为NULL）
 * @param userdata 用户数据（传递给回调）
 * @return vtx_rx_t* 成功返回接收端对象，失败返回NULL
 *
 * 注意：
 * - frame_fn 在独立线程中被调用
 * - frame_data 指针在回调返回后失效，需要拷贝数据
 */
vtx_rx_t* vtx_rx_create(
    const vtx_rx_config_t* config,
    vtx_on_frame_fn frame_fn,
    vtx_on_data_fn data_fn,
    vtx_on_connect_fn connect_fn,
    void* userdata);

/**
 * @brief 连接到发送端
 *
 * @param rx 接收端对象
 * @return 0成功，负数表示错误码
 *
 * 注意：
 * - 发送连接请求到服务器
 * - 启动接收线程和发送线程
 */
int vtx_rx_connect(vtx_rx_t* rx);

/**
 * @brief 轮询事件（非阻塞）
 *
 * @param rx 接收端对象
 * @param timeout_ms 超时时间（毫秒），0表示立即返回
 * @return 1有事件，0超时/无事件，负数表示错误码
 *
 * 注意：用于非阻塞模式下检查连接状态等
 */
int vtx_rx_poll(vtx_rx_t* rx, uint32_t timeout_ms);

/**
 * @brief 发送数据（可靠传输）
 *
 * @param rx 接收端对象
 * @param data 数据缓冲区
 * @param size 数据大小
 * @return 0成功，负数表示错误码
 *
 * 注意：
 * - 此函数用于发送非媒体流数据（如控制指令）
 * - 数据将被可靠传输（自动重传直到确认或达到最大重传次数）
 */
int vtx_rx_send(vtx_rx_t* rx, const uint8_t* data, size_t size);

/**
 * @brief 请求开始媒体传输
 *
 * @param rx 接收端对象
 * @return 0成功，负数表示错误码
 *
 * 注意：
 * - 发送START控制帧到服务器
 * - 服务器收到后通过media_fn回调通知应用层开始发送媒体数据
 */
int vtx_rx_start(vtx_rx_t* rx);

/**
 * @brief 请求停止媒体传输
 *
 * @param rx 接收端对象
 * @return 0成功，负数表示错误码
 *
 * 注意：
 * - 发送STOP控制帧到服务器
 * - 服务器收到后通过media_fn回调通知应用层停止发送媒体数据
 */
int vtx_rx_stop(vtx_rx_t* rx);

/**
 * @brief 关闭连接
 *
 * @param rx 接收端对象
 * @return 0成功，负数表示错误码
 */
int vtx_rx_close(vtx_rx_t* rx);

/**
 * @brief 获取统计信息
 *
 * @param rx 接收端对象
 * @param stats 统计信息输出结构
 * @return 0成功，负数表示错误码
 */
int vtx_rx_get_stats(vtx_rx_t* rx, vtx_rx_stats_t* stats);

/**
 * @brief 销毁接收端
 *
 * @param rx 接收端对象
 *
 * 注意：
 * - 自动关闭连接（如果未关闭）
 * - 释放所有资源
 * - rx指针在调用后失效
 */
void vtx_rx_destroy(vtx_rx_t* rx);

/* ========== 工具函数 ========== */

/**
 * @brief 获取版本字符串
 *
 * @return 版本字符串（如 "2.0.0"）
 */
const char* vtx_version(void);

/**
 * @brief 获取版本信息
 *
 * @param version 版本信息输出结构
 */
void vtx_version_info(vtx_version_t* version);

/**
 * @brief 获取构建信息
 *
 * @return 构建信息字符串（包含日期、编译器等）
 */
const char* vtx_build_info(void);

#ifdef __cplusplus
}
#endif

#endif /* VTX_H */
