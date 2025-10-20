# VTX 快速开始指南

## 5分钟上手

### 1. 构建项目

```bash
cd vtx.v2
mkdir -p build && cd build
cmake ..
make
```

结果：
- `libvtx.a` - 静态库（100KB）
- `test_basic` - 测试程序（78KB）

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
int on_data(vtx_ctrl_type_t type, const uint8_t* data,
            size_t size, void* userdata) {
    if (type == VTX_CTRL_DATA) {
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
gcc -o server server.c -I../include -L. -lvtx -lpthread
gcc -o client client.c -I../include -L. -lvtx -lpthread
```

### 4. 运行

```bash
# 终端1
./server

# 终端2
./client
```

## API速查

### TX API

| 函数 | 说明 |
|------|------|
| `vtx_tx_create()` | 创建发送端 |
| `vtx_tx_listen()` | 开始监听 |
| `vtx_tx_accept()` | 接受连接（阻塞） |
| `vtx_tx_poll()` | 轮询事件（处理ACK等） |
| `vtx_tx_send()` | 发送数据（可靠传输） |
| `vtx_tx_get_stats()` | 获取统计信息 |
| `vtx_tx_close()` | 关闭连接 |
| `vtx_tx_destroy()` | 销毁发送端 |

### RX API

| 函数 | 说明 |
|------|------|
| `vtx_rx_create()` | 创建接收端 |
| `vtx_rx_connect()` | 连接到服务器 |
| `vtx_rx_poll()` | 轮询事件（接收数据） |
| `vtx_rx_send()` | 发送数据（可靠传输） |
| `vtx_rx_get_stats()` | 获取统计信息 |
| `vtx_rx_close()` | 关闭连接 |
| `vtx_rx_destroy()` | 销毁接收端 |

### 回调函数

```c
// 接收帧回调（RX）
typedef int (*vtx_on_frame_fn)(
    const uint8_t* frame_data,
    size_t frame_size,
    vtx_frame_type_t frame_type,
    void* userdata);

// 控制帧回调（TX/RX）
typedef int (*vtx_on_ctrl_fn)(
    vtx_ctrl_type_t ctrl_type,
    const uint8_t* data,
    size_t size,
    void* userdata);

// 连接事件回调（RX）
typedef void (*vtx_on_connect_fn)(
    bool connected,
    void* userdata);
```

## 常见配置

### 默认值

```c
#define VTX_DEFAULT_MTU           1400   // MTU大小
#define VTX_DEFAULT_SEND_BUF      (2MB)  // 发送缓冲
#define VTX_DEFAULT_RECV_BUF      (2MB)  // 接收缓冲
#define VTX_DEFAULT_RETRANS_TIMEOUT_MS  5   // I帧重传超时
#define VTX_DEFAULT_MAX_RETRANS   3      // 最大重传次数
#define VTX_DEFAULT_FRAME_TIMEOUT_MS 100 // 帧超时
```

### 自定义配置

```c
vtx_tx_config_t config = {
    .bind_addr = "0.0.0.0",
    .bind_port = 8888,
    .mtu = 1200,                    // 减小MTU
    .retrans_timeout_ms = 10,        // 增加重传超时
    .max_retrans = 5,                // 增加重传次数
};
```

## 错误处理

```c
int ret = vtx_tx_send(tx, data, size);
if (ret != VTX_OK) {
    fprintf(stderr, "Send failed: %s\n", vtx_strerror(ret));
}
```

常见错误码：
- `VTX_OK` (0) - 成功
- `VTX_ERR_INVALID_PARAM` (-1) - 参数错误
- `VTX_ERR_NO_MEMORY` (-2) - 内存不足
- `VTX_ERR_TIMEOUT` (-6) - 超时
- `VTX_ERR_NOT_READY` (-16) - 未就绪

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

## 更多文档

- [README.md](README.md) - 完整项目说明
- [STATUS.md](STATUS.md) - 实现状态
- [CLAUDE.md](CLAUDE.md) - 开发指南
- 头文件注释 - 详细API文档

## 支持

问题反馈: [GitHub Issues](https://github.com/your-repo/vtx/issues)

---

**Happy Coding with VTX! 🚀**
