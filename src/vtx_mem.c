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
 * @file vtx_mem.c
 * @brief VTX Memory Management Implementation
 */

#include "vtx_mem.h"
#include "vtx_error.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef MEM_DEBUG

#include <pthread.h>

/* ========== 调试模式：内存追踪 ========== */

#define MEM_MAGIC_HEAD 0xDEADBEEF
#define MEM_MAGIC_TAIL 0xCAFEBABE
#define MEM_FREED_MAGIC 0xFEEEFEEE

/* 内存块头部 */
typedef struct mem_block {
    uint32_t magic_head;      /* 头部魔数 */
    size_t size;              /* 用户请求的大小 */
    struct mem_block *next;   /* 链表指针 */
    struct mem_block *prev;
} mem_block_t;

/* 内存块尾部 */
typedef struct {
    uint32_t magic_tail;      /* 尾部魔数 */
} mem_tail_t;

/* 全局状态 */
static struct {
    pthread_mutex_t lock;
    mem_block_t *head;        /* 内存块链表 */
    vtx_mem_stats_t stats;    /* 统计信息 */
    uint64_t limit_bytes;     /* 内存上限 */
    int initialized;
} g_mem = {
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .head = NULL,
    .stats = {0},
    .limit_bytes = 0,
    .initialized = 0,
};

/* 获取尾部指针 */
static inline mem_tail_t* get_tail(mem_block_t *block) {
    return (mem_tail_t*)((char*)(block + 1) + block->size);
}

/* 检查边界 */
static int check_boundary(mem_block_t *block) {
    if (block->magic_head != MEM_MAGIC_HEAD) {
        return -1;  /* 头部损坏 */
    }
    mem_tail_t *tail = get_tail(block);
    if (tail->magic_tail != MEM_MAGIC_TAIL) {
        return -2;  /* 尾部损坏 */
    }
    return 0;
}

/* 初始化 */
int vtx_mem_init(uint64_t limit_bytes) {
    pthread_mutex_lock(&g_mem.lock);

    if (g_mem.initialized) {
        pthread_mutex_unlock(&g_mem.lock);
        return VTX_ERR_ALREADY_INIT;
    }

    memset(&g_mem.stats, 0, sizeof(g_mem.stats));
    g_mem.head = NULL;
    g_mem.limit_bytes = limit_bytes;
    g_mem.initialized = 1;

    pthread_mutex_unlock(&g_mem.lock);
    return VTX_OK;
}

/* 销毁 */
void vtx_mem_fini(void) {
    pthread_mutex_lock(&g_mem.lock);

    if (!g_mem.initialized) {
        pthread_mutex_unlock(&g_mem.lock);
        return;
    }

    /* 检查内存泄漏 */
    if (g_mem.stats.current_bytes > 0) {
        fprintf(stderr, "[MEM] WARNING: Memory leak detected: %llu bytes in %llu blocks\n",
                (unsigned long long)g_mem.stats.current_bytes,
                (unsigned long long)(g_mem.stats.total_alloc - g_mem.stats.total_free));
    }

    g_mem.initialized = 0;
    pthread_mutex_unlock(&g_mem.lock);
}

/* 分配内存 */
void* vtx_malloc(size_t size) {
    if (size == 0) {
        return NULL;
    }

    pthread_mutex_lock(&g_mem.lock);

    /* 检查上限 */
    if (g_mem.limit_bytes > 0 &&
        g_mem.stats.current_bytes + size > g_mem.limit_bytes) {
        pthread_mutex_unlock(&g_mem.lock);
        return NULL;
    }

    /* 分配：头部 + 用户数据 + 尾部 */
    size_t total_size = sizeof(mem_block_t) + size + sizeof(mem_tail_t);
    mem_block_t *block = (mem_block_t*)malloc(total_size);
    if (!block) {
        pthread_mutex_unlock(&g_mem.lock);
        return NULL;
    }

    /* 初始化头部 */
    block->magic_head = MEM_MAGIC_HEAD;
    block->size = size;
    block->next = g_mem.head;
    block->prev = NULL;

    if (g_mem.head) {
        g_mem.head->prev = block;
    }
    g_mem.head = block;

    /* 初始化尾部 */
    mem_tail_t *tail = get_tail(block);
    tail->magic_tail = MEM_MAGIC_TAIL;

    /* 更新统计 */
    g_mem.stats.total_alloc++;
    g_mem.stats.current_bytes += size;
    g_mem.stats.total_bytes += size;
    if (g_mem.stats.current_bytes > g_mem.stats.peak_bytes) {
        g_mem.stats.peak_bytes = g_mem.stats.current_bytes;
    }

    pthread_mutex_unlock(&g_mem.lock);

    /* 清零并返回用户数据区 */
    void *ptr = (void*)(block + 1);
    memset(ptr, 0, size);
    return ptr;
}

