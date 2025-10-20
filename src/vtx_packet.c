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
 * @file vtx_packet.c
 * @brief VTX Packet Processing Implementation
 */

#include "vtx_packet.h"
#include "vtx_error.h"
#include "vtx_log.h"
#include <string.h>
#include <arpa/inet.h>

#ifdef __APPLE__
#include <libkern/OSByteOrder.h>
#define htobe64(x) OSSwapHostToBigInt64(x)
#define be64toh(x) OSSwapBigToHostInt64(x)
#elif defined(__linux__)
#include <endian.h>
#endif

/* ========== CRC16校验 ========== */

/* CRC-16-CCITT 多项式: 0x1021 */
static const uint16_t crc16_table[256] = {
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50a5, 0x60c6, 0x70e7,
    0x8108, 0x9129, 0xa14a, 0xb16b, 0xc18c, 0xd1ad, 0xe1ce, 0xf1ef,
    0x1231, 0x0210, 0x3273, 0x2252, 0x52b5, 0x4294, 0x72f7, 0x62d6,
    0x9339, 0x8318, 0xb37b, 0xa35a, 0xd3bd, 0xc39c, 0xf3ff, 0xe3de,
    0x2462, 0x3443, 0x0420, 0x1401, 0x64e6, 0x74c7, 0x44a4, 0x5485,
    0xa56a, 0xb54b, 0x8528, 0x9509, 0xe5ee, 0xf5cf, 0xc5ac, 0xd58d,
    0x3653, 0x2672, 0x1611, 0x0630, 0x76d7, 0x66f6, 0x5695, 0x46b4,
    0xb75b, 0xa77a, 0x9719, 0x8738, 0xf7df, 0xe7fe, 0xd79d, 0xc7bc,
    0x48c4, 0x58e5, 0x6886, 0x78a7, 0x0840, 0x1861, 0x2802, 0x3823,
    0xc9cc, 0xd9ed, 0xe98e, 0xf9af, 0x8948, 0x9969, 0xa90a, 0xb92b,
    0x5af5, 0x4ad4, 0x7ab7, 0x6a96, 0x1a71, 0x0a50, 0x3a33, 0x2a12,
    0xdbfd, 0xcbdc, 0xfbbf, 0xeb9e, 0x9b79, 0x8b58, 0xbb3b, 0xab1a,
    0x6ca6, 0x7c87, 0x4ce4, 0x5cc5, 0x2c22, 0x3c03, 0x0c60, 0x1c41,
    0xedae, 0xfd8f, 0xcdec, 0xddcd, 0xad2a, 0xbd0b, 0x8d68, 0x9d49,
    0x7e97, 0x6eb6, 0x5ed5, 0x4ef4, 0x3e13, 0x2e32, 0x1e51, 0x0e70,
    0xff9f, 0xefbe, 0xdfdd, 0xcffc, 0xbf1b, 0xaf3a, 0x9f59, 0x8f78,
    0x9188, 0x81a9, 0xb1ca, 0xa1eb, 0xd10c, 0xc12d, 0xf14e, 0xe16f,
    0x1080, 0x00a1, 0x30c2, 0x20e3, 0x5004, 0x4025, 0x7046, 0x6067,
    0x83b9, 0x9398, 0xa3fb, 0xb3da, 0xc33d, 0xd31c, 0xe37f, 0xf35e,
    0x02b1, 0x1290, 0x22f3, 0x32d2, 0x4235, 0x5214, 0x6277, 0x7256,
    0xb5ea, 0xa5cb, 0x95a8, 0x8589, 0xf56e, 0xe54f, 0xd52c, 0xc50d,
    0x34e2, 0x24c3, 0x14a0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
    0xa7db, 0xb7fa, 0x8799, 0x97b8, 0xe75f, 0xf77e, 0xc71d, 0xd73c,
    0x26d3, 0x36f2, 0x0691, 0x16b0, 0x6657, 0x7676, 0x4615, 0x5634,
    0xd94c, 0xc96d, 0xf90e, 0xe92f, 0x99c8, 0x89e9, 0xb98a, 0xa9ab,
    0x5844, 0x4865, 0x7806, 0x6827, 0x18c0, 0x08e1, 0x3882, 0x28a3,
    0xcb7d, 0xdb5c, 0xeb3f, 0xfb1e, 0x8bf9, 0x9bd8, 0xabbb, 0xbb9a,
    0x4a75, 0x5a54, 0x6a37, 0x7a16, 0x0af1, 0x1ad0, 0x2ab3, 0x3a92,
    0xfd2e, 0xed0f, 0xdd6c, 0xcd4d, 0xbdaa, 0xad8b, 0x9de8, 0x8dc9,
    0x7c26, 0x6c07, 0x5c64, 0x4c45, 0x3ca2, 0x2c83, 0x1ce0, 0x0cc1,
    0xef1f, 0xff3e, 0xcf5d, 0xdf7c, 0xaf9b, 0xbfba, 0x8fd9, 0x9ff8,
    0x6e17, 0x7e36, 0x4e55, 0x5e74, 0x2e93, 0x3eb2, 0x0ed1, 0x1ef0
};

uint16_t vtx_crc16(const uint8_t* data, size_t size) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < size; i++) {
        crc = (crc << 8) ^ crc16_table[((crc >> 8) ^ data[i]) & 0xFF];
    }
    return crc;
}

bool vtx_crc16_verify(const uint8_t* data, size_t size, uint16_t expected_crc) {
    uint16_t actual_crc = vtx_crc16(data, size);
    return actual_crc == expected_crc;
}

