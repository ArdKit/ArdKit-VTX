# VTX 构建说明

## 版本信息
- **当前版本**: v2.0.5
- **最后更新**: 2025-10-20

## 构建系统设计

所有构建相关的文件和输出都在 `build/` 目录中，保持项目根目录干净。

## 快速构建

```bash
# 从项目根目录
mkdir -p build && cd build
cmake ..
make -j4
```

## 详细构建步骤

### 1. 创建构建目录并配置

```bash
mkdir -p build
cd build
cmake ..
```

### 2. 编译

```bash
# 使用多线程编译加速
make -j4

# 或单线程编译
make
```

### 3. 编译产物

编译成功后，产物位于：

- **静态库**: `build/lib/libvtx.a`
- **可执行文件**: `build/bin/`
  - `test_basic` - 基础测试程序
  - `server` - 服务端示例（需FFmpeg）
  - `client` - 客户端示例（需FFmpeg）

### 4. 运行示例

```bash
# 从项目根目录运行
./build/bin/test_basic

# 或进入build目录
cd build

# 运行服务端（需要测试视频文件）
./bin/server 8888

# 运行客户端（另一个终端）
./bin/client 127.0.0.1 8888
```

## 构建选项

### Release构建（默认）

```bash
cmake -DCMAKE_BUILD_TYPE=Release ..
make
```

特点：
- 优化级别：-O2
- 无调试符号
- 包头大小：14字节
- 性能最优

### Debug构建

```bash
cmake -DCMAKE_BUILD_TYPE=Debug ..
make
```

特点：
- 包含调试符号（-g）
- 无优化（-O0）
- 包头增加8字节时间戳（总共22字节）
- 启用详细日志
- 支持延迟统计

## 清理构建

```bash
# 完全清理
rm -rf build/

# 重新构建
mkdir -p build && cd build && cmake .. && make -j4
```

## 目录结构

```
vtx.v2/
├── build/              # 构建目录（.gitignore中）
│   ├── bin/           # 可执行文件输出目录
│   │   ├── test_basic
│   │   ├── server
│   │   └── client
│   ├── lib/           # 库文件输出目录
│   │   └── libvtx.a
│   └── CMakeFiles/    # CMake生成的构建文件
├── examples/          # 示例程序源码
│   ├── server.c
│   └── client.c
├── include/           # 公共头文件
│   ├── vtx.h         # 主API
│   ├── vtx_types.h   # 类型定义
│   ├── vtx_error.h   # 错误码
│   ├── vtx_frame.h   # 帧管理
│   ├── vtx_packet.h  # 数据包
│   └── ...
├── src/               # 库源码实现
│   ├── vtx.c
│   ├── vtx_tx.c
│   ├── vtx_rx.c
│   ├── vtx_frame.c
│   └── vtx_packet.c
├── tests/             # 测试代码
│   └── test_basic.c
├── data/              # 测试数据目录
│   └── *.mp4         # 测试视频文件
├── CMakeLists.txt     # CMake配置
└── *.md              # 文档
```

## 依赖要求

### 编译时依赖

- **CMake**: 3.10 或更高版本
- **C编译器**: 支持C17标准
  - GCC 7.0+
  - Clang 10.0+
  - Apple Clang 12.0+

### 链接时依赖

- **pthread**: POSIX线程库（通常系统自带）
- **系统库**:
  - macOS: libkern (用于字节序转换)
  - Linux: 标准C库

### 示例程序额外依赖（可选）

- **FFmpeg**: 用于server和client示例
  - libavformat
  - libavcodec
  - libavutil
  - libswscale

如果没有FFmpeg，仍可编译核心库libvtx.a和test_basic。

## 编译配置

### 包头大小

| 构建类型 | 包头大小 | 说明 |
|---------|---------|------|
| Release | 14字节 | 标准模式，性能最优 |
| Debug | 22字节 | 增加8字节时间戳用于延迟测量 |

### 内存配置

默认配置（可在vtx_types.h中修改）：

```c
#define VTX_DEFAULT_MTU           1400        // MTU大小
#define VTX_MAX_FRAME_SIZE        (512*1024)  // 最大帧512KB
#define VTX_DEFAULT_SEND_BUF      (2*1024*1024)  // 2MB发送缓冲
#define VTX_DEFAULT_RECV_BUF      (2*1024*1024)  // 2MB接收缓冲
```

## 常见问题

### Q: 编译时提示找不到FFmpeg？

A: 示例程序需要FFmpeg。解决方法：
```bash
# macOS
brew install ffmpeg

# Ubuntu/Debian
sudo apt-get install libavformat-dev libavcodec-dev libavutil-dev
```

或者只编译核心库：
```bash
# 仅编译libvtx.a和test_basic
cmake .. -DBUILD_EXAMPLES=OFF
make
```

### Q: 如何查看编译详细输出？

A:
```bash
make VERBOSE=1
```

### Q: 如何安装到系统？

A:
```bash
sudo make install
```

默认安装路径：
- 头文件: `/usr/local/include/vtx/`
- 库文件: `/usr/local/lib/libvtx.a`

### Q: 如何交叉编译？

A:
```bash
# 例如为ARM64编译
cmake -DCMAKE_TOOLCHAIN_FILE=arm64-toolchain.cmake ..
make
```

## 性能优化编译

```bash
# 启用额外优化
cmake -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_C_FLAGS="-O3 -march=native" ..
make
```

## 验证构建

```bash
# 1. 检查库文件
ls -lh build/lib/libvtx.a

# 2. 运行基本测试
./build/bin/test_basic

# 3. 查看符号表
nm build/lib/libvtx.a | grep vtx_
```

## 代码统计

```bash
# 统计代码行数
find src include -name "*.c" -o -name "*.h" | xargs wc -l
```

当前统计（v2.0.5）：
- 源文件：~2600行
- 头文件：~900行
- 总计：~3500行

## 持续集成

示例GitHub Actions配置：

```yaml
- name: Build VTX
  run: |
    mkdir build && cd build
    cmake -DCMAKE_BUILD_TYPE=Release ..
    make -j4

- name: Run Tests
  run: |
    ./build/bin/test_basic
```

## 更新记录

### v2.0.5 (2025-10-20)
- ✅ 修复server.c线程竞态条件
- ✅ 增强URL解析边界检查
- ✅ 添加帧重组超时机制
- ✅ 重构魔数为常量定义
- ✅ 优化构建输出目录结构（bin/和lib/）

### v2.0.4
- 基础功能实现

---

**构建成功后，请参考 [QUICKSTART.md](QUICKSTART.md) 开始使用VTX！**
