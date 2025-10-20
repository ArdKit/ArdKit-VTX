/**
 * @file test_frame_copy.c
 * @brief Test vtx_frame_copyfrom and vtx_frame_copyto
 */

#include "vtx_frame.h"
#include "vtx_error.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    printf("=== VTX Frame Copy Test ===\n\n");

    /* 创建内存池 */
    vtx_frame_pool_t* pool = vtx_frame_pool_create(1, 1024);
    if (!pool) {
        printf("Failed to create frame pool\n");
        return 1;
    }

    /* 获取frame */
    vtx_frame_t* frame = vtx_frame_pool_acquire(pool);
    if (!frame) {
        printf("Failed to acquire frame\n");
        vtx_frame_pool_destroy(pool);
        return 1;
    }

    printf("Frame capacity: %zu bytes\n", vtx_frame_capacity(frame));
    printf("Frame initial size: %zu bytes\n\n", vtx_frame_size(frame));

    /* 测试 copyto */
    printf("Test 1: vtx_frame_copyto\n");
    const char* test_data1 = "Hello, VTX!";
    size_t copied = vtx_frame_copyto(frame, 0, (const uint8_t*)test_data1, strlen(test_data1));
    printf("  Copied %zu bytes to offset 0\n", copied);
    printf("  Frame size after copyto: %zu bytes\n", vtx_frame_size(frame));

    /* 测试 copyfrom */
    printf("\nTest 2: vtx_frame_copyfrom\n");
    char buffer[64] = {0};
    copied = vtx_frame_copyfrom(frame, 0, (uint8_t*)buffer, strlen(test_data1));
    printf("  Copied %zu bytes from offset 0\n", copied);
    printf("  Data: \"%s\"\n", buffer);

    /* 测试追加数据 */
    printf("\nTest 3: Append data at offset\n");
    const char* test_data2 = " World!";
    copied = vtx_frame_copyto(frame, strlen(test_data1), (const uint8_t*)test_data2, strlen(test_data2));
    printf("  Copied %zu bytes to offset %zu\n", copied, strlen(test_data1));
    printf("  Frame size after append: %zu bytes\n", vtx_frame_size(frame));

    /* 读取完整数据 */
    memset(buffer, 0, sizeof(buffer));
    copied = vtx_frame_copyfrom(frame, 0, (uint8_t*)buffer, vtx_frame_size(frame));
    printf("  Full data: \"%s\"\n", buffer);

    /* 测试覆盖数据 */
    printf("\nTest 4: Overwrite data at offset\n");
    const char* test_data3 = "OVERWRITE";
    copied = vtx_frame_copyto(frame, 7, (const uint8_t*)test_data3, strlen(test_data3));
    printf("  Copied %zu bytes to offset 7\n", copied);
    printf("  Frame size: %zu bytes\n", vtx_frame_size(frame));

    memset(buffer, 0, sizeof(buffer));
    copied = vtx_frame_copyfrom(frame, 0, (uint8_t*)buffer, vtx_frame_size(frame));
    printf("  Data after overwrite: \"%s\"\n", buffer);

    /* 测试边界检查 - copyfrom */
    printf("\nTest 5: Boundary check for copyfrom\n");
    copied = vtx_frame_copyfrom(frame, vtx_frame_size(frame), (uint8_t*)buffer, 10);
    printf("  Try to copy from offset=%zu (should fail): copied=%zu\n",
           vtx_frame_size(frame), copied);

    copied = vtx_frame_copyfrom(frame, 0, (uint8_t*)buffer, vtx_frame_size(frame) + 100);
    printf("  Try to copy %zu bytes (exceeds size, should fail): copied=%zu\n",
           vtx_frame_size(frame) + 100, copied);

    /* 测试边界检查 - copyto */
    printf("\nTest 6: Boundary check for copyto\n");
    copied = vtx_frame_copyto(frame, vtx_frame_capacity(frame), (const uint8_t*)"X", 1);
    printf("  Try to copy to offset=%zu (at capacity, should fail): copied=%zu\n",
           vtx_frame_capacity(frame), copied);

    size_t big_offset = vtx_frame_capacity(frame) - 5;
    copied = vtx_frame_copyto(frame, big_offset, (const uint8_t*)"0123456789", 10);
    printf("  Try to copy 10 bytes to offset=%zu (exceeds capacity, should fail): copied=%zu\n",
           big_offset, copied);

    /* 测试部分读取 */
    printf("\nTest 7: Partial read from middle\n");
    memset(buffer, 0, sizeof(buffer));
    copied = vtx_frame_copyfrom(frame, 7, (uint8_t*)buffer, 9);
    printf("  Copy 9 bytes from offset 7: \"%s\" (copied=%zu)\n", buffer, copied);

    /* 清理 */
    vtx_frame_release(pool, frame);
    vtx_frame_pool_destroy(pool);

    printf("\n=== All tests completed ===\n");

    return 0;
}
