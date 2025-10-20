/**
 * @file vtx_error.c
 * @brief VTX Error Handling Implementation
 */

#include "vtx_error.h"
#include <stddef.h>

/* ========== 公共API ========== */

const char* vtx_strerror(int err_code) {
    if (err_code == VTX_OK) {
        return "Success";
    }

    /* 警告 */
    if (err_code > 0) {
        switch (err_code) {
        case VTX_WARN_PARTIAL:
            return "Partial success";
        case VTX_WARN_RETRY:
            return "Retry required";
        default:
            return "Unknown warning";
        }
    }

    /* 错误 */
    switch (err_code) {
    /* 通用错误 */
    case VTX_ERR_INVALID_PARAM:
        return "Invalid parameter";
    case VTX_ERR_NO_MEMORY:
        return "Out of memory";
    case VTX_ERR_IO_FAILED:
        return "IO operation failed";
    case VTX_ERR_NOT_FOUND:
        return "Not found";
    case VTX_ERR_NOT_SUPPORTED:
        return "Operation not supported";
    case VTX_ERR_TIMEOUT:
        return "Operation timeout";
    case VTX_ERR_BUSY:
        return "Resource busy";
    case VTX_ERR_EXIST:
        return "Already exists";
    case VTX_ERR_OVERFLOW:
        return "Buffer overflow";
    case VTX_ERR_CORRUPTED:
        return "Data corrupted";
    case VTX_ERR_UNINITIALIZED:
        return "Uninitialized";
    case VTX_ERR_ALREADY_INIT:
        return "Already initialized";
    case VTX_ERR_NOT_READY:
        return "Not ready";
    case VTX_ERR_CHECKSUM:
        return "Checksum error";
    case VTX_ERR_DISCONNECTED:
        return "Connection disconnected";

    /* 网络错误 */
    case VTX_ERR_NETWORK:
        return "Network error";
    case VTX_ERR_SOCKET_CREATE:
        return "Failed to create socket";
    case VTX_ERR_SOCKET_BIND:
        return "Failed to bind socket";
    case VTX_ERR_SOCKET_SEND:
        return "Failed to send data";
    case VTX_ERR_SOCKET_RECV:
        return "Failed to receive data";
    case VTX_ERR_ADDR_INVALID:
        return "Invalid address";

    /* 协议错误 */
    case VTX_ERR_PACKET_INVALID:
        return "Invalid packet";
    case VTX_ERR_PACKET_TOO_LARGE:
        return "Packet too large";
    case VTX_ERR_FRAME_INVALID:
        return "Invalid frame";
    case VTX_ERR_FRAME_INCOMPLETE:
        return "Incomplete frame";
    case VTX_ERR_SEQUENCE:
        return "Sequence error";

    /* 编解码错误 */
    case VTX_ERR_CODEC_OPEN:
        return "Failed to open codec";
    case VTX_ERR_CODEC_DECODE:
        return "Decode failed";
    case VTX_ERR_CODEC_ENCODE:
        return "Encode failed";
    case VTX_ERR_CODEC_PARAM:
        return "Codec parameter error";
    case VTX_ERR_FORMAT_INVALID:
        return "Invalid format";

    /* 文件错误 */
    case VTX_ERR_FILE_OPEN:
        return "Failed to open file";
    case VTX_ERR_FILE_READ:
        return "Failed to read file";
    case VTX_ERR_FILE_WRITE:
        return "Failed to write file";
    case VTX_ERR_FILE_EOF:
        return "End of file";

    default:
        return "Unknown error";
    }
}
