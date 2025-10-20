/**
 * @file vtx_tx.c
 * @brief VTX Transmitter Implementation
 */

#include "vtx.h"
#include "vtx_packet.h"
#include "vtx_frame.h"
#include "vtx_error.h"
#include "vtx_log.h"
#include "vtx_mem.h"
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>

#ifdef __APPLE__
#include <libkern/OSByteOrder.h>
#define htobe64(x) OSSwapHostToBigInt64(x)
#define be64toh(x) OSSwapBigToHostInt64(x)
#elif defined(__linux__)
#include <endian.h>
#endif

/* ========== 发送端结构 ========== */

/**
 * @brief 发送端完整定义
 */
struct vtx_tx {
    /* 网络 */
    int                    sockfd;           /* UDP socket */
    struct sockaddr_in     client_addr;      /* 客户端地址 */
    socklen_t              client_addr_len;  /* 地址长度 */
    bool                   connected;        /* 连接状态 */

    /* 配置 */
    vtx_tx_config_t        config;           /* 配置副本 */

    /* 内存池 */
    vtx_frame_pool_t*      media_pool;       /* 媒体帧池 */
    vtx_frame_pool_t*      data_pool;        /* 数据帧池 */

    /* 发送队列 */
    vtx_frame_queue_t*     send_queue;       /* 待发送队列 */
    vtx_frame_queue_t*     data_queue;       /* 用户数据包队列（需要ACK） */

    /* I帧缓存 */
    vtx_frame_t*           last_iframe;      /* 最后一个I帧 */
    vtx_spinlock_t         iframe_lock;      /* I帧锁 */

    /* 序列号（原子操作） */
    atomic_uint_fast32_t   seq_num;          /* 全局序列号 */
    atomic_uint_fast16_t   frame_id;         /* 帧ID */

    /* 统计 */
    vtx_tx_stats_t         stats;            /* 统计信息 */
    vtx_spinlock_t         stats_lock;       /* 统计锁 */

    /* 回调 */
    vtx_on_data_fn         data_fn;          /* 数据帧回调 */
    vtx_on_media_fn        media_fn;         /* 媒体控制回调 */
    void*                  userdata;         /* 用户数据 */

    /* 运行状态 */
    volatile bool          running;          /* 运行标志 */
};

/* ========== 辅助函数 ========== */

/**
 * @brief 获取当前时间（毫秒）
 */
static uint64_t vtx_get_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

/**
 * @brief 创建UDP socket
 */
static int vtx_create_socket(void) {
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        vtx_log_error("Failed to create socket: %s", strerror(errno));
        return VTX_ERR_SOCKET_CREATE;
    }

    /* 设置非阻塞 */
    int flags = fcntl(sockfd, F_GETFL, 0);
    if (flags < 0 || fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) < 0) {
        vtx_log_warn("Failed to set non-blocking: %s", strerror(errno));
    }

    /* 设置发送缓冲区 */
    int sendbuf = VTX_DEFAULT_SEND_BUF;
    if (setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &sendbuf, sizeof(sendbuf)) < 0) {
        vtx_log_warn("Failed to set send buffer: %s", strerror(errno));
    }

    return sockfd;
}

/**
 * @brief 发送单个数据包
 */
