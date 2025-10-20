/**
 * @file vtx_rx.c
 * @brief VTX Receiver Implementation
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

/* ========== 接收端结构 ========== */

/**
 * @brief 接收端完整定义
 */
struct vtx_rx {
    /* 网络 */
    int                    sockfd;           /* UDP socket */
    struct sockaddr_in     server_addr;      /* 服务器地址 */
    socklen_t              server_addr_len;  /* 地址长度 */
    bool                   connected;        /* 连接状态 */

    /* 配置 */
    vtx_rx_config_t        config;           /* 配置副本 */

    /* 内存池 */
    vtx_frame_pool_t*      media_pool;       /* 媒体帧池 */
    vtx_frame_pool_t*      data_pool;        /* 控制帧池 */
    vtx_frag_pool_t*       frag_pool;        /* 分片池 */

    /* 接收队列 */
    vtx_frame_queue_t*     recv_queue;       /* 接收中的帧队列 */
    vtx_frame_queue_t*     data_queue;       /* DATA包队列（需要ACK） */

    /* I帧缓存 */
    vtx_frame_t*           last_iframe;      /* 最后一个I帧 */
    vtx_spinlock_t         iframe_lock;      /* I帧锁 */

    /* 序列号（原子操作） */
    atomic_uint_fast32_t   seq_num;          /* 全局序列号 */
    atomic_uint_fast16_t   frame_id;         /* 帧ID */
    atomic_uint_fast32_t   last_recv_seq;    /* 最后接收的序列号 */

    /* 统计 */
    vtx_rx_stats_t         stats;            /* 统计信息 */
    vtx_spinlock_t         stats_lock;       /* 统计锁 */

    /* 回调 */
    vtx_on_frame_fn        frame_fn;         /* 帧回调 */
    vtx_on_data_fn         data_fn;          /* 控制帧回调 */
    vtx_on_connect_fn      connect_fn;       /* 连接回调 */
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

    /* 设置接收缓冲区 */
    int recvbuf = VTX_DEFAULT_RECV_BUF;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &recvbuf, sizeof(recvbuf)) < 0) {
        vtx_log_warn("Failed to set recv buffer: %s", strerror(errno));
    }

    return sockfd;
}

/**
 * @brief 发送数据包
 */
static int vtx_send_packet(
    vtx_rx_t* rx,
    const vtx_packet_header_t* header,
    const uint8_t* payload,
    size_t payload_size)
{
    if (!rx || !header) {
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

    /* 计算CRC */
    vtx_packet_calc_crc(hdr_buf, payload, payload_size);

    /* 发送 */
    struct iovec iov[2];
    iov[0].iov_base = hdr_buf;
    iov[0].iov_len = hdr_size;
    iov[1].iov_base = (void*)payload;
    iov[1].iov_len = payload_size;

    struct msghdr msg = {0};
    msg.msg_name = &rx->server_addr;
    msg.msg_namelen = rx->server_addr_len;
    msg.msg_iov = iov;
    msg.msg_iovlen = payload_size > 0 ? 2 : 1;

    ssize_t sent = sendmsg(rx->sockfd, &msg, 0);
    if (sent < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return VTX_ERR_BUSY;
        }
        return VTX_ERR_SOCKET_SEND;
    }

    return VTX_OK;
}

/**
 * @brief 发送ACK
 */
static int vtx_send_ack(vtx_rx_t* rx, uint16_t frame_id) {
    vtx_packet_header_t header = {0};
    header.seq_num = atomic_fetch_add(&rx->seq_num, 1);
    header.frame_id = frame_id;
    header.frame_type = VTX_DATA_ACK;
    return vtx_send_packet(rx, &header, NULL, 0);
}

/**
 * @brief 处理接收到的分片
 */
