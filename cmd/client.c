/**
 * @file client.c
 * @brief VTX Client Example
 *
 * 接收端示例程序：
 * - 连接到服务器
 * - 接收数据帧
 * - 定期发送心跳数据
 * - 使用独立线程poll处理网络事件
 */

#include "vtx.h"
#include "vtx_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <sys/time.h>

/* 全局标志：程序运行状态 */
static volatile int g_running = 1;
static volatile int g_connected = 0;

/* 输出文件 */
static FILE* g_output_file = NULL;

/* 获取当前时间（毫秒） */
static uint64_t get_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

/* 信号处理函数 */
static void signal_handler(int sig) {
    (void)sig;
    g_running = 0;
    vtx_log_info("Received signal, shutting down...");
}

/* 帧接收回调 */
static int on_frame(
    const uint8_t* frame_data,
    size_t frame_size,
    vtx_frame_type_t frame_type,
    void* userdata)
{
    (void)userdata;

    vtx_log_info("Received frame: type=%d size=%zu", frame_type, frame_size);

    /* 将帧数据写入输出文件 */
    if (g_output_file && frame_data && frame_size > 0) {
        size_t written = fwrite(frame_data, 1, frame_size, g_output_file);
        if (written != frame_size) {
            vtx_log_error("Failed to write frame data: written=%zu, expected=%zu",
                         written, frame_size);
        } else {
            vtx_log_debug("Written %zu bytes to output file", written);
        }
    }

    return VTX_OK;
}

/* 控制帧回调 */
static int on_data_frame(
    vtx_data_type_t ctrl_type,
    const uint8_t* data,
    size_t size,
    void* userdata)
{
    (void)userdata;

    switch (ctrl_type) {
        case VTX_DATA_DISCONNECT:
            vtx_log_info("Server disconnected");
            g_running = 0;
            break;
        case VTX_DATA_USER:
            vtx_log_info("Received DATA: size=%zu", size);
            if (data && size > 0) {
                printf("  Content: %.*s\n", (int)size, (char*)data);
            }
            break;
        default:
            break;
    }

    return VTX_OK;
}

/* 连接事件回调 */
static void on_connect(bool connected, void* userdata) {
    (void)userdata;

    g_connected = connected;

    if (connected) {
        vtx_log_info("Connected to server");
    } else {
        vtx_log_info("Disconnected from server");
        g_running = 0;
    }
}

/* Poll线程：处理网络事件 */
static void* poll_thread(void* arg) {
    vtx_rx_t* rx = (vtx_rx_t*)arg;

    vtx_log_info("Poll thread started");

    while (g_running) {
        /* 轮询事件，超时100ms */
        int ret = vtx_rx_poll(rx, 100);
        if (ret < 0) {
            vtx_log_error("vtx_rx_poll failed: %d", ret);
            break;
        }
    }

    vtx_log_info("Poll thread stopped");
    return NULL;
}

int main(int argc, char* argv[]) {
    int ret;
    pthread_t poll_tid;

    /* 解析命令行参数 */
    const char* server_addr = "127.0.0.1";
    uint16_t server_port = 8888;

    if (argc >= 2) {
        server_addr = argv[1];
    }
    if (argc >= 3) {
        server_port = (uint16_t)atoi(argv[2]);
    }

    vtx_log_info("VTX Client starting...");
    vtx_log_info("Connecting to %s:%u", server_addr, server_port);

    /* 打开输出文件 */
    g_output_file = fopen("data/output.mp4", "wb");
    if (!g_output_file) {
        vtx_log_error("Failed to open output file: data/output.mp4");
        return EXIT_FAILURE;
    }
    vtx_log_info("Output file opened: data/output.mp4");

    /* 注册信号处理 */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* 创建接收端配置 */
    vtx_rx_config_t config = {
        .server_addr = server_addr,
        .server_port = server_port,
        .mtu = VTX_DEFAULT_MTU,
        .recv_buf_size = VTX_DEFAULT_RECV_BUF,
        .frame_timeout_ms = VTX_DEFAULT_FRAME_TIMEOUT_MS,
    };

    /* 创建接收端 */
    vtx_rx_t* rx = vtx_rx_create(&config, on_frame, on_data_frame,
                                  on_connect, NULL);
    if (!rx) {
        vtx_log_error("Failed to create RX");
        return EXIT_FAILURE;
    }

    /* 先创建poll线程，让协议栈运行起来 */
    ret = pthread_create(&poll_tid, NULL, poll_thread, rx);
    if (ret != 0) {
        vtx_log_error("Failed to create poll thread: %d", ret);
        vtx_rx_destroy(rx);
        return EXIT_FAILURE;
    }

    vtx_log_info("Poll thread started");

    /* 稍微等待让poll线程启动 */
    usleep(100000);  /* 100ms */

    vtx_log_info("Calling vtx_rx_connect()...");

    /* 连接到服务器 */
    ret = vtx_rx_connect(rx);

    if (ret != VTX_OK) {
        vtx_log_error("Failed to connect: %d", ret);
        g_running = 0;
        pthread_join(poll_tid, NULL);
        vtx_rx_destroy(rx);
        return EXIT_FAILURE;
    }

    /* 等待连接建立（最多等待5秒） */
    vtx_log_info("Waiting for connection establishment...");
    for (int i = 0; i < 50 && !g_connected && g_running; i++) {
        usleep(100000);  /* 100ms */
    }

    if (!g_connected) {
        vtx_log_error("Connection timeout");
        g_running = 0;
        pthread_join(poll_tid, NULL);
        vtx_rx_close(rx);
        vtx_rx_destroy(rx);
        return EXIT_FAILURE;
    }

    vtx_log_info("Connected successfully!");

    /* 请求开始媒体传输 */
    ret = vtx_rx_start(rx);
    if (ret != VTX_OK) {
        vtx_log_error("Failed to send START request: %d", ret);
    } else {
        vtx_log_info("Requested media streaming from server");
    }

    /* 主循环：定期发送测试数据 */
    int data_count = 0;
    while (g_running && g_connected) {
        sleep(2);

        /* 发送测试数据 */
        char test_data[128];
        snprintf(test_data, sizeof(test_data),
                 "Test data from client #%d, timestamp=%llu",
                 data_count++, (unsigned long long)get_time_ms());

        ret = vtx_rx_send(rx, (uint8_t*)test_data, strlen(test_data));
        if (ret != VTX_OK) {
            vtx_log_error("Failed to send data: %d", ret);
        } else {
            vtx_log_info("Sent data: %s", test_data);
        }

        /* 每10次打印统计信息 */
        if (data_count % 10 == 0) {
            vtx_rx_stats_t stats;
            if (vtx_rx_get_stats(rx, &stats) == VTX_OK) {
                vtx_log_info("Stats: frames=%llu packets=%llu bytes=%llu lost=%llu",
                             (unsigned long long)stats.total_frames,
                             (unsigned long long)stats.total_packets,
                             (unsigned long long)stats.total_bytes,
                             (unsigned long long)stats.lost_packets);
            }
        }
    }

    /* 清理 */
    vtx_log_info("Shutting down...");

    pthread_join(poll_tid, NULL);

    vtx_rx_close(rx);
    vtx_rx_destroy(rx);

    /* 关闭输出文件 */
    if (g_output_file) {
        fclose(g_output_file);
        vtx_log_info("Output file closed");
    }

    vtx_log_info("Client stopped");

    return EXIT_SUCCESS;
}