static int vtx_send_packet(
    vtx_tx_t* tx,
    const vtx_packet_header_t* header,
    const uint8_t* payload,
    size_t payload_size)
{
    if (!tx || !header) {
        return VTX_ERR_INVALID_PARAM;
    }

    /* 手工序列化header到缓冲区（避免结构体padding问题） */
    uint8_t hdr_buf[VTX_PACKET_HEADER_MAX_SIZE];
    uint8_t* p = hdr_buf;

    *(uint32_t*)p = htonl(header->seq_num); p += 4;
    *(uint16_t*)p = htons(header->frame_id); p += 2;
    *p++ = header->frame_type;
    *p++ = header->flags;
    *(uint16_t*)p = htons(header->frag_index); p += 2;
    *(uint16_t*)p = htons(header->total_frags); p += 2;
    *(uint16_t*)p = htons(header->payload_size); p += 2;
    *(uint16_t*)p = 0; p += 2;  /* CRC placeholder */

#ifdef VTX_DEBUG
    uint64_t ts = htobe64(header->timestamp_ms);
    memcpy(p, &ts, 8); p += 8;
#endif

    int hdr_size = p - hdr_buf;

    /* 计算并设置CRC */
    uint16_t crc = vtx_packet_calc_crc(hdr_buf, payload, payload_size);
    vtx_log_debug("TX send: type=%u seq=%u crc=0x%04x size=%zu",
                 header->frame_type, header->seq_num, crc, payload_size);

    /* 使用iovec零拷贝发送 */
    struct iovec iov[2];
    iov[0].iov_base = hdr_buf;
    iov[0].iov_len = hdr_size;
    iov[1].iov_base = (void*)payload;
    iov[1].iov_len = payload_size;

    struct msghdr msg = {0};
    msg.msg_name = &tx->client_addr;
    msg.msg_namelen = tx->client_addr_len;
    msg.msg_iov = iov;
    msg.msg_iovlen = payload_size > 0 ? 2 : 1;

    ssize_t sent = sendmsg(tx->sockfd, &msg, 0);
    if (sent < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return VTX_ERR_BUSY;
        }
        vtx_log_error("sendmsg failed: %s", strerror(errno));
        return VTX_ERR_SOCKET_SEND;
    }

    /* 更新统计 */
    vtx_spinlock_lock(&tx->stats_lock);
    tx->stats.total_packets++;
    tx->stats.total_bytes += sent;
    vtx_spinlock_unlock(&tx->stats_lock);

    return VTX_OK;
}

/**
 * @brief 发送帧分片
 *
 * NOTE: 此函数预留给应用层发送媒体帧使用，
 * 将来会通过公共API暴露（vtx_tx_send_frame）
 */
__attribute__((unused))
static int vtx_send_frame_frags(vtx_tx_t* tx, vtx_frame_t* frame) {
    if (!tx || !frame) {
        return VTX_ERR_INVALID_PARAM;
    }

    uint16_t mtu = tx->config.mtu;
    uint16_t total_frags = vtx_packet_calc_frags(frame->data_size, mtu);

    for (uint16_t i = 0; i < total_frags; i++) {
        /* 构造包头 */
        vtx_packet_header_t header = {0};
        header.seq_num = atomic_fetch_add(&tx->seq_num, 1);
        header.frame_id = frame->frame_id;
        header.frame_type = frame->frame_type;
        header.flags = 0;
        header.frag_index = i;
        header.total_frags = total_frags;
        header.payload_size = vtx_packet_calc_frag_size(
            frame->data_size, i, mtu);

        /* 最后分片设置标志 */
        if (i == total_frags - 1) {
            header.flags |= VTX_FLAG_LAST_FRAG;
        }

#ifdef VTX_DEBUG
        header.timestamp_ms = vtx_get_time_ms();
#endif

        /* 发送分片 */
        size_t offset = vtx_packet_calc_frag_offset(i, mtu);
        int ret = vtx_send_packet(tx, &header,
                                  frame->data + offset,
                                  header.payload_size);
        if (ret != VTX_OK) {
            vtx_log_error("Failed to send fragment %u/%u: %d",
                         i, total_frags, ret);
            return ret;
        }
    }

    /* 更新统计 */
    vtx_spinlock_lock(&tx->stats_lock);
    tx->stats.total_frames++;
    if (frame->frame_type == VTX_FRAME_I) {
        tx->stats.total_i_frames++;
    } else if (frame->frame_type == VTX_FRAME_P) {
        tx->stats.total_p_frames++;
    }
    vtx_spinlock_unlock(&tx->stats_lock);

    return VTX_OK;
}

/**
 * @brief 接收ACK包
 */
