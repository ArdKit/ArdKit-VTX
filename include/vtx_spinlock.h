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