static int vtx_handle_fragment(
    vtx_rx_t* rx,
    const vtx_packet_header_t* header,
    const uint8_t* payload)
{
    if (!rx || !header || !payload) {
        return VTX_ERR_INVALID_PARAM;
    }

    /* 查找或创建frame */
    vtx_frame_t* frame = vtx_frame_queue_find(rx->recv_queue, header->frame_id);
    if (!frame) {
        /* 新帧 */
        frame = vtx_frame_pool_acquire(rx->media_pool);
        if (!frame) {
            vtx_log_error("Failed to acquire frame");
            return VTX_ERR_NO_MEMORY;
        }

        /* 初始化 */
        int ret = vtx_frame_init_recv(frame, header->frame_id,
                                      header->frame_type,
                                      header->total_frags);
        if (ret != VTX_OK) {
            vtx_frame_release(rx->media_pool, frame);
            return ret;
        }

        /* 加入接收队列 */
        vtx_frame_queue_push(rx->recv_queue, frame);
        vtx_frame_release(rx->media_pool, frame);
    }

    /* 检查是否已接收此分片 */
    if (vtx_frame_has_frag(frame, header->frag_index)) {
        vtx_spinlock_lock(&rx->stats_lock);
        rx->stats.dup_packets++;
        vtx_spinlock_unlock(&rx->stats_lock);
        return VTX_OK;  /* 重复分片 */
    }

    /* 拷贝payload到frame */
    size_t offset = vtx_packet_calc_frag_offset(header->frag_index,
                                                rx->config.mtu);
    if (offset + header->payload_size > frame->data_capacity) {
        vtx_log_error("Fragment overflow: offset=%zu size=%u capacity=%zu",
                     offset, header->payload_size, frame->data_capacity);
        return VTX_ERR_OVERFLOW;
    }

    memcpy(frame->data + offset, payload, header->payload_size);
    frame->data_size += header->payload_size;

    /* 标记分片已接收 */
    vtx_frame_mark_frag_received(frame, header->frag_index);

    /* 对于I帧，发送分片ACK */
    if (header->frame_type == VTX_FRAME_I) {
        vtx_packet_header_t ack_header = {0};
        ack_header.seq_num = atomic_fetch_add(&rx->seq_num, 1);
        ack_header.frame_id = header->frame_id;
        ack_header.frag_index = header->frag_index;
        ack_header.frame_type = VTX_DATA_ACK;
        vtx_send_packet(rx, &ack_header, NULL, 0);
    }

    /* 更新统计 */
    vtx_spinlock_lock(&rx->stats_lock);
    rx->stats.total_packets++;
    rx->stats.total_bytes += header->payload_size;
    vtx_spinlock_unlock(&rx->stats_lock);

    /* 检查是否完整 */
    if (vtx_frame_is_complete(frame)) {
        /* 从接收队列移除 */
        vtx_frame_queue_remove(rx->recv_queue, frame);

        /* 如果是I帧，缓存 */
        if (header->frame_type == VTX_FRAME_I) {
            vtx_spinlock_lock(&rx->iframe_lock);
            if (rx->last_iframe) {
                vtx_frame_release(rx->media_pool, rx->last_iframe);
            }
            rx->last_iframe = vtx_frame_retain(frame);
            vtx_spinlock_unlock(&rx->iframe_lock);
        }

        /* 调用回调 */
        if (rx->frame_fn) {
            rx->frame_fn(frame->data, frame->data_size,
                        frame->frame_type, rx->userdata);
        }

        /* 更新统计 */
        vtx_spinlock_lock(&rx->stats_lock);
        rx->stats.total_frames++;
        if (frame->frame_type == VTX_FRAME_I) {
            rx->stats.total_i_frames++;
        } else if (frame->frame_type == VTX_FRAME_P) {
            rx->stats.total_p_frames++;
        }
        vtx_spinlock_unlock(&rx->stats_lock);

        vtx_log_debug("Frame complete: id=%u type=%u size=%zu",
                     frame->frame_id, frame->frame_type, frame->data_size);
    }

    return VTX_OK;
}

/**
 * @brief 处理重传队列（超时重传和清理）
 */
