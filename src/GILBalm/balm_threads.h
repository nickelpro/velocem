#ifndef VELOCEM_BALM_THREAD_H
#define VELOCEM_BALM_THREAD_H


#if __has_include(<threads.h>)
#include <threads.h>
#elif __has_include(<pthread.h>)
#include <pthread.h>
typedef pthread_mutex_t mtx_t;
#define mtx_plain NULL
#define mtx_init(mtx_ptr, attrs) pthread_mutex_init(mtx_ptr, attrs)
#define mtx_lock(mtx_ptr) pthread_mutex_lock(mtx_ptr)
#define mtx_unlock(mtx_ptr) pthread_mutex_unlock(mtx_ptr)
#else
#include <uv.h>
typedef uv_mutex_t mtx_t;
#define mtx_plain NULL
#define mtx_init(mtx_ptr, attrs) uv_mutex_init(mtx_ptr)
#define mtx_lock(mtx_ptr) uv_mutex_lock(mtx_ptr)
#define mtx_unlock(mtx_ptr) uv_mutex_unlock(mtx_ptr)
#endif // __has_include

#endif // VELOCEM_BALM_THREAD_H
