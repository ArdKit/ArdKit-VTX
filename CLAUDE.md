# VTX 项目开发指南

> 本文档为AI助手和开发者提供VTX项目的全面指导

---

## 1. 项目概述

### 1.1 项目简介

**VTX (Video Transmission over eXtended UDP)** 是一个基于UDP的实时视频传输协议库，专为低延迟、高可靠性的视频传输场景设计。

**核心特点**：
- 基于UDP的私有协议 + 轻量级ARQ + 自适应FEC；
- 选择性重传的ARQ机制。只对极其关键的数据进行有限次数的快速重传（I帧重传，P帧丢弃，其它控制帧重传）；
    - 发送端只缓存最后一个I帧，当有新的I帧到来时，丢弃前一个I帧的分片，开始缓存新的I帧的分片，
    - 接收端收到I帧后，丢弃前一个I帧，不管是否需要重传；
    - 控制帧有最高优先级；
    - 自适应FEC算法，根据丢包率动态调整FEC编码参数；
- 零拷贝设计，高效内存管理，无锁设计；
    - 采用帧数据包引用计数，分片直接计算不缓存，避免数据拷贝；
    - 采集帧数据包预分配，避免频繁内存分配；帧数据包分为控制帧包（小于128字节）和视频帧包（MTU - 固定长度帧头）
    - 原子引用计数，无锁设计，避免竞态条件，spinlock实现不得不用锁的地方；
- 跨平台支持（Linux/macOS）

### 1.2 应用场景

主要应用于对实时性要求高的场景：
- 无人机图传系统（端到端延迟 < 50ms）
- 实时监控
- 远程控制
- 视频会议

### 1.3 核心优势

相对于RTP/RTSP的优势：

| 特性 | VTX | RTP/RTSP |
|------|-----|----------|
| 帧类型感知 | ✅ I/P帧差异化处理 | ❌ 不区分帧类型 |
| 重传机制 | ✅ I帧快速重传 | ❌ 无重传或反馈慢 |
| 延迟控制 | ✅ 避免延迟累积 | ❌ 缓冲机制累积延迟 |
| 协议开销 | ✅ 精简14字节头 | ❌ 多层协议开销大 |

---

## 2. 核心设计理念

### 2.1 设计目标

1. **低延迟**：端到端 < 50ms（理想），< 80ms（可接受）
2. **实时性优先**：宁可丢帧也不能累积延迟
3. **关键帧保护**：I帧丢包重传，但时延到下一个I帧时则自动放弃重传。音频帧丢包不重传。
4. **可靠传输**：非流媒体数据（如控制帧）可靠传输。
4. **高效实现**：零拷贝，内存池化，无锁设计

### 2.2 关键设计决策

#### 2.2.1 选择性重传策略

```
I帧（关键帧）：
  - 必须重传，分片最多重传3次
  - 缓存最后一个I帧用于重传
  - 仅重传丢失的分片（而非整帧）

P帧（预测帧）：
  - 丢失不重传，直接丢弃
  - 避免延迟累积
  - 下一个I帧自动修复
```

#### 2.2.2 零延迟累积机制

```
重传期间的处理：
1. I帧重传请求发出
2. 发送端继续读取新帧（阻塞式）
3. 如果应用层未及时读取，新帧被编码器丢弃
4. 重传完成后，下一帧是最新的实时帧
5. 结果：重传延迟不会累积到后续帧
```

#### 2.2.3 零拷贝优化

```
发送端：
  应用层编码数据 → 分片（引用指针）→ sendmsg(iovec) → 网络

接收端：
  网络 → recvmsg(pkg缓冲) → 帧缓冲（引用pkg）→ 回调（指针）→ 应用层

关键：全程仅在pkg池和应用层之间传递指针，无数据拷贝
```

---

## 3. 技术栈

### 3.1 编程语言和标准

- **语言**: C (C17标准)
- **编译器**: GCC 或 Clang/LLVM
- **最小版本**: GCC 7.0+ / Clang 10.0+

### 3.2 依赖库

#### 核心依赖，仅测试使用
- **FFmpeg** (libavformat, libavcodec, libavutil, libswscale)
  - 用于示例程序的视频编解码
  - 库本身不依赖FFmpeg

