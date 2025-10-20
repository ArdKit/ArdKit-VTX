# VTX - Video Transmission over eXtended UDP

简体中文 | [English](../README.md)

[![Version](https://img.shields.io/badge/version-2.0.5-blue.svg)](https://github.com/xbitsmaster/vtx)
[![Build](https://img.shields.io/badge/build-passing-brightgreen.svg)](BUILD.md)
[![License](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)

VTX是一个基于UDP的实时视频传输协议库，专为低延迟、高可靠性的视频传输场景设计。

## 核心特性

- ✅ **低延迟**: 端到端延迟 < 50ms（目标）
- ✅ **选择性重传**: I帧分片级重传，P帧丢弃策略
- ✅ **零拷贝设计**: iovec + 引用计数，最小化内存拷贝
- ✅ **内存池化**: 预分配frame对象，避免频繁分配
- ✅ **无锁设计**: 原子操作 + spinlock
- ✅ **跨平台**: 支持 Linux 和 macOS
- ✅ **线程安全**: 完善的并发控制机制
- ✅ **生产就绪**: 5000+行高质量代码

## 快速开始

### 1. 构建

```bash
mkdir -p build && cd build
cmake ..
make -j4
```

生成文件（位于`build/`目录）：
- `lib/libvtx.a` - 静态库
- `bin/test_basic` - 基础测试程序
- `bin/server` - 服务端示例（需FFmpeg）
- `bin/client` - 客户端示例（需FFmpeg）

详细构建说明见 [BUILD.md](BUILD.md)

### API 示例

#### 发送端 (TX)

```c
#include "vtx.h"

// 1. 创建配置
vtx_tx_config_t config = {
    .bind_addr = "0.0.0.0",
    .bind_port = 8888,
    .mtu = VTX_DEFAULT_MTU,
};

// 2. 创建TX
vtx_tx_t* tx = vtx_tx_create(&config, NULL, NULL);

// 3. 监听端口
vtx_tx_listen(tx);

// 4. 接受连接
vtx_tx_accept(tx, 5000);  // 5秒超时

// 5. 发送数据
const char* data = "Hello VTX!";
vtx_tx_send(tx, (uint8_t*)data, strlen(data));

// 6. 清理
vtx_tx_close(tx);
vtx_tx_destroy(tx);
```

#### 接收端 (RX)

```c
#include "vtx.h"

// 帧回调
static int on_frame(const uint8_t* data, size_t size,
                    vtx_frame_type_t type, void* userdata) {
    printf("Received frame: type=%d size=%zu\n", type, size);
    return VTX_OK;
}

int main() {
    // 1. 创建配置
    vtx_rx_config_t config = {
        .server_addr = "127.0.0.1",
        .server_port = 8888,
        .mtu = VTX_DEFAULT_MTU,
    };

    // 2. 创建RX
    vtx_rx_t* rx = vtx_rx_create(&config, on_frame, NULL, NULL, NULL);

    // 3. 连接到服务器
    vtx_rx_connect(rx);

    // 4. 轮询事件
    while (running) {
        vtx_rx_poll(rx, 100);  // 100ms超时
    }

    // 5. 清理
    vtx_rx_close(rx);
    vtx_rx_destroy(rx);
}
```

## 架构

```
vtx.v2/
├── include/          # 公共头文件
│   ├── vtx.h        # 主API
│   ├── vtx_types.h  # 数据类型
│   ├── vtx_packet.h # 数据包处理
│   ├── vtx_frame.h  # 帧管理
│   └── ...
├── src/             # 源代码实现
│   ├── vtx_packet.c # 数据包处理
│   ├── vtx_frame.c  # 帧与内存池
│   ├── vtx_tx.c     # 发送端
│   ├── vtx_rx.c     # 接收端
│   └── vtx.c        # 主API
├── tests/           # 测试程序
│   └── test_basic.c
├── examples/        # 示例程序（需要FFmpeg）
│   ├── server.c
│   └── client.c
├── build/           # 构建目录
└── CMakeLists.txt   # CMake配置
```

## 核心模块

### 1. vtx_packet - 数据包处理
- CRC-16-CCITT校验
- 数据包序列化/反序列化
- 网络字节序转换

### 2. vtx_frame - 帧管理
- 内存池（媒体帧512KB，控制帧128B）
- 原子引用计数
- 分片跟踪和重组
- 超时处理

### 3. vtx_tx - 发送端
- UDP socket管理
- 连接管理（listen/accept）
- 零拷贝发送
- ACK处理

### 4. vtx_rx - 接收端
- UDP socket管理
- 连接管理（connect）
- 帧重组
- ACK发送

## 协议设计

### 数据包格式

```
+----------+----------+-------+-------+----------+-------------+
| seq_num  | frame_id | type  | flags | frag_idx | total_frags |
|  (4B)    |  (2B)    | (1B)  | (1B)  |  (2B)    |    (2B)     |
+----------+----------+-------+-------+----------+-------------+
| payload_size | checksum | [timestamp] |    payload ...       |
|     (2B)     |   (2B)   |   (8B)     |                       |
+--------------+----------+-------------+-----------------------+
```

- **Release模式**: 16字节头部
- **Debug模式**: 24字节头部（增加8字节时间戳）

### 帧类型

- `VTX_FRAME_I` - I帧（关键帧，需要重传）
- `VTX_FRAME_P` - P帧（预测帧，丢失不重传）
- `VTX_FRAME_SPS/PPS` - H.264参数集
- `VTX_FRAME_A` - 音频帧
- `VTX_CTRL_*` - 控制帧（CONNECT, DISCONNECT, ACK, DATA等）

## 性能特性

- **MTU**: 默认1400字节（可配置）
- **I帧重传超时**: 5ms（可配置）
- **DATA重传超时**: 30ms（可配置）
- **最大重传次数**: 3次（可配置）
- **帧超时**: 100ms（可配置）

## 配置选项

### TX配置

```c
typedef struct {
    const char* bind_addr;           // 绑定地址
    uint16_t    bind_port;           // 绑定端口
    uint16_t    mtu;                 // MTU大小
    uint32_t    send_buf_size;       // 发送缓冲区大小
    uint32_t    retrans_timeout_ms;  // I帧重传超时
    uint8_t     max_retrans;         // 最大重传次数
    uint32_t    data_retrans_timeout_ms; // DATA重传超时
    uint8_t     data_max_retrans;    // DATA最大重传次数
} vtx_tx_config_t;
```

### RX配置

```c
typedef struct {
    const char* server_addr;             // 服务器地址
    uint16_t    server_port;             // 服务器端口
    uint16_t    mtu;                     // MTU大小
    uint32_t    recv_buf_size;           // 接收缓冲区大小
    uint32_t    frame_timeout_ms;        // 帧接收超时（默认100ms）
    uint32_t    data_retrans_timeout_ms; // DATA包重传超时（默认30ms）
    uint8_t     data_max_retrans;        // DATA包最大重传次数（默认3次）
    uint32_t    heartbeat_interval_ms;   // 心跳发送间隔（默认60秒）
} vtx_rx_config_t;
```

## 统计信息

### TX统计

```c
typedef struct {
    uint64_t total_frames;       // 总发送帧数
    uint64_t total_i_frames;     // I帧数量
    uint64_t total_p_frames;     // P帧数量
    uint64_t total_packets;      // 总发送包数
    uint64_t total_bytes;        // 总发送字节数
    uint64_t retrans_packets;    // 重传包数
    uint64_t retrans_bytes;      // 重传字节数
    // ...
} vtx_tx_stats_t;
```

### RX统计

```c
typedef struct {
    uint64_t total_frames;       // 总接收帧数
    uint64_t total_packets;      // 总接收包数
    uint64_t total_bytes;        // 总接收字节数
    uint64_t lost_packets;       // 丢失包数
    uint64_t dup_packets;        // 重复包数
    uint64_t incomplete_frames;  // 不完整帧数
    // ...
} vtx_rx_stats_t;
```

## 错误码

```c
#define VTX_OK                  0    // 成功
#define VTX_ERR_INVALID_PARAM  -1    // 无效参数
#define VTX_ERR_NO_MEMORY      -2    // 内存不足
#define VTX_ERR_TIMEOUT        -6    // 超时
#define VTX_ERR_CHECKSUM       -20   // 校验错误
#define VTX_ERR_NETWORK        -100  // 网络错误
// ...更多错误码见 vtx_error.h
```

## 调试

启用DEBUG模式：

```bash
cmake -DCMAKE_BUILD_TYPE=Debug ..
make
```

DEBUG模式特性：
- 包头增加8字节时间戳
- 启用详细日志输出
- 延迟统计
- 可选的丢包模拟

## 依赖

### 编译依赖
- CMake 3.10+
- C17编译器（GCC 7.0+ / Clang 10.0+）

### 运行依赖
- POSIX pthread（线程）
- POSIX socket（网络）
- macOS: libkern/OSByteOrder.h
- Linux: endian.h

### 示例程序依赖（可选）
- FFmpeg (libavformat, libavcodec, libavutil, libswscale)

## 许可证

MIT License

## 文档

- [BUILD.md](BUILD.md) - 构建指南
- [QUICKSTART.md](QUICKSTART.md) - 快速开始指南
- [STATUS.md](STATUS.md) - 实现状态与版本历史
- [CLAUDE.md](CLAUDE.md) - 完整开发指南
- 头文件注释 - 详细API文档

## 更新日志

### v2.0.5 (2025-10-20) - 当前版本
- ✅ 修复server.c线程竞态条件
- ✅ 增强URL解析边界检查
- ✅ 帧重组超时机制（已验证）
- ✅ 重构魔数为常量定义
- ✅ 优化构建输出目录结构

### v2.0.4
- 基础功能实现
- 3-way handshake
- 分片级重传
- 心跳机制

详见 [STATUS.md](STATUS.md)

## 作者

VTX开发团队

## 许可证

MIT License - 详见 [LICENSE](LICENSE) 文件

## 贡献

欢迎提交Issue和Pull Request！

## 版本

**当前版本**: v2.0.5
**发布日期**: 2025-10-20
**状态**: ✅ 生产就绪 (Production Ready)