static int vtx_recv_ack(vtx_tx_t* tx) {
    uint8_t buf[VTX_PACKET_HEADER_MAX_SIZE + 128];
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);

    ssize_t n = recvfrom(tx->sockfd, buf, sizeof(buf), 0,
                         (struct sockaddr*)&from_addr, &from_len);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;  /* 无数据 */
        }
        return VTX_ERR_SOCKET_RECV;
    }

    if (n < VTX_PACKET_HEADER_SIZE) {
        return VTX_ERR_PACKET_INVALID;
    }

    /* 反序列化包头 */
    vtx_packet_header_t header;
    memcpy(&header, buf, sizeof(header));

    int ret = vtx_packet_deserialize_header(&header);
    if (ret != VTX_OK) {
        return ret;
    }

    /* 验证CRC */
    if (!vtx_packet_verify(buf, buf + VTX_PACKET_HEADER_SIZE,
                          n - VTX_PACKET_HEADER_SIZE)) {
        vtx_log_warn("CRC verification failed");
        return VTX_ERR_CHECKSUM;
    }

    /* 处理控制帧 */
    if (header.frame_type == VTX_DATA_ACK) {
        /* ACK包，从data_queue中移除对应的frame */
        vtx_frame_t* frame = vtx_frame_queue_find(tx->data_queue,
                                                   header.frame_id);
        if (frame) {
            vtx_frame_queue_remove(tx->data_queue, frame);
            vtx_frame_release(tx->data_pool, frame);
        }
    } else if (header.frame_type == VTX_DATA_CONNECT) {
        /* 连接请求 */
        tx->connected = true;
        vtx_log_info("Client connected from %s:%d",
                    inet_ntoa(from_addr.sin_addr),
                    ntohs(from_addr.sin_port));
    } else if (header.frame_type == VTX_DATA_DISCONNECT) {
        /* 断开连接 */
        tx->connected = false;
        vtx_log_info("Client disconnected");
    } else if (header.frame_type == VTX_DATA_START) {
        /* 开始媒体传输 */
        vtx_log_info("Client requested START media");
        if (tx->media_fn) {
            tx->media_fn(true, tx->userdata);
        }
    } else if (header.frame_type == VTX_DATA_STOP) {
        /* 停止媒体传输 */
        vtx_log_info("Client requested STOP media");
        if (tx->media_fn) {
            tx->media_fn(false, tx->userdata);
        }
    } else if (header.frame_type == VTX_DATA_USER) {
        /* 数据包，发送ACK */
        vtx_packet_header_t ack_header = {0};
        ack_header.seq_num = atomic_fetch_add(&tx->seq_num, 1);
        ack_header.frame_id = header.frame_id;
        ack_header.frame_type = VTX_DATA_ACK;
        vtx_send_packet(tx, &ack_header, NULL, 0);

        /* 调用回调 */
        if (tx->data_fn) {
            tx->data_fn(VTX_DATA_USER,
                       buf + VTX_PACKET_HEADER_SIZE,
                       n - VTX_PACKET_HEADER_SIZE,
                       tx->userdata);
        }
    }

    return 1;  /* 处理了一个包 */
}

/* ========== 公共API ========== */