#### 系统依赖
- POSIX pthread（线程）
- POSIX socket（网络）
- macOS: os_unfair_lock
- Linux: pthread_spinlock_t

### 3.3 构建系统

- **CMake** 3.10+
- 支持Debug/Release配置
- VTX_DEBUG宏控制调试特性

### 3.4 平台支持

| 平台 | 架构 | 状态 |
|------|------|------|
| Linux | x86_64, ARM64 | ✅ 完全支持 |
| macOS | x86_64, ARM64 | ✅ 完全支持 |
| Windows | - | ❌ 未测试 |

---

## 4. 项目结构

### 4.1 目录结构

```
```

### 4.2 模块职责

#### 4.2.1 公共API层 (include)

**vtx.h** - 主API
- 发送端：`vtx_tx_create()`, `vtx_tx_listen()`, `vtx_tx_accept`, `vtx_tx_poll`, `vtx_tx_destroy()`, `vtx_tx_send()//发送非媒体流数据`
- 接收端：`vtx_rx_create()`, `vtx_rx_destroy()`, `vtx_rx_connect()`, `vtx_rx_close()`, `vtx_rx_poll`, `vtx_rx_send()//发送非媒体流数据`

**vtx_types.h** - 数据类型
- 媒体帧类型：流媒体帧类似与H.264， H.265编码生成的帧类似一致；
- 媒体帧类型：至少包括：`VTX_FRAME_I`, `VTX_FRAME_P`, `VTX_FRAME_SPS`,`VTX_FRAME_PPS`,`VTX_FRAME_C`
- 数据帧类型：`VTX_CONNECT`, `VTX_DISCONNECT`, `VTX_ACK`, `VTX_HEARTBEAT`, `VTX_DATA`
- 统计结构：`vtx_tx_stats_t`, `vtx_rx_stats_t`

#### 4.2.2 核心实现层 (src/)

**vtx_tx.c** - 发送端
- data发送队列，收到的ACK后丢弃，未收到ACK的data包（ACK更晚发的数据包并且重传后时延大于30ms（可配置）再重传）会重传3次后丢弃
- 音视频流发送队列：I帧缓存，仅缓存最后一帧，分片收到ACK失序后自动重传并计数（间隔时间大于5ms（可配置）），超过3次后不再重传；

**vtx_rx.c** - 接收端
- 收到任意包都ACK，缓存最后一帧I帧，当I帧收全后提交上层，P帧如果未收全而收到更晚的媒体帧则丢弃；

**vtx_packet.c** - 数据包处理
- 序列化/反序列化
- CRC16校验
- 网络字节序转换

**vtx_pool.c** - 内存池
- pkg池：大缓冲区（512KB）池化管理
- 引用计数机制

#### 4.2.3 示例程序 (examples/)

**server.c** - 发送端示例
- 使用FFmpeg读取视频文件
- 循环播放（EOF后回到开头）
- 发送透传帧（<20个/秒）
- 实时统计输出

**client.c** - 接收端示例
- 连接到服务端
- 接收视频帧保存为原始H.264/H.265格式
- 发送透传帧（>10个/秒）
- 实时统计输出

---

## 5. 协议设计

### 5.1 数据包格式

#### 包头结构（14字节 / 22字节）

```c
typedef struct {
    uint32_t seq_num;        // 序列号（全局递增，用于检测丢包）
    uint16_t frame_id;       // 帧ID（同一帧的所有分片共享）
    uint8_t  frame_type;     // 帧类型: I(1)/P(2)/C(3)
    uint8_t  flags;          // 标志位
                             // bit 0: 最后一个分片（用于提前检测帧完整性）
                             // bit 1: 重传标记
                             // bit 2-7: 保留
    uint16_t frag_index;     // 分片索引（从0开始）
    uint16_t total_frags;    // 总分片数
    uint16_t payload_size;   // 本分片载荷大小
    uint16_t checksum;       // CRC16校验（可选）
#ifdef VTX_DEBUG
    uint64_t timestamp_ms;   // 发送时间戳（仅DEBUG模式，用于延迟测量）
#endif
} __attribute__((packed)) vtx_packet_header_t;
```

#### MTU和分片策略