/* calloc */
void* vtx_calloc(size_t nmemb, size_t size) {
    size_t total = nmemb * size;
    return vtx_malloc(total);  /* vtx_malloc已经清零 */
}

/* realloc */
void* vtx_realloc(void *ptr, size_t size) {
    if (!ptr) {
        return vtx_malloc(size);
    }

    if (size == 0) {
        vtx_free(ptr);
        return NULL;
    }

    /* 获取旧块 */
    mem_block_t *old_block = (mem_block_t*)ptr - 1;

    pthread_mutex_lock(&g_mem.lock);

    /* 检查边界 */
    if (check_boundary(old_block) != 0) {
        g_mem.stats.boundary_errors++;
        fprintf(stderr, "[MEM] ERROR: Boundary corruption detected in realloc\n");
        pthread_mutex_unlock(&g_mem.lock);
        return NULL;
    }

    size_t old_size = old_block->size;

    pthread_mutex_unlock(&g_mem.lock);

    /* 分配新块 */
    void *new_ptr = vtx_malloc(size);
    if (!new_ptr) {
        return NULL;
    }

    /* 复制数据 */
    size_t copy_size = (old_size < size) ? old_size : size;
    memcpy(new_ptr, ptr, copy_size);

    /* 释放旧块 */
    vtx_free(ptr);

    return new_ptr;
}

/* strdup */
char* vtx_strdup(const char *s) {
    if (!s) {
        return NULL;
    }

    size_t len = strlen(s) + 1;
    char *new_str = (char*)vtx_malloc(len);
    if (new_str) {
        memcpy(new_str, s, len);
    }
    return new_str;
}

/* strndup */
char* vtx_strndup(const char *s, size_t n) {
    if (!s) {
        return NULL;
    }

    size_t len = strnlen(s, n);
    char *new_str = (char*)vtx_malloc(len + 1);
    if (new_str) {
        memcpy(new_str, s, len);
        new_str[len] = '\0';
    }
    return new_str;
}

/* 释放 */
void vtx_free(void *ptr) {
    if (!ptr) {
        return;
    }

    mem_block_t *block = (mem_block_t*)ptr - 1;

    pthread_mutex_lock(&g_mem.lock);

    /* 检查重复释放 */
    if (block->magic_head == MEM_FREED_MAGIC) {
        g_mem.stats.double_free_errors++;
        fprintf(stderr, "[MEM] ERROR: Double free detected at %p\n", ptr);
        pthread_mutex_unlock(&g_mem.lock);
        return;
    }

    /* 检查边界 */
    if (check_boundary(block) != 0) {
        g_mem.stats.boundary_errors++;
        fprintf(stderr, "[MEM] ERROR: Boundary corruption detected at %p (size=%zu)\n",
                ptr, block->size);
    }

    /* 从链表移除 */
    if (block->prev) {
        block->prev->next = block->next;
    } else {
        g_mem.head = block->next;
    }

    if (block->next) {
        block->next->prev = block->prev;
    }

    /* 更新统计 */
    g_mem.stats.total_free++;
    g_mem.stats.current_bytes -= block->size;

    /* 标记为已释放 */
    block->magic_head = MEM_FREED_MAGIC;

    pthread_mutex_unlock(&g_mem.lock);

    /* 释放内存 */
    free(block);
}

/* 获取统计 */
int vtx_mem_get_stats(vtx_mem_stats_t *stats) {
    if (!stats) {
        return VTX_ERR_INVALID_PARAM;
    }

    pthread_mutex_lock(&g_mem.lock);
    *stats = g_mem.stats;
    pthread_mutex_unlock(&g_mem.lock);

    return VTX_OK;
}

