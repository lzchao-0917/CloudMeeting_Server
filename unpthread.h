#ifndef UNPTHREAD_H
#define UNPTHREAD_H

/* Our own header for the programs that use threads.
   Include this file, instead of "unp.h". */
#include <pthread.h>
#include "unp.h"
typedef void * (THREAD_FUNC) (void *);

typedef struct
{
    pthread_t thread_tid;
}Thread;

typedef struct
{
    pid_t child_pid; //子进程的进程ID
    int child_pipefd; //父进程与子进程之间的管道的文件描述符
    int child_status; //子进程的状态，0表示子进程处于就绪状态
    int total; //可能表示子进程处理的客户端请求的总数
}Process;

typedef struct Room // single
{
    int navail; //可用的子进程的数量
    Process *pptr; //指向Process结构体的指针，它指向一个数组，该数组包含所有的子进程
    pthread_mutex_t lock; //这是一个互斥锁，用于在修改navail和pptr时提供线程安全

    Room (int n) //构造函数
    {
        navail = n; //n为进程数
        //使用Calloc函数分配内存，创建一个包含n个Process结构体的数组，并将pptr指向这个数组
        pptr = (Process *)Calloc(n, sizeof(Process));
        lock = PTHREAD_MUTEX_INITIALIZER; //初始化互斥锁
    }
}Room;

void	Pthread_create(pthread_t *, const pthread_attr_t *,
                       void * (*)(void *), void *);
void	Pthread_join(pthread_t, void **);
void	Pthread_detach(pthread_t);
void	Pthread_kill(pthread_t, int);

void	Pthread_mutexattr_init(pthread_mutexattr_t *);
void	Pthread_mutexattr_setpshared(pthread_mutexattr_t *, int);
void	Pthread_mutex_init(pthread_mutex_t *, pthread_mutexattr_t *);
void	Pthread_mutex_lock(pthread_mutex_t *);
void	Pthread_mutex_unlock(pthread_mutex_t *);

void	Pthread_cond_broadcast(pthread_cond_t *);
void	Pthread_cond_signal(pthread_cond_t *);
void	Pthread_cond_wait(pthread_cond_t *, pthread_mutex_t *);
void	Pthread_cond_timedwait(pthread_cond_t *, pthread_mutex_t *,
                               const struct timespec *);

void	Pthread_key_create(pthread_key_t *, void (*)(void *));
void	Pthread_setspecific(pthread_key_t, const void *);
void	Pthread_once(pthread_once_t *, void (*)(void));

void     Pthread_detach(pthread_t);


#endif // UNPTHREAD_H
