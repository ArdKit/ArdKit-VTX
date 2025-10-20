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
 * @file vtx_spinlock.h
 * @brief VTX Cross-platform Spinlock
 *
 * 跨平台自旋锁实现：
 * - Linux: pthread_spinlock_t
 * - macOS: os_unfair_lock
 *
 * 提供统一的接口：
 * - vtx_spinlock_init()
 * - vtx_spinlock_lock()
 * - vtx_spinlock_unlock()
 * - vtx_spinlock_destroy()
 */

#ifndef VTX_SPINLOCK_H
#define VTX_SPINLOCK_H

#ifdef __linux__
#include <pthread.h>
#elif defined(__APPLE__)
#include <os/lock.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ========== 跨平台自旋锁类型 ========== */

#ifdef __linux__
typedef pthread_spinlock_t vtx_spinlock_t;
#elif defined(__APPLE__)
typedef os_unfair_lock vtx_spinlock_t;
#else
#error "Unsupported platform"
#endif

/* ========== 自旋锁接口 ========== */

/**
 * @brief 初始化自旋锁
 *
 * @param lock 自旋锁指针
 * @return 0成功，负数表示错误码
 */
static inline int vtx_spinlock_init(vtx_spinlock_t* lock) {
#ifdef __linux__
    return pthread_spin_init(lock, PTHREAD_PROCESS_PRIVATE);
#elif defined(__APPLE__)
    *lock = OS_UNFAIR_LOCK_INIT;
    return 0;
#endif
}

/**
 * @brief 加锁（阻塞直到获取锁）
 *
 * @param lock 自旋锁指针
 */
static inline void vtx_spinlock_lock(vtx_spinlock_t* lock) {
#ifdef __linux__
    pthread_spin_lock(lock);
#elif defined(__APPLE__)
    os_unfair_lock_lock(lock);
#endif
}

/**
 * @brief 尝试加锁（非阻塞）
 *
 * @param lock 自旋锁指针
 * @return 0成功获取锁，非0表示锁已被占用
 */
static inline int vtx_spinlock_trylock(vtx_spinlock_t* lock) {
#ifdef __linux__
    return pthread_spin_trylock(lock);
#elif defined(__APPLE__)
    return os_unfair_lock_trylock(lock) ? 0 : -1;
#endif
}

/**
 * @brief 解锁
 *
 * @param lock 自旋锁指针
 */
static inline void vtx_spinlock_unlock(vtx_spinlock_t* lock) {
#ifdef __linux__
    pthread_spin_unlock(lock);
#elif defined(__APPLE__)
    os_unfair_lock_unlock(lock);
#endif
}

/**
 * @brief 销毁自旋锁
 *
 * @param lock 自旋锁指针
 */
static inline void vtx_spinlock_destroy(vtx_spinlock_t* lock) {
#ifdef __linux__
    pthread_spin_destroy(lock);
#elif defined(__APPLE__)
    /* macOS os_unfair_lock不需要显式销毁 */
    (void)lock;
#endif
}

#ifdef __cplusplus
}
#endif

#endif /* VTX_SPINLOCK_H */
