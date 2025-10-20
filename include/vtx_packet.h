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
 * @file vtx_packet.h
 * @brief VTX Packet Processing
 *
 * 数据包处理模块，负责：
 * - 数据包序列化/反序列化
 * - CRC16校验
 * - 网络字节序转换
 * - 数据包验证
 */

#ifndef VTX_PACKET_H
#define VTX_PACKET_H

#include "vtx_types.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========== 数据包常量 ========== */

#define VTX_MAX_PAYLOAD_SIZE  (VTX_DEFAULT_MTU - VTX_PACKET_HEADER_SIZE)

/* ========== 数据包结构 ========== */

/**
 * @brief 完整数据包结构
 *
 * 注意：
 * - payload指向实际数据缓冲区（零拷贝），payload大小在header中
 * - 发送时通过iovec直接引用header和payload
 */
typedef struct {
    vtx_packet_header_t header;      /* 包头 */
    uint8_t      payload[0];     /* 载荷指针（零拷贝） */
} vtx_packet_t;

/* ========== 数据包序列化 ========== */

/**
 * @brief 序列化包头到缓冲区
 *
 * @param header 包头结构
 * @return 序列化的字节数，失败返回负数
 *
 * 注意：
 * - 自动转换为网络字节序
 * - CRC字段置0（由外部调用vtx_packet_calc_crc计算）
 */
int vtx_packet_serialize_header(vtx_packet_header_t* header);

/**
 * @brief 反序列化包头从缓冲区
 * @param header 输出包头结构
 * @return 0成功，负数表示错误码
 *
 * 注意：
 * - 自动转换为主机字节序
 * - 不验证CRC（需外部调用vtx_packet_verify验证）
 */
int vtx_packet_deserialize_header(vtx_packet_header_t* header);

/**
 * @brief 计算并设置整个包的CRC
 *
 * @param buf 包头缓冲区（已序列化，至少VTX_PACKET_HEADER_SIZE字节）
 * @param payload 载荷数据
 * @param payload_size 载荷大小
 * @return 计算的CRC16值
 *
 * 注意：
 * - 计算buf（CRC字段除外）+ payload的CRC16
 * - 自动更新buf中的CRC字段（网络字节序）
 */
uint16_t vtx_packet_calc_crc(uint8_t* buf,
                              const uint8_t* payload,
                              size_t payload_size);

/**
 * @brief 验证整个包的CRC
 *
 * @param buf 包头缓冲区（已序列化）
 * @param payload 载荷数据
 * @param payload_size 载荷大小
 * @return true校验通过，false校验失败
 *
 * 注意：
 * - 验证buf（CRC字段除外）+ payload的CRC16
 * - 应在反序列化前调用
 */
bool vtx_packet_verify(const uint8_t* buf,
                       const uint8_t* payload,
                       size_t payload_size);

/* ========== CRC校验 ========== */

/**
 * @brief 计算CRC16校验和
 *
 * @param data 数据缓冲区
 * @param size 数据大小
 * @return CRC16校验和
 *
 * 注意：使用CRC-16-CCITT算法（多项式0x1021）
 */
uint16_t vtx_crc16(const uint8_t* data, size_t size);

/**
 * @brief 验证CRC16校验和
 *
 * @param data 数据缓冲区（包含CRC字段）
 * @param size 数据大小
 * @param crc 期望的CRC值
 * @return true校验通过，false校验失败
 */
bool vtx_crc16_verify(const uint8_t* data, size_t size, uint16_t crc);

/* ========== 数据包验证 ========== */

/**
 * @brief 验证包头合法性
 *
 * @param header 包头结构
 * @return true合法，false非法
 *
 * 检查项：
 * - payload_size不超过MTU限制
 * - frag_index < total_frags
 * - total_frags > 0
 * - frame_type合法
 */
bool vtx_packet_validate_header(const vtx_packet_header_t* header);

/**
 * @brief 检查是否为最后一个分片
 *
 * @param header 包头结构
 * @return true是最后分片，false不是
 */
static inline bool vtx_packet_is_last_frag(const vtx_packet_header_t* header) {
    return (header->flags & VTX_FLAG_LAST_FRAG) != 0;
}

/**
 * @brief 检查是否为重传包
 *
 * @param header 包头结构
 * @return true是重传包，false不是
 */
static inline bool vtx_packet_is_retrans(const vtx_packet_header_t* header) {
    return (header->flags & VTX_FLAG_RETRANS) != 0;
}

/**
 * @brief 设置最后分片标志
 *
 * @param header 包头结构
 */
static inline void vtx_packet_set_last_frag(vtx_packet_header_t* header) {
    header->flags |= VTX_FLAG_LAST_FRAG;
}

/**
 * @brief 设置重传标志
 *
 * @param header 包头结构
 */
static inline void vtx_packet_set_retrans(vtx_packet_header_t* header) {
    header->flags |= VTX_FLAG_RETRANS;
}

/* ========== 工具函数 ========== */

/**
 * @brief 计算帧分片数
 *
 * @param frame_size 帧大小（字节）
 * @param mtu MTU大小
 * @return 分片数量
 */
static inline uint16_t vtx_packet_calc_frags(size_t frame_size, uint16_t mtu) {
    size_t payload_size = mtu - VTX_PACKET_HEADER_SIZE;
    return (uint16_t)((frame_size + payload_size - 1) / payload_size);
}

/**
 * @brief 计算分片载荷大小
 *
 * @param frame_size 帧大小（字节）
 * @param frag_index 分片索引（从0开始）
 * @param mtu MTU大小
 * @return 该分片的载荷大小
 */
static inline uint16_t vtx_packet_calc_frag_size(
    size_t frame_size,
    uint16_t frag_index,
    uint16_t mtu)
{
    size_t payload_size = mtu - VTX_PACKET_HEADER_SIZE;
    size_t offset = (size_t)frag_index * payload_size;
    size_t remaining = frame_size - offset;
    return (uint16_t)(remaining < payload_size ? remaining : payload_size);
}

/**
 * @brief 计算分片在帧中的偏移
 *
 * @param frag_index 分片索引（从0开始）
 * @param mtu MTU大小
 * @return 偏移量（字节）
 */
static inline size_t vtx_packet_calc_frag_offset(
    uint16_t frag_index,
    uint16_t mtu)
{
    size_t payload_size = mtu - VTX_PACKET_HEADER_SIZE;
    return (size_t)frag_index * payload_size;
}

/* ========== 调试接口 ========== */

#ifdef VTX_DEBUG

/**
 * @brief 打印包头信息（调试用）
 *
 * @param header 包头结构
 * @param prefix 前缀字符串
 */
void vtx_packet_print_header(const vtx_packet_header_t* header, const char* prefix);

#endif

#ifdef __cplusplus
}
#endif

#endif /* VTX_PACKET_H */
