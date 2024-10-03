#ifndef THREADS_SYNCH_H
#define THREADS_SYNCH_H

#include <list.h>
#include <stdbool.h>
// 信号量
/** A counting semaphore. */
struct semaphore 
  {
    unsigned value;             /**< Current value. */
    struct list waiters;        /**< List of waiting threads. */
  };
//信号量初始化
void sema_init (struct semaphore *, unsigned value);
//信号量占用
void sema_down (struct semaphore *);
//尝试信号量占用
bool sema_try_down (struct semaphore *);
//信号量释放
void sema_up (struct semaphore *);
void sema_self_test (void);
//锁
/** Lock. */
struct lock 
  {
    struct list_elem elem;
    struct thread *holder;      /**< Thread holding lock (for debugging). */
    struct semaphore semaphore; /**< Binary semaphore controlling access. */
  };
//初始化锁
void lock_init (struct lock *);
//获取锁
void lock_acquire (struct lock *);
//尝试获取锁
bool lock_try_acquire (struct lock *);
//释放锁
void lock_release (struct lock *);
//当前线程是否持有锁
bool lock_held_by_current_thread (const struct lock *);
//条件变量
/** Condition variable. */
struct condition 
  {
    struct list waiters;        /**< List of waiting threads. */
  };
//初始化条件变量
void cond_init (struct condition *);
//条件等待
void cond_wait (struct condition *, struct lock *);
//释放信号
void cond_signal (struct condition *, struct lock *);
void cond_broadcast (struct condition *, struct lock *);

/** Optimization barrier.

   The compiler will not reorder operations across an
   optimization barrier.  See "Optimization Barriers" in the
   reference guide for more information.*/
#define barrier() asm volatile ("" : : : "memory")

#endif /**< threads/synch.h */
