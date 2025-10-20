# VTX 快速开始指南

> VTX v2.0.5 - 生产就绪的实时视频传输协议库

## 5分钟上手

### 1. 构建项目

```bash
cd vtx.v2
mkdir -p build && cd build
cmake ..
make -j4
```

编译产物位于 `build/` 目录：
- `lib/libvtx.a` - 静态库
- `bin/test_basic` - 基础测试程序
- `bin/server` - 服务端示例（需FFmpeg）
- `bin/client` - 客户端示例（需FFmpeg）

详细构建说明见 [BUILD.md](BUILD.md)

### 2. 最小示例

#### 发送端（TX Server）

```c
#include "vtx.h"
#include <stdio.h>

int main() {
    // 创建TX配置
    vtx_tx_config_t config = {
        .bind_addr = "0.0.0.0",
        .bind_port = 8888,
        .mtu = VTX_DEFAULT_MTU,
    };

    // 创建并启动TX
    vtx_tx_t* tx = vtx_tx_create(&config, NULL, NULL);
    vtx_tx_listen(tx);

    printf("Waiting for client...\n");
    if (vtx_tx_accept(tx, 0) == VTX_OK) {
        printf("Client connected!\n");

        // 发送数据
        const char* msg = "Hello from Server!";
        vtx_tx_send(tx, (uint8_t*)msg, strlen(msg));

        // 轮询处理ACK
        while (running) {
            vtx_tx_poll(tx, 100);
        }
    }

    // 清理
    vtx_tx_close(tx);
    vtx_tx_destroy(tx);
    return 0;
}
```

#### 接收端（RX Client）

```c
#include "vtx.h"
#include <stdio.h>

// 数据回调
int on_data(vtx_data_type_t type, const uint8_t* data,
            size_t size, void* userdata) {
    if (type == VTX_DATA_USER) {
        printf("Received: %.*s\n", (int)size, data);
    }
    return VTX_OK;
}

int main() {
    // 创建RX配置
    vtx_rx_config_t config = {
        .server_addr = "127.0.0.1",
        .server_port = 8888,
        .mtu = VTX_DEFAULT_MTU,
    };

    // 创建并连接RX
    vtx_rx_t* rx = vtx_rx_create(&config, NULL, on_data, NULL, NULL);

    if (vtx_rx_connect(rx) == VTX_OK) {
        printf("Connected to server!\n");

        // 轮询处理接收
        while (running) {
            vtx_rx_poll(rx, 100);
        }
    }

    // 清理
    vtx_rx_close(rx);
    vtx_rx_destroy(rx);
    return 0;
}
```

### 3. 编译你的程序

```bash
# 从build/目录
gcc -o server server.c -I../include -L./lib -lvtx -lpthread
gcc -o client client.c -I../include -L./lib -lvtx -lpthread
```

### 4. 运行

```bash
# 终端1 - 运行示例服务端
./build/bin/server 8888

# 终端2 - 运行示例客户端
./build/bin/client 127.0.0.1 8888

# 或运行基础测试
./build/bin/test_basic
```

## API速查

### TX API（发送端）

| 函数 | 说明 |
|------|------|
| `vtx_tx_create()` | 创建发送端 |
| `vtx_tx_listen()` | 开始监听 |
| `vtx_tx_accept()` | 接受连接（带超时） |
| `vtx_tx_poll()` | 轮询事件（处理ACK、心跳等） |
| `vtx_tx_send()` | 发送用户数据（可靠传输） |
| `vtx_tx_alloc_media_frame()` | 分配媒体帧 |
| `vtx_tx_send_media()` | 发送媒体帧（自动分片） |
| `vtx_tx_free_frame()` | 释放帧 |
| `vtx_tx_get_stats()` | 获取统计信息 |
| `vtx_tx_close()` | 关闭连接 |
| `vtx_tx_destroy()` | 销毁发送端 |

### RX API（接收端）

| 函数 | 说明 |
|------|------|
| `vtx_rx_create()` | 创建接收端 |
| `vtx_rx_connect()` | 连接到服务器（3-way handshake） |
| `vtx_rx_poll()` | 轮询事件（接收数据、心跳） |
| `vtx_rx_send()` | 发送用户数据（可靠传输） |
| `vtx_rx_start()` | 发送START控制命令 |
| `vtx_rx_stop()` | 发送STOP控制命令 |
| `vtx_rx_get_stats()` | 获取统计信息 |
| `vtx_rx_close()` | 关闭连接 |
| `vtx_rx_destroy()` | 销毁接收端 |

### 回调函数

```c
// 媒体帧回调（RX）
typedef int (*vtx_on_frame_fn)(
    const uint8_t* frame_data,
    size_t frame_size,
    vtx_frame_type_t frame_type,
    void* userdata);

// 数据帧回调（TX/RX）
typedef int (*vtx_on_data_fn)(
    vtx_data_type_t data_type,
    const uint8_t* data,
    size_t size,
    void* userdata);

// 连接事件回调（RX）
typedef void (*vtx_on_connect_fn)(
    bool connected,
    void* userdata);

// 媒体控制回调（TX）
typedef void (*vtx_on_media_fn)(
    vtx_data_type_t data_type,
    const char* url,
    void* userdata);
```

## 常见配置

### 默认值

