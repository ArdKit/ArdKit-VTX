/**
 * @file vtx_error.c
 * @brief VTX Error Handling Implementation
 */

#include "vtx_error.h"
#include <stddef.h>

/* ========== 错误信息字符串表 ========== */

static const char* vtx_error_strings[] = {
    [0] = "Success",
    [-VTX_ERR_INVALID_PARAM] = "Invalid parameter",
    [-VTX_ERR_NO_MEMORY] = "Out of memory",
    [-VTX_ERR_IO_FAILED] = "IO operation failed",
    [-VTX_ERR_NOT_FOUND] = "Not found",
    [-VTX_ERR_NOT_SUPPORTED] = "Operation not supported",
    [-VTX_ERR_TIMEOUT] = "Operation timeout",
    [-VTX_ERR_BUSY] = "Resource busy",
    [-VTX_ERR_EXIST] = "Already exists",
    [-VTX_ERR_OVERFLOW] = "Buffer overflow",
    [-VTX_ERR_CORRUPTED] = "Data corrupted",
    [-VTX_ERR_UNINITIALIZED] = "Uninitialized",
    [-VTX_ERR_ALREADY_INIT] = "Already initialized",
    [-VTX_ERR_NOT_READY] = "Not ready",
    [-VTX_ERR_CHECKSUM] = "Checksum error",
    [-VTX_ERR_NETWORK] = "Network error",
    [-VTX_ERR_SOCKET_CREATE] = "Failed to create socket",
    [-VTX_ERR_SOCKET_BIND] = "Failed to bind socket",
    [-VTX_ERR_SOCKET_SEND] = "Failed to send data",
    [-VTX_ERR_SOCKET_RECV] = "Failed to receive data",
    [-VTX_ERR_ADDR_INVALID] = "Invalid address",
    [-VTX_ERR_PACKET_INVALID] = "Invalid packet",
    [-VTX_ERR_PACKET_TOO_LARGE] = "Packet too large",
    [-VTX_ERR_FRAME_INVALID] = "Invalid frame",
    [-VTX_ERR_FRAME_INCOMPLETE] = "Incomplete frame",
    [-VTX_ERR_SEQUENCE] = "Sequence error",
    [-VTX_ERR_CODEC_OPEN] = "Failed to open codec",
    [-VTX_ERR_CODEC_DECODE] = "Decode failed",
    [-VTX_ERR_CODEC_ENCODE] = "Encode failed",
    [-VTX_ERR_CODEC_PARAM] = "Codec parameter error",
    [-VTX_ERR_FORMAT_INVALID] = "Invalid format",
    [-VTX_ERR_FILE_OPEN] = "Failed to open file",
    [-VTX_ERR_FILE_READ] = "Failed to read file",
    [-VTX_ERR_FILE_WRITE] = "Failed to write file",
    [-VTX_ERR_FILE_EOF] = "End of file",
};

#define VTX_ERROR_COUNT (sizeof(vtx_error_strings) / sizeof(vtx_error_strings[0]))

/* ========== 公共API ========== */

const char* vtx_strerror(int err_code) {
    if (err_code == VTX_OK) {
        return vtx_error_strings[0];
    }

    if (err_code > 0) {
        /* 警告 */
        if (err_code == VTX_WARN_PARTIAL) {
            return "Partial success";
        } else if (err_code == VTX_WARN_RETRY) {
            return "Retry required";
        }
        return "Unknown warning";
    }

    /* 错误 */
    int idx = -err_code;
    if (idx > 0 && idx < (int)VTX_ERROR_COUNT && vtx_error_strings[idx]) {
        return vtx_error_strings[idx];
    }

    return "Unknown error";
}
