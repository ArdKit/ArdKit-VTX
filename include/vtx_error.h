/**
 * @file vtx_error.h
 * @brief VTX Error Code Definitions
 *
 * 错误码定义：
 * - 0: 成功
 * - 负数: 失败
 * - 正数: 警告
 */

#ifndef VTX_ERROR_H
#define VTX_ERROR_H

#ifdef __cplusplus
extern "C" {
#endif

/* ========== 成功 ========== */
#define VTX_OK                     0

/* ========== 通用错误 (0x8000 ~ 0x80FF) ========== */
#define VTX_ERR_INVALID_PARAM      0x8001   /* 无效参数 */
#define VTX_ERR_NO_MEMORY          0x8002   /* 内存不足 */
#define VTX_ERR_IO_FAILED          0x8003   /* IO操作失败 */
#define VTX_ERR_NOT_FOUND          0x8004   /* 未找到 */
#define VTX_ERR_NOT_SUPPORTED      0x8005   /* 不支持的操作 */
#define VTX_ERR_TIMEOUT            0x8006   /* 操作超时 */
#define VTX_ERR_BUSY               0x8007   /* 资源忙 */
#define VTX_ERR_EXIST              0x8008   /* 已存在 */
#define VTX_ERR_OVERFLOW           0x8009   /* 溢出 */
#define VTX_ERR_CORRUPTED          0x800A   /* 数据损坏 */
#define VTX_ERR_UNINITIALIZED      0x800B   /* 未初始化 */
#define VTX_ERR_ALREADY_INIT       0x800C   /* 已初始化 */
#define VTX_ERR_NOT_READY          0x800D   /* 未就绪 */
#define VTX_ERR_CHECKSUM           0x800E   /* 校验和错误 */
#define VTX_ERR_DISCONNECTED       0x800F   /* 连接已断开 */

/* ========== 网络错误 (0x8100 ~ 0x81FF) ========== */
#define VTX_ERR_NETWORK            0x8100   /* 网络错误 */
#define VTX_ERR_SOCKET_CREATE      0x8101   /* 创建socket失败 */
#define VTX_ERR_SOCKET_BIND        0x8102   /* 绑定socket失败 */
#define VTX_ERR_SOCKET_SEND        0x8103   /* 发送失败 */
#define VTX_ERR_SOCKET_RECV        0x8104   /* 接收失败 */
#define VTX_ERR_ADDR_INVALID       0x8105   /* 地址无效 */

/* ========== 协议错误 (0x8200 ~ 0x82FF) ========== */
#define VTX_ERR_PACKET_INVALID     0x8200   /* 无效的数据包 */
#define VTX_ERR_PACKET_TOO_LARGE   0x8201   /* 数据包过大 */
#define VTX_ERR_FRAME_INVALID      0x8202   /* 无效的帧 */
#define VTX_ERR_FRAME_INCOMPLETE   0x8203   /* 帧不完整 */
#define VTX_ERR_SEQUENCE           0x8204   /* 序列号错误 */

/* ========== 编解码错误 (0x8300 ~ 0x83FF) ========== */
#define VTX_ERR_CODEC_OPEN         0x8300   /* 打开编解码器失败 */
#define VTX_ERR_CODEC_DECODE       0x8301   /* 解码失败 */
#define VTX_ERR_CODEC_ENCODE       0x8302   /* 编码失败 */
#define VTX_ERR_CODEC_PARAM        0x8303   /* 编解码器参数错误 */
#define VTX_ERR_FORMAT_INVALID     0x8304   /* 格式无效 */

/* ========== 文件错误 (0x8400 ~ 0x84FF) ========== */
#define VTX_ERR_FILE_OPEN          0x8400   /* 打开文件失败 */
#define VTX_ERR_FILE_READ          0x8401   /* 读取文件失败 */
#define VTX_ERR_FILE_WRITE         0x8402   /* 写入文件失败 */
#define VTX_ERR_FILE_EOF           0x8403   /* 文件结束 */

/* ========== 警告 (1 ~ 999) ========== */
#define VTX_WARN_PARTIAL           1        /* 部分成功 */
#define VTX_WARN_RETRY             3        /* 需要重试 */

/**
 * @brief 获取错误码对应的错误信息
 * @param err_code 错误码
 * @return 错误信息字符串
 */
const char *vtx_strerror(int err_code);

/**
 * @brief 检查错误码是否表示成功
 * @param err_code 错误码
 * @return 1表示成功，0表示失败或警告
 */
static inline int vtx_is_ok(int err_code) {
    return err_code == VTX_OK;
}

/**
 * @brief 检查错误码是否表示失败
 * @param err_code 错误码
 * @return 1表示失败，0表示成功或警告
 */
static inline int vtx_is_error(int err_code) {
    return (err_code & 0x8000) != 0;
}

/**
 * @brief 检查错误码是否表示警告
 * @param err_code 错误码
 * @return 1表示警告，0表示成功或失败
 */
static inline int vtx_is_warning(int err_code) {
    return err_code > 0;
}

#ifdef __cplusplus
}
#endif

#endif /* VTX_ERROR_H */
