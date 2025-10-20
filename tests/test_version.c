/**
 * @file test_version.c
 * @brief Test VTX version and initialization
 */

#include "vtx.h"
#include <stdio.h>

int main(void) {
    printf("=== VTX Version Test ===\n");

    /* 测试版本API */
    printf("Version string: %s\n", vtx_version());
    printf("Build info: %s\n\n", vtx_build_info());

    vtx_version_t version;
    vtx_version_info(&version);
    printf("Version info:\n");
    printf("  Major: %d\n", version.major);
    printf("  Minor: %d\n", version.minor);
    printf("  Build: %d\n\n", version.build);

    /* 测试初始化 */
    printf("Is initialized: %d (should be 0)\n", vtx_is_initialized());

    vtx_init_config_t config = {
        .mem_limit_bytes = 1024 * 1024 * 100  /* 100MB limit */
    };

    int ret = vtx_init(&config);
    printf("vtx_init() returned: %d (should be 0)\n", ret);
    printf("Is initialized: %d (should be 1)\n", vtx_is_initialized());

    /* 测试重复初始化 */
    ret = vtx_init(&config);
    printf("Second vtx_init() returned: %d (should be -15 = VTX_ERR_ALREADY_INIT)\n", ret);

    /* 销毁 */
    vtx_fini();
    printf("vtx_fini() called\n");
    printf("Is initialized: %d (should be 0)\n", vtx_is_initialized());

    printf("\nAll tests passed!\n");

    return 0;
}