vtx_tx_t* vtx_tx_create(
    const vtx_tx_config_t* config,
    vtx_on_data_fn data_fn,
    vtx_on_media_fn media_fn,
    void* userdata)
{
    if (!config) {
        vtx_log_error("Invalid config");
        return NULL;
    }

    vtx_tx_t* tx = (vtx_tx_t*)vtx_calloc(1, sizeof(vtx_tx_t));
    if (!tx) {
        vtx_log_error("Failed to allocate tx");
        return NULL;
    }

    /* 拷贝配置 */
    tx->config = *config;
    if (!tx->config.bind_addr) {
        tx->config.bind_addr = "0.0.0.0";
    }
    if (tx->config.mtu == 0) {
        tx->config.mtu = VTX_DEFAULT_MTU;
    }
    if (tx->config.retrans_timeout_ms == 0) {
        tx->config.retrans_timeout_ms = VTX_DEFAULT_RETRANS_TIMEOUT_MS;
    }
    if (tx->config.max_retrans == 0) {
        tx->config.max_retrans = VTX_DEFAULT_MAX_RETRANS;
    }
    if (tx->config.data_retrans_timeout_ms == 0) {
        tx->config.data_retrans_timeout_ms = VTX_DEFAULT_DATA_RETRANS_TIMEOUT_MS;
    }
    if (tx->config.data_max_retrans == 0) {
        tx->config.data_max_retrans = VTX_DEFAULT_MAX_RETRANS;
    }

    /* 创建socket */
    tx->sockfd = vtx_create_socket();
    if (tx->sockfd < 0) {
        vtx_free(tx);
        return NULL;
    }

    /* 创建内存池 */
    tx->media_pool = vtx_frame_pool_create(VTX_FRAME_POOL_INIT_SIZE,
                                           VTX_MEDIA_FRAME_DATA_SIZE);
    tx->data_pool = vtx_frame_pool_create(VTX_FRAME_POOL_INIT_SIZE * 4,
                                          VTX_CTRL_FRAME_DATA_SIZE);
    if (!tx->media_pool || !tx->data_pool) {
        vtx_log_error("Failed to create frame pools");
        if (tx->media_pool) vtx_frame_pool_destroy(tx->media_pool);
        if (tx->data_pool) vtx_frame_pool_destroy(tx->data_pool);
        close(tx->sockfd);
        vtx_free(tx);
        return NULL;
    }

    /* 创建队列 */
    tx->send_queue = vtx_frame_queue_create(tx->media_pool, 0);
    tx->data_queue = vtx_frame_queue_create(
        tx->data_pool, tx->config.data_retrans_timeout_ms);
    if (!tx->send_queue || !tx->data_queue) {
        vtx_log_error("Failed to create queues");
        vtx_frame_pool_destroy(tx->media_pool);
        vtx_frame_pool_destroy(tx->data_pool);
        if (tx->send_queue) vtx_frame_queue_destroy(tx->send_queue);
        if (tx->data_queue) vtx_frame_queue_destroy(tx->data_queue);
        close(tx->sockfd);
        vtx_free(tx);
        return NULL;
    }

    /* 初始化锁 */
    vtx_spinlock_init(&tx->iframe_lock);
    vtx_spinlock_init(&tx->stats_lock);

    /* 设置回调 */
    tx->data_fn = data_fn;
    tx->media_fn = media_fn;
    tx->userdata = userdata;

    tx->running = true;

    vtx_log_info("TX created: bind=%s:%u mtu=%u",
                tx->config.bind_addr, tx->config.bind_port, tx->config.mtu);

    return tx;
}

int vtx_tx_listen(vtx_tx_t* tx) {
    if (!tx) {
        return VTX_ERR_INVALID_PARAM;
    }

    /* 绑定地址 */
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(tx->config.bind_port);

    if (inet_pton(AF_INET, tx->config.bind_addr, &addr.sin_addr) <= 0) {
        vtx_log_error("Invalid bind address: %s", tx->config.bind_addr);
        return VTX_ERR_ADDR_INVALID;
    }

    if (bind(tx->sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        vtx_log_error("Failed to bind: %s", strerror(errno));
        return VTX_ERR_SOCKET_BIND;
    }

    vtx_log_info("TX listening on %s:%u",
                tx->config.bind_addr, tx->config.bind_port);

    return VTX_OK;
}

int vtx_tx_accept(vtx_tx_t* tx, uint32_t timeout_ms) {
    if (!tx) {
        return VTX_ERR_INVALID_PARAM;
    }

    uint64_t start_ms = vtx_get_time_ms();
    uint64_t deadline_ms = timeout_ms > 0 ? start_ms + timeout_ms : UINT64_MAX;

    vtx_log_info("Waiting for client connection...");

    /* 等待连接请求 */
    while (tx->running && !tx->connected) {
        /* 检查超时 */
        if (vtx_get_time_ms() >= deadline_ms) {
            return VTX_ERR_TIMEOUT;
        }

        /* 接收数据 */
        uint8_t buf[VTX_PACKET_HEADER_MAX_SIZE];
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);

        ssize_t n = recvfrom(tx->sockfd, buf, sizeof(buf), 0,
                            (struct sockaddr*)&from_addr, &from_len);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                usleep(1000);  /* 1ms */
                continue;
            }
            vtx_log_error("recvfrom failed: %s", strerror(errno));
            return VTX_ERR_SOCKET_RECV;
        }

        if (n < VTX_PACKET_HEADER_SIZE) {
            continue;
        }

        /* 反序列化 */
        vtx_packet_header_t header;
        memcpy(&header, buf, sizeof(header));
        vtx_packet_deserialize_header(&header);

        /* 检查是否为连接请求 */
        if (header.frame_type == VTX_DATA_CONNECT) {
            tx->client_addr = from_addr;
            tx->client_addr_len = from_len;
            tx->connected = true;

            vtx_log_info("Client connected from %s:%d",
                        inet_ntoa(from_addr.sin_addr),
                        ntohs(from_addr.sin_port));

            /* 发送ACK */
            vtx_packet_header_t ack = {0};
            ack.seq_num = atomic_fetch_add(&tx->seq_num, 1);
            ack.frame_type = VTX_DATA_ACK;
            vtx_send_packet(tx, &ack, NULL, 0);

            return VTX_OK;
        }
    }

    return VTX_ERR_TIMEOUT;
}