/* ========== 数据包序列化 ========== */

/* CRC字段在序列化缓冲区中的偏移（checksum字段之前的所有字段） */
/* 0-3: seq_num, 4-5: frame_id, 6: frame_type, 7: flags,
   8-9: frag_index, 10-11: total_frags, 12-13: payload_size, 14-15: checksum */
#define VTX_CRC_OFFSET 14

int vtx_packet_serialize_header(vtx_packet_header_t* header)
{
    if (!header) {
        return VTX_ERR_INVALID_PARAM;
    }

    /* 直接在header结构中转换为网络字节序 */
    header->seq_num = htonl(header->seq_num);
    header->frame_id = htons(header->frame_id);
    /* frame_type和flags是单字节，无需转换 */
    header->frag_index = htons(header->frag_index);
    header->total_frags = htons(header->total_frags);
    header->payload_size = htons(header->payload_size);
    header->checksum = htons(header->checksum);

#ifdef VTX_DEBUG
    header->timestamp_ms = htobe64(header->timestamp_ms);
#endif

    return VTX_PACKET_HEADER_SIZE;
}

int vtx_packet_deserialize_header(vtx_packet_header_t* header)
{
    if (!header) {
        return VTX_ERR_INVALID_PARAM;
    }

    /* 直接在header结构中转换为主机字节序 */
    header->seq_num = ntohl(header->seq_num);
    header->frame_id = ntohs(header->frame_id);
    /* frame_type和flags是单字节，无需转换 */
    header->frag_index = ntohs(header->frag_index);
    header->total_frags = ntohs(header->total_frags);
    header->payload_size = ntohs(header->payload_size);
    header->checksum = ntohs(header->checksum);

#ifdef VTX_DEBUG
    header->timestamp_ms = be64toh(header->timestamp_ms);
#endif

    return VTX_OK;
}

uint16_t vtx_packet_calc_crc(uint8_t* buf,
                              const uint8_t* payload,
                              size_t payload_size)
{
    if (!buf) {
        return 0;
    }

    /* 计算header（CRC字段除外）的CRC */
    uint16_t crc = vtx_crc16(buf, VTX_CRC_OFFSET);

    /* 继续计算payload的CRC */
    if (payload && payload_size > 0) {
        for (size_t i = 0; i < payload_size; i++) {
            crc = (crc << 8) ^ crc16_table[((crc >> 8) ^ payload[i]) & 0xFF];
        }
    }

    /* 更新buf中的CRC字段（网络字节序） */
    *(uint16_t*)(buf + VTX_CRC_OFFSET) = htons(crc);

    return crc;
}

bool vtx_packet_verify(const uint8_t* buf,
                       const uint8_t* payload,
                       size_t payload_size)
{
    if (!buf) {
        return false;
    }

    /* 读取buf中的CRC */
    uint16_t received_crc = ntohs(*(const uint16_t*)(buf + VTX_CRC_OFFSET));

    /* 计算header（CRC字段除外）的CRC */
    uint16_t calculated_crc = vtx_crc16(buf, VTX_CRC_OFFSET);

    /* 继续计算payload的CRC */
    if (payload && payload_size > 0) {
        for (size_t i = 0; i < payload_size; i++) {
            calculated_crc = (calculated_crc << 8) ^
                            crc16_table[((calculated_crc >> 8) ^ payload[i]) & 0xFF];
        }
    }

    /* 比较CRC */
    if (calculated_crc != received_crc) {
        vtx_log_debug("CRC mismatch: expected=0x%04x, calculated=0x%04x",
                     received_crc, calculated_crc);
        return false;
    }

    return true;
}

/* ========== 数据包验证 ========== */

bool vtx_packet_validate_header(const vtx_packet_header_t* header) {
    if (!header) {
        return false;
    }

    /* 检查分片索引 */
    if (header->frag_index >= header->total_frags) {
        vtx_log_warn("Invalid frag_index: %u >= %u",
                     header->frag_index, header->total_frags);
        return false;
    }

    /* 检查总分片数 */
    if (header->total_frags == 0) {
        vtx_log_warn("Invalid total_frags: 0");
        return false;
    }

    /* 检查载荷大小 */
    if (header->payload_size > VTX_MAX_PAYLOAD_SIZE) {
        vtx_log_warn("Invalid payload_size: %u > %u",
                     header->payload_size, VTX_MAX_PAYLOAD_SIZE);
        return false;
    }

    /* 检查帧类型 */
    if (header->frame_type < VTX_FRAME_I ||
        (header->frame_type > VTX_FRAME_A &&
         header->frame_type < VTX_DATA_CONNECT)) {
        vtx_log_warn("Invalid frame_type: %u", header->frame_type);
        return false;
    }

    return true;
}

/* ========== 调试接口 ========== */

#ifdef VTX_DEBUG

void vtx_packet_print_header(const vtx_packet_header_t* header, const char* prefix) {
    if (!header || !prefix) {
        return;
    }

    fprintf(stderr, "%s seq=%u fid=%u type=%u flags=0x%02x "
            "frag=%u/%u psize=%u crc=0x%04x",
            prefix, header->seq_num, header->frame_id,
            header->frame_type, header->flags,
            header->frag_index, header->total_frags,
            header->payload_size, header->checksum);

    if (header->flags & VTX_FLAG_LAST_FRAG) {
        fprintf(stderr, " [LAST]");
    }
    if (header->flags & VTX_FLAG_RETRANS) {
        fprintf(stderr, " [RETRANS]");
    }

    fprintf(stderr, " ts=%llu\n", (unsigned long long)header->timestamp_ms);
}

#endif
