cmake_minimum_required(VERSION 3.10)

# 版本管理：x.y.z 格式
# x = 架构主版本（固定为2）
# y = 特性版本（固定为0）
# z = 构建版本（每次构建自动递增）
set(VTX_VERSION_MAJOR 2)
set(VTX_VERSION_MINOR 0)

# 读取构建版本号
set(VERSION_FILE "${CMAKE_CURRENT_SOURCE_DIR}/version.txt")
if(EXISTS "${VERSION_FILE}")
    file(READ "${VERSION_FILE}" VTX_VERSION_BUILD)
    string(STRIP "${VTX_VERSION_BUILD}" VTX_VERSION_BUILD)
else()
    set(VTX_VERSION_BUILD 0)
endif()

# 递增构建版本号
math(EXPR VTX_VERSION_BUILD "${VTX_VERSION_BUILD} + 1")

# 写回版本文件
file(WRITE "${VERSION_FILE}" "${VTX_VERSION_BUILD}")

# 完整版本字符串
set(VTX_VERSION "${VTX_VERSION_MAJOR}.${VTX_VERSION_MINOR}.${VTX_VERSION_BUILD}")

project(VTX VERSION ${VTX_VERSION} LANGUAGES C)

# C标准
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

# 编译选项
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_definitions(-DVTX_DEBUG)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -O0 -Wall -Wextra")
else()
    # 临时启用DEBUG日志
    add_definitions(-DVTX_DEBUG)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O0 -g -Wall")
endif()

# 版本号定义
add_definitions(
    -DVTX_VERSION_MAJOR=${VTX_VERSION_MAJOR}
    -DVTX_VERSION_MINOR=${VTX_VERSION_MINOR}
    -DVTX_VERSION_BUILD=${VTX_VERSION_BUILD}
)

# 输出目录设置（所有输出都在build目录内）
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)  # 静态库输出目录
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)  # 动态库输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)  # 可执行文件输出目录

# 包含目录
include_directories(${PROJECT_SOURCE_DIR}/include)

# 源文件列表
set(VTX_SOURCES
    src/vtx_packet.c
    src/vtx_frame.c
    src/vtx_tx.c
    src/vtx_rx.c
    src/vtx_error.c
    src/vtx_mem.c
    src/vtx.c
)

# 静态库
add_library(vtx STATIC ${VTX_SOURCES})

# 平台相关链接库
if(APPLE)
    # macOS无需额外链接
elseif(UNIX)
    target_link_libraries(vtx pthread)
endif()

# 测试程序
add_executable(test_basic tests/test_basic.c)
target_link_libraries(test_basic vtx pthread)

# 示例程序（需要FFmpeg）
find_package(PkgConfig)
if(PkgConfig_FOUND)
    pkg_check_modules(FFMPEG QUIET libavformat libavcodec libavutil)
    if(FFMPEG_FOUND)
        add_executable(server cmd/server.c)
        target_include_directories(server PRIVATE ${FFMPEG_INCLUDE_DIRS})
        target_link_directories(server PRIVATE ${FFMPEG_LIBRARY_DIRS})
        target_link_libraries(server vtx pthread ${FFMPEG_LDFLAGS})
        message(STATUS "Building server with FFmpeg support")
    else()
        message(WARNING "FFmpeg not found, skipping server build")
    endif()
endif()

add_executable(client cmd/client.c)
target_link_libraries(client vtx pthread)

# 安装规则
install(TARGETS vtx DESTINATION lib)
install(DIRECTORY include/ DESTINATION include
        FILES_MATCHING PATTERN "*.h")

# 显示配置信息
message(STATUS "VTX Version: ${PROJECT_VERSION}")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C Compiler: ${CMAKE_C_COMPILER}")
