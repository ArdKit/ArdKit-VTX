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
 * @file server.c
 * @brief VTX Server Example with FFmpeg
 *
 * 发送端示例程序：
 * - 监听端口等待客户端连接
 * - 接收START/STOP控制指令
 * - 使用FFmpeg读取视频文件并发送
 * - 按帧率间隔发送媒体数据
 */

#include "vtx.h"
#include "vtx_frame.h"
#include "vtx_packet.h"
#include "vtx_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <sys/time.h>

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>

/* 全局标志 */
static volatile int g_running = 1;
static volatile int g_streaming = 0;  /* 媒体流状态：0=停止，1=发送中 */
static char g_media_file[256] = "";   /* 当前媒体文件路径 */
static pthread_mutex_t g_media_lock = PTHREAD_MUTEX_INITIALIZER;  /* 保护 g_media_file */
static char g_root_dir[256] = "data"; /* 媒体文件根目录 */
static pthread_t g_media_tid = 0;     /* 媒体线程ID */
static vtx_tx_t* g_tx = NULL;         /* TX对象（供回调使用） */

/* FFmpeg上下文 */
typedef struct {
    AVFormatContext* fmt_ctx;
    AVCodecContext*  codec_ctx;
    int              video_stream_idx;
    int64_t          start_time;
    int64_t          frame_count;
    double           fps;
} ffmpeg_ctx_t;

/* 获取当前时间（毫秒） */
static uint64_t get_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

/* 信号处理 */
static void signal_handler(int sig) {
    (void)sig;
    g_running = 0;
    g_streaming = 0;
    vtx_log_info("Received signal, shutting down...");
}

/* 媒体线程声明 */
static void* media_thread(void* arg);

/* 媒体控制回调 */
static void on_media(vtx_data_type_t data_type, const char* url, void* userdata) {
    (void)userdata;

    if (data_type == VTX_DATA_START) {
        /* 如果已经在播放，先停止 */
        if (g_streaming && g_media_tid != 0) {
            vtx_log_info("Stopping current media stream...");
            g_streaming = 0;
            pthread_join(g_media_tid, NULL);
            g_media_tid = 0;
        }

        /* 处理URL参数，构建完整文件路径 */
        char temp_file[256];
        if (url && url[0] != '\0') {
            /* URL格式: /path/to/file?offset=10,size=20
             * 如果以'/'开头，去掉'/'，作为相对于根目录的路径
             */
            const char* file_path = url;
            if (file_path[0] == '/') {
                file_path++;  /* 跳过开头的'/' */
            }

            /* 提取文件路径（去掉查询参数） */
            const char* query_start = strchr(file_path, '?');
            size_t path_len = query_start ? (size_t)(query_start - file_path) : strlen(file_path);

            /* 构建完整路径: root_dir + "/" + file_path */
            if (path_len > 0) {
                size_t root_len = strlen(g_root_dir);
                if (root_len + 1 + path_len < sizeof(temp_file)) {
                    snprintf(temp_file, sizeof(temp_file), "%s/%.*s",
                            g_root_dir, (int)path_len, file_path);
                    vtx_log_info("START media streaming: %s", temp_file);
                } else {
                    vtx_log_error("File path too long, cannot start streaming");
                    return;
                }
            } else {
                vtx_log_error("Empty file path, cannot start streaming");
                return;
            }
        } else {
            vtx_log_error("No URL provided, cannot start streaming");
            return;
        }

        /* 使用互斥锁保护 g_media_file */
        pthread_mutex_lock(&g_media_lock);
        strncpy(g_media_file, temp_file, sizeof(g_media_file) - 1);
        g_media_file[sizeof(g_media_file) - 1] = '\0';
        pthread_mutex_unlock(&g_media_lock);

        /* 启动媒体线程 - 先创建线程，成功后再设置状态 */
        pthread_t tid;
        if (pthread_create(&tid, NULL, media_thread, g_tx) != 0) {
            vtx_log_error("Failed to create media thread");
            return;
        }

        /* 线程创建成功，设置状态 */
        g_media_tid = tid;
        g_streaming = 1;
        vtx_log_info("Media thread started");

    } else if (data_type == VTX_DATA_STOP) {
        /* 停止媒体线程 */
        if (g_streaming && g_media_tid != 0) {
            vtx_log_info("STOP media streaming");
            g_streaming = 0;
            pthread_join(g_media_tid, NULL);
            g_media_tid = 0;
            vtx_log_info("Media thread stopped");
        }
    }
}

/* 控制帧回调 */
static int on_data(vtx_data_type_t type, const uint8_t* data,
                   size_t size, void* userdata) {
    (void)userdata;

    if (type == VTX_DATA_USER && data && size > 0) {
        vtx_log_info("Received DATA: %.*s", (int)size, (char*)data);
    }

    return VTX_OK;
}