static void vtx_process_retrans_queue(vtx_rx_t* rx) {
    uint64_t now_ms = vtx_get_time_ms();
    vtx_frame_t* frame;
    vtx_frame_t* tmp;

    vtx_spinlock_lock(&rx->data_queue->lock);

    list_for_each_entry_safe(frame, tmp, &rx->data_queue->frames, list) {
        /* 检查重传次数是否超限 */
        if (frame->retrans_count >= 3) {
            vtx_log_warn("Frame dropped: id=%u, retrans=%u",
                       frame->frame_id, frame->retrans_count);

            /* 从队列移除并释放 */
            list_del(&frame->list);
            rx->data_queue->count--;
            vtx_spinlock_unlock(&rx->data_queue->lock);

            vtx_frame_release(rx->data_pool, frame);

            vtx_spinlock_lock(&rx->data_queue->lock);
            continue;
        }

        /* 检查是否需要重传 */
        uint64_t elapsed = now_ms - frame->send_time_ms;
        if (elapsed >= 100) {  /* 100ms 超时 */
            /* 需要重传 */
            frame->retrans_count++;
            frame->send_time_ms = now_ms;

            vtx_log_debug("Retransmitting frame: id=%u, retrans=%u, elapsed=%llu ms",
                        frame->frame_id, frame->retrans_count,
                        (unsigned long long)elapsed);

            /* 重新发送数据包 */
            vtx_packet_header_t header = {0};
            header.seq_num = atomic_fetch_add(&rx->seq_num, 1);
            header.frame_id = frame->frame_id;
            header.frame_type = VTX_DATA_USER;
            header.frag_index = 0;
            header.total_frags = 1;
            header.payload_size = frame->data_size;
            header.flags = VTX_FLAG_RETRANS;

            vtx_spinlock_unlock(&rx->data_queue->lock);

            vtx_send_packet(rx, &header, frame->data, frame->data_size);

            vtx_spinlock_lock(&rx->data_queue->lock);
        }
    }

    vtx_spinlock_unlock(&rx->data_queue->lock);
}

/**
 * @brief 接收并处理数据包
 */
static int vtx_recv_packet(vtx_rx_t* rx) {
    uint8_t buf[VTX_DEFAULT_MTU];
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);

    ssize_t n = recvfrom(rx->sockfd, buf, sizeof(buf), 0,
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
        vtx_log_warn("CRC verification failed: type=%u seq=%u size=%zd",
                    header.frame_type, header.seq_num, n);
        return VTX_ERR_CHECKSUM;
    }

    /* 验证包头 */
    if (!vtx_packet_validate_header(&header)) {
        return VTX_ERR_PACKET_INVALID;
    }

    /* 检测丢包 */
    uint32_t last_seq = atomic_load(&rx->last_recv_seq);
    if (last_seq > 0 && header.seq_num > last_seq + 1) {
        uint32_t lost = header.seq_num - last_seq - 1;
        vtx_spinlock_lock(&rx->stats_lock);
        rx->stats.lost_packets += lost;
        vtx_spinlock_unlock(&rx->stats_lock);
    }
    atomic_store(&rx->last_recv_seq, header.seq_num);

    /* 发送ACK（对任意包都ACK） */
    vtx_send_ack(rx, header.frame_id);

    /* 使用状态机处理不同类型的包 */
    if (header.frame_type >= VTX_FRAME_I && header.frame_type <= VTX_FRAME_A) {
        /* 媒体帧分片 */
        return vtx_handle_fragment(rx, &header, buf + VTX_PACKET_HEADER_SIZE);
    }

    switch (header.frame_type) {
    case VTX_DATA_ACK: {
        /* ACK包，从data_queue中移除对应的frame */
        vtx_frame_t* frame = vtx_frame_queue_find(rx->data_queue,
                                                   header.frame_id);
        if (frame) {
            vtx_frame_queue_remove(rx->data_queue, frame);
            vtx_frame_release(rx->data_pool, frame);
        }
        break;
    }

    case VTX_DATA_DISCONNECT:
        /* 断开连接 */
        rx->connected = false;
        if (rx->connect_fn) {
            rx->connect_fn(false, rx->userdata);
        }
        vtx_log_info("Server disconnected");
        break;

    case VTX_DATA_USER:
        /* 数据包 */
        if (rx->data_fn) {
            rx->data_fn(VTX_DATA_USER,
                       buf + VTX_PACKET_HEADER_SIZE,
                       n - VTX_PACKET_HEADER_SIZE,
                       rx->userdata);
        }
        break;

    default:
        vtx_log_warn("Unknown frame type: %u", header.frame_type);
        break;
    }

    return 1;  /* 处理了一个包 */
}

