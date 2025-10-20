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
 * @file test_tx_media.c
 * @brief Test TX media frame APIs
 */

#include "vtx.h"
#include "vtx_frame.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    printf("=== VTX TX Media Frame Test ===\n\n");

    /* 初始化VTX */
    vtx_init(NULL);

    /* 创建TX配置 */
    vtx_tx_config_t tx_config = {
        .bind_addr = "127.0.0.1",
        .bind_port = 8888,
        .mtu = VTX_DEFAULT_MTU,
        .send_buf_size = VTX_DEFAULT_SEND_BUF,
        .retrans_timeout_ms = VTX_DEFAULT_RETRANS_TIMEOUT_MS,
        .max_retrans = VTX_DEFAULT_MAX_RETRANS,
        .data_retrans_timeout_ms = VTX_DEFAULT_DATA_RETRANS_TIMEOUT_MS,
        .data_max_retrans = VTX_DEFAULT_MAX_RETRANS,
    };

    /* 创建TX */
    vtx_tx_t* tx = vtx_tx_create(&tx_config, NULL, NULL, NULL);
    if (!tx) {
        printf("Failed to create TX\n");
        return 1;
    }

    printf("TX created successfully\n");

    /* 测试分配媒体帧 */
    printf("\nTest 1: Allocate media frame\n");
    vtx_frame_t* frame = vtx_tx_alloc_media_frame(tx);
    if (!frame) {
        printf("  Failed to allocate frame\n");
        vtx_tx_destroy(tx);
        return 1;
    }
    printf("  Allocated frame: capacity=%zu bytes\n", vtx_frame_capacity(frame));

    /* 填充数据 */
    printf("\nTest 2: Fill frame with test data\n");
    const char* test_data = "This is a test I-frame data";
    size_t data_len = strlen(test_data);

    size_t copied = vtx_frame_copyto(frame, 0, (const uint8_t*)test_data, data_len);
    printf("  Copied %zu bytes to frame\n", copied);
    printf("  Frame data_size: %zu\n", vtx_frame_size(frame));

    /* 设置帧类型 */
    frame->frame_type = VTX_FRAME_I;
    printf("  Frame type set to: VTX_FRAME_I\n");

    /* 测试释放帧 */
    printf("\nTest 3: Free media frame\n");
    vtx_tx_free_frame(tx, frame);
    printf("  Frame freed successfully\n");

    /* 再次分配并测试多次 */
    printf("\nTest 4: Multiple allocate/free cycles\n");
    for (int i = 0; i < 5; i++) {
        vtx_frame_t* f = vtx_tx_alloc_media_frame(tx);
        if (f) {
            printf("  Cycle %d: allocated frame\n", i + 1);
            vtx_tx_free_frame(tx, f);
        }
    }

    /* 清理 */
    vtx_tx_destroy(tx);
    printf("\nTX destroyed\n");

    vtx_fini();
    printf("\n=== Test completed ===\n");

    return 0;
}