- **MTU**: 默认1400字节（创建时配置，CONNECT时提供rx端的MTU值，使用两者中较小的MTU）
- **头部**: Release 14字节，Debug 22字节
- **载荷**: Release 1386字节，Debug 1378字节
- **最大帧**: 512KB（约381个分片）

### 5.3 连接流程

```
RX流程：
1. vtx_rx_create()   - 创建接收端,注册回调
2. vtx_rx_connect()    - 启动接收/发送线程
3. vtx_rx_send()    - 发送帧数据
4. vtx_rx_close()    - 关闭接收端

TX流程：
1. vtx_tx_create()   - 创建发送端,注册回调
2. vtx_tx_listen()    - 监听端口
3. vtx_tx_accept()    - 接收连接
4. vtx_tx_send()    - 发送帧数据
5. vtx_tx_close()    - 关闭接收端
```

### 零拷贝流程

**发送端**:
```
编码器 → pkg.data (指针)
       ↓
sendmsg(iovec) → pkg.data + offset (零拷贝发送)
```

**接收端**:
```
recvmsg → pkg.data (直接接收到pkg缓冲)
       ↓
回调 → on_frame(pkg.data, ...) (传递指针)
```

### 6.2 错误处理

```c
// 统一错误码
typedef enum {
    VTX_OK = 0,
    VTX_ERR_INVALID_PARAM = -3,
    VTX_ERR_NOMEM = -2,
    VTX_ERR_TIMEOUT = -4,
} vtx_error_t;

// 函数返回值
int vtx_tx_create(...) {
    if (!config) return VTX_ERR_INVALID_PARAM;
    // ...
    return VTX_OK;
}

// 调用者检查
vtx_tx_t* tx = vtx_tx_create(...);
if (!tx) {
    fprintf(stderr, "Failed to create TX\n");
    return -1;
}
```

### 6.3 日志规范

```c
// 使用vtx_log_*宏
vtx_log_error(err_code, "Failed to send packet: %s", strerror(errno));
vtx_log_warn(err_code, "Frame buffer pool exhausted");
vtx_log_info("Connection established");
vtx_log_debug("Received packet: seq=%u", seq_num);
```

### 6.4 注释规范

```c
/**
 * @brief 创建发送端
 *
 * @param config 配置参数
 * @param read_fn 读取帧回调（阻塞式）
 * @param ctrl_fn 控制帧回调
 * @param userdata 用户数据
 * @return vtx_tx_t* 成功返回发送端对象，失败返回NULL
 */
vtx_tx_t* vtx_tx_create(
    const vtx_tx_config_t* config,
    vtx_read_frame_fn read_fn,
    vtx_on_ctrl_fn ctrl_fn,
    void* userdata);

// 重要逻辑添加注释
// 使用MSG_PEEK避免拷贝包头
ssize_t peek_len = recvfrom(sockfd, hdr_buf,
                            VTX_PACKET_HEADER_MAX_SIZE,
                            MSG_PEEK, ...);
```

DEBUG模式特性：
- 包头增加8字节时间戳
- 丢包模拟（可配置丢包率）
- 详细日志输出
- 延迟统计



## 14. 维护指南

### 14.1 版本发布流程

1. **更新版本号**
```bash
# 修改CMakeLists.txt
project(VTX VERSION 1.0.0)
```

2. **更新CHANGELOG**
```markdown
## [1.0.0] - 2025-01-19
### Added
- 初始版本发布
- 支持H.264/H.265编解码
- I帧选择性重传

### Fixed
- 修复packet反序列化bug
```

3. **打包发布**
```bash
# 清理和打包
rm -rf build
tar -czf vtx-1.0.0.tar.gz --exclude=.git --exclude=.claude vtx/
```

### 14.2 贡献指南

1. Fork项目
2. 创建特性分支 (`git checkout -b feature/new-feature`)
3. 提交更改 (`git commit -am 'Add new feature'`)
4. 推送到分支 (`git push origin feature/new-feature`)
5. 创建Pull Request

### 14.3 问题报告

报告bug时请包含：
- 操作系统和版本
- 编译器版本
- 复现步骤
- 错误日志
- 预期行为和实际行为

---

## 15. 许可证

本项目采用Apache License 2.0许可证。详见LICENSE文件。

---

**文档版本**: 1.0
**最后更新**: 2025-01-19
**维护者**: VTX开发团队
**联系方式**: [项目GitHub]