/* ========== 公共API ========== */

vtx_rx_t* vtx_rx_create(
    const vtx_rx_config_t* config,
    vtx_on_frame_fn frame_fn,
    vtx_on_data_fn data_fn,
    vtx_on_connect_fn connect_fn,
    void* userdata)
{
    if (!config || !frame_fn) {
        vtx_log_error("Invalid config or frame_fn");
        return NULL;
    }

    vtx_rx_t* rx = (vtx_rx_t*)vtx_calloc(1, sizeof(vtx_rx_t));
    if (!rx) {
        vtx_log_error("Failed to allocate rx");
        return NULL;
    }

    /* 拷贝配置 */
    rx->config = *config;
    if (rx->config.mtu == 0) {
        rx->config.mtu = VTX_DEFAULT_MTU;
    }
    if (rx->config.frame_timeout_ms == 0) {
        rx->config.frame_timeout_ms = VTX_DEFAULT_FRAME_TIMEOUT_MS;
    }

    /* 创建socket */
    rx->sockfd = vtx_create_socket();
    if (rx->sockfd < 0) {
        vtx_free(rx);
        return NULL;
    }

    /* 解析服务器地址 */
    rx->server_addr.sin_family = AF_INET;
    rx->server_addr.sin_port = htons(config->server_port);
    if (inet_pton(AF_INET, config->server_addr,
                  &rx->server_addr.sin_addr) <= 0) {
        vtx_log_error("Invalid server address: %s", config->server_addr);
        close(rx->sockfd);
        vtx_free(rx);
        return NULL;
    }
    rx->server_addr_len = sizeof(rx->server_addr);

    /* 创建内存池 */
    rx->media_pool = vtx_frame_pool_create(VTX_FRAME_POOL_INIT_SIZE,
                                           VTX_MEDIA_FRAME_DATA_SIZE);
    rx->data_pool = vtx_frame_pool_create(VTX_FRAME_POOL_INIT_SIZE * 4,
                                          VTX_CTRL_FRAME_DATA_SIZE);
    rx->frag_pool = vtx_frag_pool_create(128);  /* 默认128个分片 */
    if (!rx->media_pool || !rx->data_pool || !rx->frag_pool) {
        vtx_log_error("Failed to create frame pools");
        if (rx->media_pool) vtx_frame_pool_destroy(rx->media_pool);
        if (rx->data_pool) vtx_frame_pool_destroy(rx->data_pool);
        if (rx->frag_pool) vtx_frag_pool_destroy(rx->frag_pool);
        close(rx->sockfd);
        vtx_free(rx);
        return NULL;
    }

    /* 创建队列 */
    rx->recv_queue = vtx_frame_queue_create(
        rx->media_pool, rx->config.frame_timeout_ms);
    rx->data_queue = vtx_frame_queue_create(rx->data_pool, 0);
    if (!rx->recv_queue || !rx->data_queue) {
        vtx_log_error("Failed to create queues");
        vtx_frame_pool_destroy(rx->media_pool);
        vtx_frame_pool_destroy(rx->data_pool);
        if (rx->recv_queue) vtx_frame_queue_destroy(rx->recv_queue);
        if (rx->data_queue) vtx_frame_queue_destroy(rx->data_queue);
        close(rx->sockfd);
        vtx_free(rx);
        return NULL;
    }

    /* 初始化锁 */
    vtx_spinlock_init(&rx->iframe_lock);
    vtx_spinlock_init(&rx->stats_lock);

    /* 设置回调 */
    rx->frame_fn = frame_fn;
    rx->data_fn = data_fn;
    rx->connect_fn = connect_fn;
    rx->userdata = userdata;

    rx->running = true;

    vtx_log_info("RX created: server=%s:%u mtu=%u",
                config->server_addr, config->server_port, rx->config.mtu);

    return rx;
}