/* 初始化FFmpeg */
static int init_ffmpeg(ffmpeg_ctx_t* ctx, const char* filename) {
    memset(ctx, 0, sizeof(*ctx));

    /* 打开输入文件 */
    if (avformat_open_input(&ctx->fmt_ctx, filename, NULL, NULL) < 0) {
        vtx_log_error("Failed to open input file: %s", filename);
        return -1;
    }

    /* 查找流信息 */
    if (avformat_find_stream_info(ctx->fmt_ctx, NULL) < 0) {
        vtx_log_error("Failed to find stream info");
        avformat_close_input(&ctx->fmt_ctx);
        return -1;
    }

    /* 查找视频流 */
    ctx->video_stream_idx = -1;
    for (unsigned int i = 0; i < ctx->fmt_ctx->nb_streams; i++) {
        if (ctx->fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            ctx->video_stream_idx = i;
            break;
        }
    }

    if (ctx->video_stream_idx == -1) {
        vtx_log_error("No video stream found");
        avformat_close_input(&ctx->fmt_ctx);
        return -1;
    }

    /* 获取帧率 */
    AVStream* stream = ctx->fmt_ctx->streams[ctx->video_stream_idx];
    ctx->fps = av_q2d(stream->avg_frame_rate);
    if (ctx->fps <= 0) {
        ctx->fps = av_q2d(stream->r_frame_rate);
    }

    vtx_log_info("Video: codec=%s fps=%.2f duration=%.2fs",
                avcodec_get_name(stream->codecpar->codec_id),
                ctx->fps,
                (double)ctx->fmt_ctx->duration / AV_TIME_BASE);

    return 0;
}

/* 清理FFmpeg */
static void cleanup_ffmpeg(ffmpeg_ctx_t* ctx) {
    if (ctx->fmt_ctx) {
        avformat_close_input(&ctx->fmt_ctx);
    }
}

/* 发送媒体帧 */
static int send_media_frame(vtx_tx_t* tx, AVPacket* pkt, vtx_frame_type_t frame_type) {
    /* 从TX分配media frame */
    vtx_frame_t* frame = vtx_tx_alloc_media_frame(tx);
    if (!frame) {
        vtx_log_error("Failed to allocate media frame");
        return VTX_ERR_NO_MEMORY;
    }

    /* 复制packet数据到frame */
    size_t copied = vtx_frame_copyto(frame, 0, pkt->data, pkt->size);
    if (copied != (size_t)pkt->size) {
        vtx_log_error("Failed to copy packet data: copied=%zu expected=%d", copied, pkt->size);
        vtx_tx_free_frame(tx, frame);
        return VTX_ERR_INVALID_PARAM;
    }

    /* 设置帧类型 */
    frame->frame_type = frame_type;

    /* 发送媒体帧 */
    int ret = vtx_tx_send_media(tx, frame);
    if (ret != VTX_OK) {
        vtx_log_error("Failed to send media frame: %d", ret);
        /* 注意：vtx_tx_send_media 失败时已经释放了 frame */
        return ret;
    }

    vtx_log_debug("Sent frame: type=%d size=%d", frame_type, pkt->size);

    return VTX_OK;
}

/* 媒体发送线程 */
static void* media_thread(void* arg) {
    vtx_tx_t* tx = (vtx_tx_t*)arg;
    ffmpeg_ctx_t ffmpeg;
    AVPacket* pkt = NULL;
    char media_file[256];

    vtx_log_info("Media thread started");

    /* 使用互斥锁读取媒体文件路径 */
    pthread_mutex_lock(&g_media_lock);
    strncpy(media_file, g_media_file, sizeof(media_file) - 1);
    media_file[sizeof(media_file) - 1] = '\0';
    pthread_mutex_unlock(&g_media_lock);

    /* 初始化FFmpeg，读取媒体文件 */
    if (init_ffmpeg(&ffmpeg, media_file) != 0) {
        vtx_log_error("Failed to initialize FFmpeg with file: %s", media_file);
        return NULL;
    }

    pkt = av_packet_alloc();
    if (!pkt) {
        cleanup_ffmpeg(&ffmpeg);
        return NULL;
    }

    /* 帧时间间隔（毫秒） */
    uint64_t frame_interval_ms = (uint64_t)(1000.0 / ffmpeg.fps);
    vtx_log_info("Frame interval: %llu ms (%.2f fps)",
                (unsigned long long)frame_interval_ms, ffmpeg.fps);

    while (g_running) {
        /* 如果不在流状态，等待 */
        while (g_running && !g_streaming) {
            usleep(100000);  /* 100ms */
        }

        if (!g_running) break;

        /* 读取帧 */
        int ret = av_read_frame(ffmpeg.fmt_ctx, pkt);

        /* 到达文件末尾，循环播放 */
        if (ret == AVERROR_EOF) {
            vtx_log_info("EOF reached, looping...");
            av_seek_frame(ffmpeg.fmt_ctx, ffmpeg.video_stream_idx, 0, AVSEEK_FLAG_BACKWARD);
            ffmpeg.frame_count = 0;
            ffmpeg.start_time = get_time_ms();
            continue;
        }

        if (ret < 0) {
            vtx_log_error("Failed to read frame: %d", ret);
            break;
        }

        /* 仅处理视频流 */
        if (pkt->stream_index != ffmpeg.video_stream_idx) {
            av_packet_unref(pkt);
            continue;
        }

        /* 确定帧类型 */
        vtx_frame_type_t frame_type = VTX_FRAME_P;
        if (pkt->flags & AV_PKT_FLAG_KEY) {
            frame_type = VTX_FRAME_I;
        }

        /* 发送帧 */
        if (g_streaming) {
            send_media_frame(tx, pkt, frame_type);

            ffmpeg.frame_count++;
            if (ffmpeg.frame_count % 30 == 0) {
                vtx_log_info("Sent %lld frames", (long long)ffmpeg.frame_count);
            }
        }

        av_packet_unref(pkt);

        /* 按帧率延迟 */
        usleep(frame_interval_ms * 1000);
    }

    av_packet_free(&pkt);
    cleanup_ffmpeg(&ffmpeg);

    vtx_log_info("Media thread stopped");
    return NULL;
}