int vtx_tx_poll(vtx_tx_t* tx, uint32_t timeout_ms) {
    if (!tx) {
        return VTX_ERR_INVALID_PARAM;
    }

    /* 使用select等待 */
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(tx->sockfd, &readfds);

    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    int ret = select(tx->sockfd + 1, &readfds, NULL, NULL,
                    timeout_ms > 0 ? &tv : NULL);
    if (ret < 0) {
        if (errno == EINTR) {
            return 0;
        }
        return VTX_ERR_IO_FAILED;
    }

    if (ret == 0) {
        return 0;  /* 超时 */
    }

    /* 处理接收到的数据 */
    return vtx_recv_ack(tx);
}

int vtx_tx_send(vtx_tx_t* tx, const uint8_t* data, size_t size) {
    if (!tx || !data || size == 0) {
        return VTX_ERR_INVALID_PARAM;
    }

    if (!tx->connected) {
        return VTX_ERR_NOT_READY;
    }

    if (size > VTX_CTRL_FRAME_DATA_SIZE) {
        return VTX_ERR_PACKET_TOO_LARGE;
    }

    /* 创建DATA帧 */
    vtx_frame_t* frame = vtx_frame_pool_acquire(tx->data_pool);
    if (!frame) {
        return VTX_ERR_NO_MEMORY;
    }

    frame->frame_id = atomic_fetch_add(&tx->frame_id, 1);
    frame->frame_type = (vtx_frame_type_t)VTX_DATA_USER;
    frame->data_size = size;
    memcpy(frame->data, data, size);
    frame->send_time_ms = vtx_get_time_ms();

    /* 发送 */
    vtx_packet_header_t header = {0};
    header.seq_num = atomic_fetch_add(&tx->seq_num, 1);
    header.frame_id = frame->frame_id;
    header.frame_type = VTX_DATA_USER;
    header.frag_index = 0;
    header.total_frags = 1;
    header.payload_size = size;

    int ret = vtx_send_packet(tx, &header, frame->data, size);
    if (ret != VTX_OK) {
        vtx_frame_release(tx->data_pool, frame);
        return ret;
    }

    /* 加入data队列等待ACK */
    vtx_frame_queue_push(tx->data_queue, frame);
    vtx_frame_release(tx->data_pool, frame);

    return VTX_OK;
}

vtx_frame_t* vtx_tx_alloc_media_frame(vtx_tx_t* tx) {
    if (!tx) {
        return NULL;
    }

    return vtx_frame_pool_acquire(tx->media_pool);
}

void vtx_tx_free_frame(vtx_tx_t* tx, vtx_frame_t* frame) {
    if (!tx || !frame) {
        return;
    }

    /* 判断frame来自哪个池 */
    if (frame->data_capacity == VTX_MEDIA_FRAME_DATA_SIZE) {
        vtx_frame_release(tx->media_pool, frame);
    } else {
        vtx_frame_release(tx->data_pool, frame);
    }
}