int vtx_rx_connect(vtx_rx_t* rx) {
    if (!rx) {
        return VTX_ERR_INVALID_PARAM;
    }

    /* 发送连接请求 */
    vtx_packet_header_t header = {0};
    header.seq_num = atomic_fetch_add(&rx->seq_num, 1);
    header.frame_type = VTX_DATA_CONNECT;

    int ret = vtx_send_packet(rx, &header, NULL, 0);
    if (ret != VTX_OK) {
        vtx_log_error("Failed to send CONNECT: %d", ret);
        return ret;
    }

    vtx_log_info("Connecting to %s:%u...",
                inet_ntoa(rx->server_addr.sin_addr),
                ntohs(rx->server_addr.sin_port));

    /* 等待ACK */
    uint64_t start_ms = vtx_get_time_ms();
    while (vtx_get_time_ms() - start_ms < 5000) {  /* 5秒超时 */
        ret = vtx_recv_packet(rx);
        if (ret > 0) {
            /* 检查是否收到ACK */
            rx->connected = true;
            if (rx->connect_fn) {
                rx->connect_fn(true, rx->userdata);
            }
            vtx_log_info("Connected successfully");
            return VTX_OK;
        }
        usleep(1000);  /* 1ms */
    }

    vtx_log_error("Connection timeout");
    return VTX_ERR_TIMEOUT;
}

int vtx_rx_poll(vtx_rx_t* rx, uint32_t timeout_ms) {
    if (!rx) {
        return VTX_ERR_INVALID_PARAM;
    }

    /* 使用select等待 */
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(rx->sockfd, &readfds);

    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    int ret = select(rx->sockfd + 1, &readfds, NULL, NULL,
                    timeout_ms > 0 ? &tv : NULL);
    if (ret < 0) {
        if (errno == EINTR) {
            return 0;
        }
        return VTX_ERR_IO_FAILED;
    }

    if (ret == 0) {
        /* 超时：处理重传队列和清理超时帧 */
        vtx_process_retrans_queue(rx);

        if (rx->recv_queue) {
            size_t cleaned = vtx_frame_queue_cleanup_timeout(
                rx->recv_queue, vtx_get_time_ms());
            if (cleaned > 0) {
                vtx_spinlock_lock(&rx->stats_lock);
                rx->stats.incomplete_frames += cleaned;
                vtx_spinlock_unlock(&rx->stats_lock);
                vtx_log_debug("Cleaned %zu timeout frames", cleaned);
            }
        }
        return 0;
    }

    /* 处理接收到的数据 */
    return vtx_recv_packet(rx);
}

int vtx_rx_send(vtx_rx_t* rx, const uint8_t* data, size_t size) {
    if (!rx || !data || size == 0) {
        return VTX_ERR_INVALID_PARAM;
    }

    if (!rx->connected) {
        return VTX_ERR_NOT_READY;
    }

    if (size > VTX_CTRL_FRAME_DATA_SIZE) {
        return VTX_ERR_PACKET_TOO_LARGE;
    }

    /* 创建DATA帧 */
    vtx_frame_t* frame = vtx_frame_pool_acquire(rx->data_pool);
    if (!frame) {
        return VTX_ERR_NO_MEMORY;
    }

    frame->frame_id = atomic_fetch_add(&rx->frame_id, 1);
    frame->frame_type = (vtx_frame_type_t)VTX_DATA_USER;
    frame->data_size = size;
    memcpy(frame->data, data, size);
    frame->send_time_ms = vtx_get_time_ms();

    /* 发送 */
    vtx_packet_header_t header = {0};
    header.seq_num = atomic_fetch_add(&rx->seq_num, 1);
    header.frame_id = frame->frame_id;
    header.frame_type = VTX_DATA_USER;
    header.frag_index = 0;
    header.total_frags = 1;
    header.payload_size = size;

    int ret = vtx_send_packet(rx, &header, frame->data, size);
    if (ret != VTX_OK) {
        vtx_frame_release(rx->data_pool, frame);
        return ret;
    }

    /* 加入data队列等待ACK */
    vtx_frame_queue_push(rx->data_queue, frame);
    vtx_frame_release(rx->data_pool, frame);

    return VTX_OK;
}