/* Poll线程 */
static void* poll_thread(void* arg) {
    vtx_tx_t* tx = (vtx_tx_t*)arg;

    vtx_log_info("Poll thread started");

    while (g_running) {
        int ret = vtx_tx_poll(tx, 100);
        if (ret < 0) {
            vtx_log_error("vtx_tx_poll failed: %d", ret);
            break;
        }
    }

    vtx_log_info("Poll thread stopped");
    return NULL;
}

int main(int argc, char* argv[]) {
    int ret;
    pthread_t poll_tid;

    /* 解析命令行 */
    const char* bind_addr = "0.0.0.0";
    uint16_t bind_port = 8888;

    if (argc >= 2) {
        bind_port = (uint16_t)atoi(argv[1]);
    }

    vtx_log_info("=== VTX Server ===");
    vtx_log_info("Binding to %s:%u", bind_addr, bind_port);
    vtx_log_info("Media root directory: %s", g_root_dir);

    /* 注册信号处理 */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* 创建TX配置 */
    vtx_tx_config_t config = {
        .bind_addr = bind_addr,
        .bind_port = bind_port,
        .mtu = VTX_DEFAULT_MTU,
        .send_buf_size = VTX_DEFAULT_SEND_BUF,
        .retrans_timeout_ms = VTX_DEFAULT_RETRANS_TIMEOUT_MS,
        .max_retrans = VTX_DEFAULT_MAX_RETRANS,
        .data_retrans_timeout_ms = VTX_DEFAULT_DATA_RETRANS_TIMEOUT_MS,
        .data_max_retrans = VTX_DEFAULT_MAX_RETRANS,
    };

    /* 创建TX，传入media回调 */
    vtx_tx_t* tx = vtx_tx_create(&config, on_data, on_media, NULL);
    if (!tx) {
        vtx_log_error("Failed to create TX");
        return EXIT_FAILURE;
    }

    /* 设置全局TX对象（供回调使用） */
    g_tx = tx;

    /* 监听 */
    ret = vtx_tx_listen(tx);
    if (ret != VTX_OK) {
        vtx_log_error("Failed to listen: %d", ret);
        vtx_tx_destroy(tx);
        g_tx = NULL;
        return EXIT_FAILURE;
    }

    /* 接受连接 */
    vtx_log_info("Waiting for client...");
    ret = vtx_tx_accept(tx, 0);
    if (ret != VTX_OK) {
        vtx_log_error("Failed to accept: %d", ret);
        vtx_tx_destroy(tx);
        g_tx = NULL;
        return EXIT_FAILURE;
    }

    vtx_log_info("Client connected!");

    /* 创建poll线程 */
    pthread_create(&poll_tid, NULL, poll_thread, tx);

    /* 等待poll线程结束 */
    pthread_join(poll_tid, NULL);

    /* 停止媒体线程（如果正在运行） */
    if (g_streaming && g_media_tid != 0) {
        vtx_log_info("Stopping media thread...");
        g_streaming = 0;
        pthread_join(g_media_tid, NULL);
        g_media_tid = 0;
    }

    /* 清理 */
    vtx_tx_close(tx);
    vtx_tx_destroy(tx);
    g_tx = NULL;

    vtx_log_info("Server stopped");

    return EXIT_SUCCESS;
}
