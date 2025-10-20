# VTX 构建说明

## 构建系统设计

所有构建相关的文件和输出都在 `build/` 目录中，保持项目根目录干净。

## 构建步骤

### 1. 创建构建目录并配置

```bash
mkdir -p build
cd build
cmake ..
```

### 2. 编译

```bash
make -j4
```

### 3. 运行

编译产物位置：
- **可执行文件**: `build/bin/`
  - `test_basic` - 基础测试程序
  - `server` - 服务端示例
  - `client` - 客户端示例
  
- **静态库**: `build/lib/`
  - `libvtx.a` - VTX静态库

运行示例：
```bash
# 从项目根目录运行
./build/bin/test_basic

# 或从build目录运行
cd build
./bin/server
```

## 清理构建

```bash
# 删除所有构建文件
rm -rf build/

# 重新构建
mkdir -p build && cd build && cmake .. && make
```

## 目录结构

```
vtx.v2/
├── build/              # 构建目录（git忽略）
│   ├── bin/           # 可执行文件
│   ├── lib/           # 静态库
│   └── CMakeFiles/    # CMake生成文件
├── cmd/               # 示例程序源码
├── include/           # 头文件
├── src/               # 库源码
├── tests/             # 测试代码
└── CMakeLists.txt     # CMake配置
```

## 构建选项

```bash
# Debug构建
cmake -DCMAKE_BUILD_TYPE=Debug ..
make

# Release构建
cmake -DCMAKE_BUILD_TYPE=Release ..
make
```
