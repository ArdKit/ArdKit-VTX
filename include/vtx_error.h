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

/* ========== 通用错误 (-1 ~ -99) ========== */
#define VTX_ERR_INVALID_PARAM      -1   /* 无效参数 */
#define VTX_ERR_NO_MEMORY          -2   /* 内存不足 */
#define VTX_ERR_IO_FAILED          -3   /* IO操作失败 */
#define VTX_ERR_NOT_FOUND          -4   /* 未找到 */
#define VTX_ERR_NOT_SUPPORTED      -5   /* 不支持的操作 */
#define VTX_ERR_TIMEOUT            -6   /* 操作超时 */
#define VTX_ERR_BUSY               -7   /* 资源忙 */
#define VTX_ERR_EXIST              -8   /* 已存在 */
#define VTX_ERR_OVERFLOW           -10  /* 溢出 */
#define VTX_ERR_CORRUPTED          -12  /* 数据损坏 */
#define VTX_ERR_UNINITIALIZED      -14  /* 未初始化 */
#define VTX_ERR_ALREADY_INIT       -15  /* 已初始化 */
#define VTX_ERR_NOT_READY          -16  /* 未就绪 */
#define VTX_ERR_CHECKSUM           -20  /* 校验和错误 */

/* ========== 网络错误 (-100 ~ -199) ========== */
#define VTX_ERR_NETWORK            -100 /* 网络错误 */
#define VTX_ERR_SOCKET_CREATE      -101 /* 创建socket失败 */
#define VTX_ERR_SOCKET_BIND        -102 /* 绑定socket失败 */
#define VTX_ERR_SOCKET_SEND        -103 /* 发送失败 */
#define VTX_ERR_SOCKET_RECV        -104 /* 接收失败 */
#define VTX_ERR_ADDR_INVALID       -105 /* 地址无效 */

/* ========== 协议错误 (-200 ~ -299) ========== */
#define VTX_ERR_PACKET_INVALID     -200 /* 无效的数据包 */
#define VTX_ERR_PACKET_TOO_LARGE   -201 /* 数据包过大 */
#define VTX_ERR_FRAME_INVALID      -202 /* 无效的帧 */
#define VTX_ERR_FRAME_INCOMPLETE   -203 /* 帧不完整 */
#define VTX_ERR_SEQUENCE           -204 /* 序列号错误 */

/* ========== 编解码错误 (-300 ~ -399) ========== */
#define VTX_ERR_CODEC_OPEN         -300 /* 打开编解码器失败 */
#define VTX_ERR_CODEC_DECODE       -301 /* 解码失败 */
#define VTX_ERR_CODEC_ENCODE       -302 /* 编码失败 */
#define VTX_ERR_CODEC_PARAM        -303 /* 编解码器参数错误 */
#define VTX_ERR_FORMAT_INVALID     -304 /* 格式无效 */

/* ========== 文件错误 (-400 ~ -499) ========== */
#define VTX_ERR_FILE_OPEN          -400 /* 打开文件失败 */
#define VTX_ERR_FILE_READ          -401 /* 读取文件失败 */
#define VTX_ERR_FILE_WRITE         -402 /* 写入文件失败 */
#define VTX_ERR_FILE_EOF           -403 /* 文件结束 */

/* ========== 警告 (1 ~ 999) ========== */
#define VTX_WARN_PARTIAL           1    /* 部分成功 */
#define VTX_WARN_RETRY             3    /* 需要重试 */

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
    return err_code < 0;
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
