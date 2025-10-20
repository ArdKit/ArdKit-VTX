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
 * @file vtx.c
 * @brief VTX Main API Implementation
 */

#include "vtx.h"
#include "vtx_error.h"
#include "vtx_mem.h"
#include <stdio.h>
#include <string.h>

/* ========== 版本信息 ========== */

#ifndef VTX_VERSION_MAJOR
#define VTX_VERSION_MAJOR 2
#endif

#ifndef VTX_VERSION_MINOR
#define VTX_VERSION_MINOR 0
#endif

#ifndef VTX_VERSION_BUILD
#define VTX_VERSION_BUILD 0
#endif

#define _VTX_STR(x) #x
#define _VTX_XSTR(x) _VTX_STR(x)

#define VTX_VERSION_STRING \
    _VTX_XSTR(VTX_VERSION_MAJOR) "." \
    _VTX_XSTR(VTX_VERSION_MINOR) "." \
    _VTX_XSTR(VTX_VERSION_BUILD)

/* ========== 全局状态 ========== */

static struct {
    int initialized;
    uint64_t mem_limit;
} g_vtx = {
    .initialized = 0,
    .mem_limit = 0,
};

/* ========== 初始化/销毁 ========== */

int vtx_init(const vtx_init_config_t* config) {
    if (g_vtx.initialized) {
        return VTX_ERR_ALREADY_INIT;
    }

    uint64_t mem_limit = 0;
    if (config) {
        mem_limit = config->mem_limit_bytes;
    }

    /* 初始化内存管理 */
    int ret = vtx_mem_init(mem_limit);
    if (ret != VTX_OK) {
        return ret;
    }

    g_vtx.mem_limit = mem_limit;
    g_vtx.initialized = 1;

    return VTX_OK;
}

void vtx_fini(void) {
    if (!g_vtx.initialized) {
        return;
    }

    /* 销毁内存管理 */
    vtx_mem_fini();

    g_vtx.initialized = 0;
}

int vtx_is_initialized(void) {
    return g_vtx.initialized;
}

/* ========== 版本信息 ========== */

const char* vtx_version(void) {
    return VTX_VERSION_STRING;
}

void vtx_version_info(vtx_version_t* version) {
    if (!version) {
        return;
    }

    version->major = VTX_VERSION_MAJOR;
    version->minor = VTX_VERSION_MINOR;
    version->build = VTX_VERSION_BUILD;
}

const char* vtx_build_info(void) {
    static char build_info[256];
    snprintf(build_info, sizeof(build_info),
             "VTX v%s, built on %s %s"
#ifdef VTX_DEBUG
             " (DEBUG)"
#else
             " (RELEASE)"
#endif
#ifdef __APPLE__
             " for macOS"
#elif defined(__linux__)
             " for Linux"
#else
             " for Unknown"
#endif
             ,
             VTX_VERSION_STRING, __DATE__, __TIME__);
    return build_info;
}