```c
#define VTX_DEFAULT_MTU           1400        // MTU大小
#define VTX_DEFAULT_SEND_BUF      (2*1024*1024)  // 发送缓冲2MB
#define VTX_DEFAULT_RECV_BUF      (2*1024*1024)  // 接收缓冲2MB
#define VTX_DEFAULT_RETRANS_TIMEOUT_MS  5     // I帧重传超时
#define VTX_DEFAULT_MAX_RETRANS   3           // 最大重传次数
#define VTX_DEFAULT_FRAME_TIMEOUT_MS 100      // 帧重组超时
#define VTX_DEFAULT_DATA_RETRANS_TIMEOUT_MS 30  // 数据包重传超时
#define VTX_DEFAULT_HEARTBEAT_INTERVAL_MS 60000 // 心跳间隔60秒
#define VTX_DEFAULT_HEARTBEAT_MAX_MISS 3      // 最大丢失心跳次数
```

### TX端自定义配置

```c
vtx_tx_config_t config = {
    .bind_addr = "0.0.0.0",
    .bind_port = 8888,
    .mtu = 1200,                    // 减小MTU
    .retrans_timeout_ms = 10,        // 增加I帧重传超时
    .max_retrans = 5,                // 增加I帧重传次数
    .data_retrans_timeout_ms = 50,   // 增加数据包重传超时
    .data_max_retrans = 5,           // 增加数据包重传次数
    .heartbeat_interval_ms = 30000,  // 心跳间隔30秒
    .heartbeat_max_miss = 3,         // 最大丢失3个心跳
};
```

### RX端自定义配置

```c
vtx_rx_config_t config = {
    .server_addr = "127.0.0.1",
    .server_port = 8888,
    .mtu = 1200,                    // 减小MTU
    .frame_timeout_ms = 200,         // 增加帧重组超时
    .data_retrans_timeout_ms = 50,   // 增加数据包重传超时
    .data_max_retrans = 5,           // 增加数据包重传次数
    .heartbeat_interval_ms = 30000,  // 心跳发送间隔30秒
};
```

## 错误处理

```c
int ret = vtx_tx_send(tx, data, size);
if (ret != VTX_OK) {
    fprintf(stderr, "Send failed: %s\n", vtx_strerror(ret));
}

// 检查是否为错误码
if (vtx_is_error(ret)) {
    // 处理错误
}
```

常见错误码（0x8xxx格式）：
- `VTX_OK` (0) - 成功
- `VTX_ERR_INVALID_PARAM` (0x8001) - 无效参数
- `VTX_ERR_NO_MEMORY` (0x8002) - 内存不足
- `VTX_ERR_TIMEOUT` (0x8006) - 超时
- `VTX_ERR_NOT_READY` (0x8007) - 未就绪
- `VTX_ERR_DISCONNECTED` (0x800F) - 连接断开
- `VTX_ERR_CHECKSUM` (0x8014) - 校验错误

## 调试技巧

### 启用DEBUG模式

```bash
cmake -DCMAKE_BUILD_TYPE=Debug ..
make
```

DEBUG模式会：
- 启用详细日志
- 添加时间戳到包头
- 记录延迟信息

### 查看日志

日志级别：
- `[DEBUG]` - 调试信息
- `[INFO]` - 一般信息
- `[WARN]` - 警告
- `[ERROR]` - 错误

### 查看统计

```c
vtx_tx_stats_t stats;
vtx_tx_get_stats(tx, &stats);

printf("TX: packets=%llu bytes=%llu retrans=%llu\n",
       stats.total_packets,
       stats.total_bytes,
       stats.retrans_packets);
```

## 线程模型

推荐使用独立的poll线程：

```c
// Poll线程
void* poll_thread(void* arg) {
    vtx_tx_t* tx = (vtx_tx_t*)arg;
    while (running) {
        vtx_tx_poll(tx, 100);  // 100ms超时
    }
    return NULL;
}

// 主线程
pthread_t tid;
pthread_create(&tid, NULL, poll_thread, tx);

// 发送数据
vtx_tx_send(tx, data, size);

// 等待线程结束
pthread_join(tid, NULL);
```

## 性能优化提示

1. **MTU调整**: 根据网络环境调整MTU（1200-1400）
2. **缓冲区大小**: 高带宽场景增大缓冲区
3. **重传参数**: 低延迟要求降低重传超时
4. **内存池**: 系统会自动扩展，无需担心
5. **零拷贝**: 使用引用计数，避免数据拷贝

## 协议流程

### 连接建立（3-Way Handshake）

```
RX                          TX
 |                           |
 |-------- CONNECT --------->|
 |                           |
 |<------- CONNECTED --------|  (重传直到ACK或超时)
 |                           |
 |--------- ACK ------------>|
 |                           |
 [连接建立完成]
```

### 心跳机制

```
RX每60秒发送HEARTBEAT → TX响应ACK
TX检测180秒内未收到心跳 → 自动断开连接
```

### 数据传输

```
I帧：分片级重传，最多重传3次，超时5ms
P帧：丢失不重传，直接丢弃
用户数据：可靠传输，最多重传3次，超时30ms
```

## v2.0.5 新特性

- ✅ 线程安全增强（server.c mutex保护）
- ✅ URL解析边界检查
- ✅ 帧重组超时机制（100ms默认）
- ✅ 可配置的重传参数
- ✅ 心跳机制（连接保活和超时检测）
- ✅ 优化的构建目录结构（bin/和lib/）

## 更多文档

- [README.md](README.md) - 完整项目说明
- [BUILD.md](BUILD.md) - 构建指南
- [STATUS.md](STATUS.md) - 实现状态和版本历史
- [CLAUDE.md](CLAUDE.md) - 完整开发指南
- 头文件注释 - 详细API文档

## 支持

问题反馈: [GitHub Issues](https://github.com/ArdKit/ArdKit-VTX/issues)

---

**当前版本**: v2.0.5
**发布日期**: 2025-10-20
**状态**: ✅ 生产就绪 (Production Ready)

**Happy Coding with VTX! 🚀**