int vtx_tx_send_media(vtx_tx_t* tx, vtx_frame_t* frame) {
    if (!tx || !frame) {
        return VTX_ERR_INVALID_PARAM;
    }

    if (!tx->connected) {
        vtx_frame_release(tx->media_pool, frame);
        return VTX_ERR_NOT_READY;
    }

    if (frame->data_size == 0 || frame->data_size > frame->data_capacity) {
        vtx_frame_release(tx->media_pool, frame);
        return VTX_ERR_INVALID_PARAM;
    }

    /* 设置帧ID */
    frame->frame_id = atomic_fetch_add(&tx->frame_id, 1);
    frame->send_time_ms = vtx_get_time_ms();

    /* 计算分片数量 */
    size_t payload_capacity = tx->config.mtu - VTX_PACKET_HEADER_SIZE;
    uint16_t total_frags = (frame->data_size + payload_capacity - 1) / payload_capacity;

    /* 发送所有分片 */
    for (uint16_t i = 0; i < total_frags; i++) {
        size_t offset = i * payload_capacity;
        size_t payload_size = frame->data_size - offset;
        if (payload_size > payload_capacity) {
            payload_size = payload_capacity;
        }

        vtx_packet_header_t header = {0};
        header.seq_num = atomic_fetch_add(&tx->seq_num, 1);
        header.frame_id = frame->frame_id;
        header.frame_type = frame->frame_type;
        header.frag_index = i;
        header.total_frags = total_frags;
        header.payload_size = payload_size;

        if (i == total_frags - 1) {
            header.flags |= VTX_FLAG_LAST_FRAG;
        }

        int ret = vtx_send_packet(tx, &header, frame->data + offset, payload_size);
        if (ret != VTX_OK) {
            vtx_log_error("Failed to send media fragment %u/%u", i + 1, total_frags);
            vtx_frame_release(tx->media_pool, frame);
            return ret;
        }
    }

    /* 如果是I帧，缓存以备重传 */
    if (frame->frame_type == VTX_FRAME_I) {
        vtx_spinlock_lock(&tx->iframe_lock);

        /* 释放旧的I帧 */
        if (tx->last_iframe) {
            vtx_frame_release(tx->media_pool, tx->last_iframe);
        }

        /* 保存新的I帧 */
        tx->last_iframe = frame;
        vtx_frame_retain(frame);  /* 增加引用计数 */

        vtx_spinlock_unlock(&tx->iframe_lock);
    }

    /* 释放frame */
    vtx_frame_release(tx->media_pool, frame);

    /* 更新统计 */
    vtx_spinlock_lock(&tx->stats_lock);
    tx->stats.total_frames++;
    if (frame->frame_type == VTX_FRAME_I) {
        tx->stats.total_i_frames++;
    } else if (frame->frame_type == VTX_FRAME_P) {
        tx->stats.total_p_frames++;
    }
    tx->stats.total_packets += total_frags;
    tx->stats.total_bytes += frame->data_size;
    vtx_spinlock_unlock(&tx->stats_lock);

    return VTX_OK;
}

int vtx_tx_close(vtx_tx_t* tx) {
    if (!tx) {
        return VTX_ERR_INVALID_PARAM;
    }

    if (tx->connected) {
        /* 发送断开连接 */
        vtx_packet_header_t header = {0};
        header.seq_num = atomic_fetch_add(&tx->seq_num, 1);
        header.frame_type = VTX_DATA_DISCONNECT;
        vtx_send_packet(tx, &header, NULL, 0);

        tx->connected = false;
        vtx_log_info("Connection closed");
    }

    return VTX_OK;
}

int vtx_tx_get_stats(vtx_tx_t* tx, vtx_tx_stats_t* stats) {
    if (!tx || !stats) {
        return VTX_ERR_INVALID_PARAM;
    }

    vtx_spinlock_lock(&tx->stats_lock);
    *stats = tx->stats;
    vtx_spinlock_unlock(&tx->stats_lock);

    return VTX_OK;
}

void vtx_tx_destroy(vtx_tx_t* tx) {
    if (!tx) {
        return;
    }

    /* 停止运行 */
    tx->running = false;

    /* 关闭连接 */
    vtx_tx_close(tx);

    /* 释放I帧 */
    vtx_spinlock_lock(&tx->iframe_lock);
    if (tx->last_iframe) {
        vtx_frame_release(tx->media_pool, tx->last_iframe);
        tx->last_iframe = NULL;
    }
    vtx_spinlock_unlock(&tx->iframe_lock);

    /* 销毁队列 */
    if (tx->send_queue) vtx_frame_queue_destroy(tx->send_queue);
    if (tx->data_queue) vtx_frame_queue_destroy(tx->data_queue);

    /* 销毁内存池 */
    if (tx->media_pool) vtx_frame_pool_destroy(tx->media_pool);
    if (tx->data_pool) vtx_frame_pool_destroy(tx->data_pool);

    /* 销毁锁 */
    vtx_spinlock_destroy(&tx->iframe_lock);
    vtx_spinlock_destroy(&tx->stats_lock);

    /* 关闭socket */
    if (tx->sockfd >= 0) {
        close(tx->sockfd);
    }

    vtx_log_info("TX destroyed");

    vtx_free(tx);
}
