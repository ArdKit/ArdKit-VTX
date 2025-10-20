# VTX - Video Transmission over eXtended UDP

[简体中文](docs/README-cn.md) | English

[![Version](https://img.shields.io/badge/version-2.0.5-blue.svg)](https://github.com/ArdKit/ArdKit-VTX)
[![Build](https://img.shields.io/badge/build-passing-brightgreen.svg)](BUILD.md)
[![License](https://img.shields.io/badge/license-Apache%202.0-blue.svg)](LICENSE)

VTX is a UDP-based real-time video transmission protocol library designed for low-latency, high-reliability video streaming scenarios.

## Core Features

- ✅ **Low Latency**: End-to-end latency < 50ms (target)
- ✅ **Selective Retransmission**: Fragment-level retransmission for I-frames, drop strategy for P-frames
- ✅ **Zero-Copy Design**: iovec + reference counting to minimize memory copying
- ✅ **Memory Pooling**: Pre-allocated frame objects to avoid frequent allocations
- ✅ **Lock-Free Design**: Atomic operations + spinlock
- ✅ **Cross-Platform**: Supports Linux and macOS
- ✅ **Thread-Safe**: Comprehensive concurrency control mechanisms
- ✅ **Production Ready**: 5000+ lines of high-quality code

## Quick Start

### 1. Build

```bash
mkdir -p build && cd build
cmake ..
make -j4
```

Build outputs (located in `build/` directory):
- `lib/libvtx.a` - Static library
- `bin/test_basic` - Basic test program
- `bin/server` - Server example (requires FFmpeg)
- `bin/client` - Client example (requires FFmpeg)

For detailed build instructions, see [BUILD.md](BUILD.md)

### 2. API Example

#### Transmitter (TX)

```c
#include "vtx.h"

// 1. Create configuration
vtx_tx_config_t config = {
    .bind_addr = "0.0.0.0",
    .bind_port = 8888,
    .mtu = VTX_DEFAULT_MTU,
};

// 2. Create TX
vtx_tx_t* tx = vtx_tx_create(&config, NULL, NULL);

// 3. Listen on port
vtx_tx_listen(tx);

// 4. Accept connection
vtx_tx_accept(tx, 5000);  // 5-second timeout

// 5. Send data
const char* data = "Hello VTX!";
vtx_tx_send(tx, (uint8_t*)data, strlen(data));

// 6. Cleanup
vtx_tx_close(tx);
vtx_tx_destroy(tx);
```

#### Receiver (RX)

```c
#include "vtx.h"

// Frame callback
static int on_frame(const uint8_t* data, size_t size,
                    vtx_frame_type_t type, void* userdata) {
    printf("Received frame: type=%d size=%zu\n", type, size);
    return VTX_OK;
}

int main() {
    // 1. Create configuration
    vtx_rx_config_t config = {
        .server_addr = "127.0.0.1",
        .server_port = 8888,
        .mtu = VTX_DEFAULT_MTU,
    };

    // 2. Create RX
    vtx_rx_t* rx = vtx_rx_create(&config, on_frame, NULL, NULL, NULL);

    // 3. Connect to server
    vtx_rx_connect(rx);

    // 4. Poll events
    while (running) {
        vtx_rx_poll(rx, 100);  // 100ms timeout
    }

    // 5. Cleanup
    vtx_rx_close(rx);
    vtx_rx_destroy(rx);
}
```

## Architecture

```
vtx.v2/
├── include/          # Public header files
│   ├── vtx.h        # Main API
│   ├── vtx_types.h  # Data types
│   ├── vtx_packet.h # Packet processing
│   ├── vtx_frame.h  # Frame management
│   └── ...
├── src/             # Source code implementation
│   ├── vtx_packet.c # Packet processing
│   ├── vtx_frame.c  # Frame and memory pool
│   ├── vtx_tx.c     # Transmitter
│   ├── vtx_rx.c     # Receiver
│   └── vtx.c        # Main API
├── tests/           # Test programs
│   └── test_basic.c
├── examples/        # Example programs (require FFmpeg)
│   ├── server.c
│   └── client.c
├── build/           # Build directory
└── CMakeLists.txt   # CMake configuration
```

## Core Modules

### 1. vtx_packet - Packet Processing
- CRC-16-CCITT checksum
- Packet serialization/deserialization
- Network byte order conversion

### 2. vtx_frame - Frame Management
- Memory pools (512KB media frames, 128B control frames)
- Atomic reference counting
- Fragment tracking and reassembly
- Timeout handling

### 3. vtx_tx - Transmitter
- UDP socket management
- Connection management (listen/accept)
- Zero-copy transmission
- ACK processing

### 4. vtx_rx - Receiver
- UDP socket management
- Connection management (connect)
- Frame reassembly
- ACK transmission

## Protocol Design

### Packet Format

```
+----------+----------+-------+-------+----------+-------------+
| seq_num  | frame_id | type  | flags | frag_idx | total_frags |
|  (4B)    |  (2B)    | (1B)  | (1B)  |  (2B)    |    (2B)     |
+----------+----------+-------+-------+----------+-------------+
| payload_size | checksum | [timestamp] |    payload ...       |
|     (2B)     |   (2B)   |   (8B)     |                       |
+--------------+----------+-------------+-----------------------+
```

- **Release mode**: 16-byte header
- **Debug mode**: 24-byte header (additional 8-byte timestamp)

### Frame Types

- `VTX_FRAME_I` - I-frame (keyframe, requires retransmission)
- `VTX_FRAME_P` - P-frame (predicted frame, no retransmission on loss)
- `VTX_FRAME_SPS/PPS` - H.264 parameter sets
- `VTX_FRAME_A` - Audio frame
- `VTX_DATA_*` - Control frames (CONNECT, DISCONNECT, ACK, DATA, etc.)

## Performance Characteristics

- **MTU**: Default 1400 bytes (configurable)
- **I-frame retransmission timeout**: 5ms (configurable)
- **DATA retransmission timeout**: 30ms (configurable)
- **Maximum retransmissions**: 3 times (configurable)
- **Frame timeout**: 100ms (configurable)

## Configuration Options

### TX Configuration

```c
typedef struct {
    const char* bind_addr;           // Bind address
    uint16_t    bind_port;           // Bind port
    uint16_t    mtu;                 // MTU size
    uint32_t    send_buf_size;       // Send buffer size
    uint32_t    retrans_timeout_ms;  // I-frame retransmission timeout
    uint8_t     max_retrans;         // Maximum retransmissions
    uint32_t    data_retrans_timeout_ms; // DATA retransmission timeout
    uint8_t     data_max_retrans;    // DATA maximum retransmissions
} vtx_tx_config_t;
```

### RX Configuration

```c
typedef struct {
    const char* server_addr;             // Server address
    uint16_t    server_port;             // Server port
    uint16_t    mtu;                     // MTU size
    uint32_t    recv_buf_size;           // Receive buffer size
    uint32_t    frame_timeout_ms;        // Frame reception timeout (default 100ms)
    uint32_t    data_retrans_timeout_ms; // DATA packet retransmission timeout (default 30ms)
    uint8_t     data_max_retrans;        // DATA packet max retransmissions (default 3)
    uint32_t    heartbeat_interval_ms;   // Heartbeat transmission interval (default 60s)
} vtx_rx_config_t;
```

## Statistics

### TX Statistics

```c
typedef struct {
    uint64_t total_frames;       // Total transmitted frames
    uint64_t total_i_frames;     // I-frame count
    uint64_t total_p_frames;     // P-frame count
    uint64_t total_packets;      // Total transmitted packets
    uint64_t total_bytes;        // Total transmitted bytes
    uint64_t retrans_packets;    // Retransmitted packets
    uint64_t retrans_bytes;      // Retransmitted bytes
    // ...
} vtx_tx_stats_t;
```

### RX Statistics

```c
typedef struct {
    uint64_t total_frames;       // Total received frames
    uint64_t total_packets;      // Total received packets
    uint64_t total_bytes;        // Total received bytes
    uint64_t lost_packets;       // Lost packets
    uint64_t dup_packets;        // Duplicate packets
    uint64_t incomplete_frames;  // Incomplete frames
    // ...
} vtx_rx_stats_t;
```

## Error Codes

```c
#define VTX_OK                  0      // Success
#define VTX_ERR_INVALID_PARAM   0x8001 // Invalid parameter
#define VTX_ERR_NO_MEMORY       0x8002 // Out of memory
#define VTX_ERR_TIMEOUT         0x8006 // Timeout
#define VTX_ERR_CHECKSUM        0x8014 // Checksum error
#define VTX_ERR_NETWORK         0x8064 // Network error
// See vtx_error.h for more error codes
```

## Debugging

Enable DEBUG mode:

```bash
cmake -DCMAKE_BUILD_TYPE=Debug ..
make
```

DEBUG mode features:
- 8-byte timestamp added to packet header
- Detailed logging output
- Latency statistics
- Optional packet loss simulation

## Dependencies

### Build Dependencies
- CMake 3.10+
- C17 compiler (GCC 7.0+ / Clang 10.0+)

### Runtime Dependencies
- POSIX pthread (threading)
- POSIX socket (networking)
- macOS: libkern/OSByteOrder.h
- Linux: endian.h

### Example Program Dependencies (Optional)
- FFmpeg (libavformat, libavcodec, libavutil, libswscale)

## License

Apache License 2.0

## Documentation

- [BUILD.md](BUILD.md) - Build guide
- [QUICKSTART.md](QUICKSTART.md) - Quick start guide
- [STATUS.md](STATUS.md) - Implementation status and version history
- [CLAUDE.md](CLAUDE.md) - Complete development guide (Chinese)
- Header file comments - Detailed API documentation

## Changelog

### v2.0.5 (2025-10-20) - Current Version
- ✅ Fixed thread race condition in server.c
- ✅ Enhanced URL parsing boundary checks
- ✅ Frame reassembly timeout mechanism (verified)
- ✅ Refactored magic numbers to constants
- ✅ Optimized build output directory structure

### v2.0.4
- Basic functionality implementation
- 3-way handshake
- Fragment-level retransmission
- Heartbeat mechanism

See [STATUS.md](STATUS.md) for details

## Authors

VTX Development Team

## License

Apache License 2.0 - See [LICENSE](LICENSE) file for details

## Contributing

Issues and Pull Requests are welcome!

## Version

**Current Version**: v2.0.5
**Release Date**: 2025-10-20
**Status**: ✅ Production Ready