/* 重置统计 */
int vtx_mem_reset_stats(void) {
    pthread_mutex_lock(&g_mem.lock);

    uint64_t current = g_mem.stats.current_bytes;
    memset(&g_mem.stats, 0, sizeof(g_mem.stats));
    g_mem.stats.current_bytes = current;

    pthread_mutex_unlock(&g_mem.lock);
    return VTX_OK;
}

/* 设置上限 */
int vtx_mem_set_limit(uint64_t limit_bytes) {
    pthread_mutex_lock(&g_mem.lock);
    g_mem.limit_bytes = limit_bytes;
    pthread_mutex_unlock(&g_mem.lock);
    return VTX_OK;
}

/* 获取上限 */
uint64_t vtx_mem_get_limit(void) {
    pthread_mutex_lock(&g_mem.lock);
    uint64_t limit = g_mem.limit_bytes;
    pthread_mutex_unlock(&g_mem.lock);
    return limit;
}

/* 打印统计 */
void vtx_mem_print_stats(void) {
    pthread_mutex_lock(&g_mem.lock);

    printf("\n========== VTX Memory Statistics ==========\n");
    printf("Total allocations:   %llu\n", (unsigned long long)g_mem.stats.total_alloc);
    printf("Total frees:         %llu\n", (unsigned long long)g_mem.stats.total_free);
    printf("Current bytes:       %llu\n", (unsigned long long)g_mem.stats.current_bytes);
    printf("Peak bytes:          %llu\n", (unsigned long long)g_mem.stats.peak_bytes);
    printf("Total bytes:         %llu\n", (unsigned long long)g_mem.stats.total_bytes);
    printf("Boundary errors:     %llu\n", (unsigned long long)g_mem.stats.boundary_errors);
    printf("Double free errors:  %llu\n", (unsigned long long)g_mem.stats.double_free_errors);
    printf("===========================================\n\n");

    pthread_mutex_unlock(&g_mem.lock);
}

/* 检查泄漏 */
int vtx_mem_check_leak(void) {
    pthread_mutex_lock(&g_mem.lock);
    int leak_count = (int)(g_mem.stats.total_alloc - g_mem.stats.total_free);
    pthread_mutex_unlock(&g_mem.lock);
    return leak_count;
}

/* 打印泄漏 */
void vtx_mem_dump_leaks(void) {
    pthread_mutex_lock(&g_mem.lock);

    if (!g_mem.head) {
        printf("No memory leaks detected.\n");
        pthread_mutex_unlock(&g_mem.lock);
        return;
    }

    printf("\n========== Memory Leaks ==========\n");
    int count = 0;
    mem_block_t *block = g_mem.head;
    while (block) {
        count++;
        printf("Leak #%d: %p, size=%zu\n", count, (void*)(block + 1), block->size);
        block = block->next;
    }
    printf("Total: %d leaked blocks, %llu bytes\n",
           count, (unsigned long long)g_mem.stats.current_bytes);
    printf("==================================\n\n");

    pthread_mutex_unlock(&g_mem.lock);
}

#else

/* ========== 发布模式：直接封装libc ========== */

int vtx_mem_init(uint64_t limit_bytes) {
    (void)limit_bytes;
    return VTX_OK;
}

void vtx_mem_fini(void) {
    /* No-op */
}

void* vtx_malloc(size_t size) {
    if (size == 0) {
        return NULL;
    }
    void *ptr = malloc(size);
    if (ptr) {
        memset(ptr, 0, size);
    }
    return ptr;
}

void* vtx_calloc(size_t nmemb, size_t size) {
    return calloc(nmemb, size);
}

void* vtx_realloc(void *ptr, size_t size) {
    if (!ptr) {
        return vtx_malloc(size);
    }
    if (size == 0) {
        free(ptr);
        return NULL;
    }
    return realloc(ptr, size);
}

char* vtx_strdup(const char *s) {
    if (!s) {
        return NULL;
    }
    return strdup(s);
}

char* vtx_strndup(const char *s, size_t n) {
    if (!s) {
        return NULL;
    }
#ifdef __APPLE__
    /* macOS has strndup */
    return strndup(s, n);
#else
    size_t len = strnlen(s, n);
    char *new_str = (char*)malloc(len + 1);
    if (new_str) {
        memcpy(new_str, s, len);
        new_str[len] = '\0';
    }
    return new_str;
#endif
}

void vtx_free(void *ptr) {
    free(ptr);
}

#endif /* MEM_DEBUG */