int vtx_rx_start(vtx_rx_t* rx) {
    if (!rx) {
        return VTX_ERR_INVALID_PARAM;
    }

    if (!rx->connected) {
        return VTX_ERR_NOT_READY;
    }

    /* 发送START控制帧 */
    vtx_packet_header_t header = {0};
    header.seq_num = atomic_fetch_add(&rx->seq_num, 1);
    header.frame_type = VTX_DATA_START;

    int ret = vtx_send_packet(rx, &header, NULL, 0);
    if (ret != VTX_OK) {
        vtx_log_error("Failed to send START: %d", ret);
        return ret;
    }

    vtx_log_info("Sent START request to server");
    return VTX_OK;
}

int vtx_rx_stop(vtx_rx_t* rx) {
    if (!rx) {
        return VTX_ERR_INVALID_PARAM;
    }

    if (!rx->connected) {
        return VTX_ERR_NOT_READY;
    }

    /* 发送STOP控制帧 */
    vtx_packet_header_t header = {0};
    header.seq_num = atomic_fetch_add(&rx->seq_num, 1);
    header.frame_type = VTX_DATA_STOP;

    int ret = vtx_send_packet(rx, &header, NULL, 0);
    if (ret != VTX_OK) {
        vtx_log_error("Failed to send STOP: %d", ret);
        return ret;
    }

    vtx_log_info("Sent STOP request to server");
    return VTX_OK;
}

int vtx_rx_close(vtx_rx_t* rx) {
    if (!rx) {
        return VTX_ERR_INVALID_PARAM;
    }

    if (rx->connected) {
        /* 发送断开连接 */
        vtx_packet_header_t header = {0};
        header.seq_num = atomic_fetch_add(&rx->seq_num, 1);
        header.frame_type = VTX_DATA_DISCONNECT;
        vtx_send_packet(rx, &header, NULL, 0);

        rx->connected = false;
        if (rx->connect_fn) {
            rx->connect_fn(false, rx->userdata);
        }
        vtx_log_info("Connection closed");
    }

    return VTX_OK;
}

int vtx_rx_get_stats(vtx_rx_t* rx, vtx_rx_stats_t* stats) {
    if (!rx || !stats) {
        return VTX_ERR_INVALID_PARAM;
    }

    vtx_spinlock_lock(&rx->stats_lock);
    *stats = rx->stats;
    vtx_spinlock_unlock(&rx->stats_lock);

    return VTX_OK;
}

void vtx_rx_destroy(vtx_rx_t* rx) {
    if (!rx) {
        return;
    }

    /* 停止运行 */
    rx->running = false;

    /* 关闭连接 */
    vtx_rx_close(rx);

    /* 释放I帧 */
    vtx_spinlock_lock(&rx->iframe_lock);
    if (rx->last_iframe) {
        vtx_frame_release(rx->media_pool, rx->last_iframe);
        rx->last_iframe = NULL;
    }
    vtx_spinlock_unlock(&rx->iframe_lock);

    /* 销毁队列 */
    if (rx->recv_queue) vtx_frame_queue_destroy(rx->recv_queue);
    if (rx->data_queue) vtx_frame_queue_destroy(rx->data_queue);

    /* 销毁内存池 */
    if (rx->media_pool) vtx_frame_pool_destroy(rx->media_pool);
    if (rx->data_pool) vtx_frame_pool_destroy(rx->data_pool);
    if (rx->frag_pool) vtx_frag_pool_destroy(rx->frag_pool);

    /* 销毁锁 */
    vtx_spinlock_destroy(&rx->iframe_lock);
    vtx_spinlock_destroy(&rx->stats_lock);

    /* 关闭socket */
    if (rx->sockfd >= 0) {
        close(rx->sockfd);
    }

    vtx_log_info("RX destroyed");

    vtx_free(rx);
}
