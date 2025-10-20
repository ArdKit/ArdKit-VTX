# VTX 测试视频文件

本目录包含用于VTX协议测试的视频文件。

## 生成的文件

| 文件名 | 编码格式 | 分辨率 | 帧率 | 时长 | I帧间隔 | 文件大小 |
|--------|----------|--------|------|------|---------|----------|
| h264_30fps.mp4 | H.264 (AVC) | 1280x720 | 30fps | 10s | 每30帧 (1秒) | 316KB |
| h265_30fps.mp4 | H.265 (HEVC) | 1280x720 | 30fps | 10s | 每30帧 (1秒) | 123KB |

## 视频特性

### H.264 (h264_30fps.mp4)
- **编码器**: libx264
- **Profile**: Baseline
- **Level**: 3.1
- **像素格式**: YUV420P
- **GOP大小**: 30帧
- **总帧数**: 300帧
  - I帧: 10个（帧号: 1, 31, 61, 91, 121, 151, 181, 211, 241, 271）
  - P帧: 290个
- **码率**: ~257 kbps

### H.265 (h265_30fps.mp4)
- **编码器**: libx265
- **像素格式**: YUV420P
- **GOP大小**: 30帧
- **总帧数**: 300帧
  - I帧: 10个（帧号: 1, 31, 61, 91, 121, 151, 181, 211, 241, 271）
  - P帧: 71个
  - B帧: 219个
- **码率**: ~95 kbps
- **压缩率**: 比H.264节省约61%的空间

## I帧分布验证

两个视频的I帧都严格按照每30帧（1秒）间隔分布：

```
帧号:   1,  31,  61,  91, 121, 151, 181, 211, 241, 271
秒数:  0s,  1s,  2s,  3s,  4s,  5s,  6s,  7s,  8s,  9s
```

第301帧（10秒结束）不包含在内，因为duration=10s。

## 使用场景

这些视频文件适合用于：

1. **VTX协议测试**
   - 测试I帧选择性重传机制
   - 测试P帧丢弃策略
   - 验证帧分片和重组

2. **性能基准测试**
   - 不同编码格式的传输性能对比
   - 网络丢包恢复测试
   - 延迟测量

3. **兼容性测试**
   - H.264 Baseline profile兼容性
   - H.265解码器兼容性

## 生成命令

### H.264 30fps
```bash
ffmpeg -f lavfi -i testsrc=duration=10:size=1280x720:rate=30 \
  -c:v libx264 -profile:v baseline -level 3.1 \
  -g 30 -keyint_min 30 -sc_threshold 0 \
  -pix_fmt yuv420p -y h264_30fps.mp4
```

### H.265 30fps
```bash
ffmpeg -f lavfi -i testsrc=duration=10:size=1280x720:rate=30 \
  -c:v libx265 -x265-params keyint=30:min-keyint=30:scenecut=0 \
  -pix_fmt yuv420p -y h265_30fps.mp4
```

## 参数说明

- `-f lavfi -i testsrc`: 使用FFmpeg内置的测试图案源
- `duration=10`: 视频时长10秒
- `size=1280x720`: 分辨率720p
- `rate=30`: 帧率30fps
- `-g 30`: GOP大小为30帧
- `-keyint_min 30`: 最小关键帧间隔30帧
- `-sc_threshold 0`: 禁用场景切换检测（确保固定GOP）
- `-pix_fmt yuv420p`: 像素格式（兼容性最好）

## 验证命令

### 查看视频信息
```bash
ffprobe -v error -show_entries stream=codec_name,width,height,r_frame_rate,nb_frames \
  -show_entries format=duration h264_30fps.mp4
```

### 查看I帧位置
```bash
ffprobe -v error -select_streams v:0 \
  -show_entries frame=pict_type,coded_picture_number \
  -of csv=p=0 h264_30fps.mp4 | grep -n "^I"
```

### 提取帧类型统计
```bash
ffprobe -v error -select_streams v:0 \
  -show_entries frame=pict_type -of csv=p=0 h264_30fps.mp4 | \
  sort | uniq -c
```

## 测试内容

视频内容为FFmpeg的testsrc图案，包含：
- 彩色条纹
- 移动的方块
- 时间戳显示
- 不同颜色区域

这种图案设计用于视频编码测试，包含足够的细节和运动来产生代表性的编码结果。

---

**生成日期**: 2025-10-20
**FFmpeg版本**: 8.0
