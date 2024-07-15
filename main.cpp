#include <iostream>
#include "unpthread.h"
#include "unp.h"
using namespace std;

Thread * tptr;
socklen_t addrlen;
int listenfd;
int navail, nprocesses;

Room *room;
//typedef struct Room_Pool
//{
//    Room *room_pool[100]; //pool
//    int num;

//    Room_Pool()
//    {
//        memset(room_pool, 0, sizeof(room_pool));
//        num = 0;
//    }
//}Room_Pool;

int main(int argc, char **argv)
{
    void sig_chld(int signo);
    Signal(SIGCHLD, sig_chld);
    int i,maxfd;
    void thread_make(int);
    void process_make(int, int);

    fd_set rset, masterset;
    FD_ZERO(&masterset);

    if(argc == 4)
    {
        listenfd = Tcp_listen(NULL, argv[1], &addrlen);
    }
    else if(argc == 5)
    {
        listenfd = Tcp_listen(argv[1], argv[2], &addrlen);
    }
    else
    {
        err_quit("usage: ./app [host] <port #> <#threads> <#processes>");
    }
    maxfd = listenfd;
    int nthreads = atoi(argv[argc - 2]);
    nprocesses = atoi(argv[argc-1]);

    //init room
    room = new Room(nprocesses);

    printf("total threads: %d  total process: %d\n", nthreads, nprocesses);

    tptr = (Thread *)Calloc(nthreads, sizeof(Thread));

    //process pool----room
    for(i = 0; i < nprocesses; i++)
    {
        process_make(i, listenfd);
        FD_SET(room->pptr[i].child_pipefd, &masterset);
        maxfd = max(maxfd, room->pptr[i].child_pipefd);
    }

    //thread pool
    for(i = 0; i < nthreads; i++)
    {
        thread_make(i);
    }
    for(;;)
    {
        //listen，masterset（包含所有子进程的管道文件描述符的集合）赋值给rset
        rset = masterset;
        //调用Select函数监听rset中的所有文件描述符。Select函数返回准备好的文件描述符的数量
        int nsel = Select(maxfd + 1, &rset, NULL, NULL, NULL);
        if(nsel == 0) continue; //如果没有文件描述符准备好，那么跳过当前的迭代，继续下一次循环

        //set room status to 0(empty)
        for(i = 0; i < nprocesses; i++)
        {
            //如果子进程的管道文件描述符在rset中，那么执行以下操作
            if(FD_ISSET(room->pptr[i].child_pipefd, &rset))
            {
                //声明两个变量
                char rc; //rc用于存储从管道读取的字符
				int  n; // n用于存储Readn函数的返回值
                //从子进程的管道读取一个字符。如果读取的字节数小于等于0，那么打印一条错误消息并退出。
                if((n = Readn(room->pptr[i].child_pipefd, &rc, 1)) <= 0)
                {
                    err_quit("child %d terminated unexpectedly", i);
                }
				printf("c = %c\n", rc); //打印从管道读取的字符
                if(rc == 'E') // room empty,子进程现在是空闲
                {
                    pthread_mutex_lock(&room->lock); //锁定房间，以防止其他线程同时修改房间的状态
                    room->pptr[i].child_status = 0; //将子进程的状态设置为0（表示空闲）
                    room->navail++; //增加可用的子进程的数量
                    printf("room %d is now free\n", room->pptr[i].child_pid); //打印一条消息，显示子进程现在是空闲的
                    pthread_mutex_unlock(&room->lock); //解锁房间

                }
                else if(rc == 'Q') // partner quit,子进程的一个客户端已经退出
                {
                    Pthread_mutex_lock(&room->lock);
                    room->pptr[i].total--; //减少子进程处理的客户端的数量
                    Pthread_mutex_unlock(&room->lock);
                }
                else // trash data
                {
                    //打印一条错误消息并跳过当前的迭代，继续下一次循环
                    err_msg("read from %d error", room->pptr[i].child_pipefd);
                    continue;
                }
                if(--nsel == 0) break; /*all done with select results*/
            }

        }
    }
    return 0;
}


// create threads
void thread_make(int i)
{
    void * thread_main(void *);
    //这两行代码创建了一个新的整数，并将其设置为i。这个整数将作为参数传递给thread_main函数
    int *arg = (int *) Calloc(1, sizeof(int));
    *arg = i;
    //这行代码创建了一个新的线程。新线程的线程ID存储在tptr[i].thread_tid中，
    //新线程将执行thread_main函数，arg作为参数传递给thread_main函数
    Pthread_create(&tptr[i].thread_tid, NULL, thread_main, arg);
}


void process_make(int i, int listenfd)
{
    //定义一个包含两个元素的整数数组sockfd，用于存储管道的两个文件描述符
    int sockfd[2];
    //定义一个类型为pid_t的变量pid，用于存储新创建的子进程的进程ID
    pid_t pid;
    void process_main(int, int);
    //创建一个双向的UNIX域套接字对，两个套接字的文件描述符存储在sockfd数组中
    Socketpair(AF_LOCAL, SOCK_STREAM, 0, sockfd);
    //创建一个新的子进程。
    //如果当前进程是父进程，那么fork函数返回新创建的子进程的进程ID，这个值大于0；
    //如果当前进程是新创建的子进程，那么fork函数返回0
    if((pid = fork()) > 0)
    {
        //在父进程中关闭管道的一端。因为父进程只需要通过管道读取数据，所以关闭写端
        Close(sockfd[1]);
        //将新创建的子进程的进程ID存储在room对象的pptr数组的第i个元素的child_pid字段中
        room->pptr[i].child_pid = pid;
        //将管道的读端的文件描述符存储在room对象的pptr数组的第i个元素的child_pipefd字段中
        room->pptr[i].child_pipefd = sockfd[0];
        //将room对象的pptr数组的第i个元素的child_status字段设置为0，表示子进程当前是空闲的
        room->pptr[i].child_status = 0;
        //将room对象的pptr数组的第i个元素的total字段设置为0，表示子进程当前没有处理任何客户端的请求
        room->pptr[i].total = 0;
        //返回新创建的子进程的进程ID
        //return pid; // father
    }
    //如果当前进程是子进程，那么关闭监听套接字，因为子进程不需要接受新的客户端连接
    Close(listenfd); // child not need this open
    //在子进程中关闭管道的另一端。因为子进程只需要通过管道发送数据，所以关闭读端。
    Close(sockfd[0]);
    //调用process_main函数，开始在子进程中处理客户端的请求。注意，这个函数永远不会返回
    process_main(i, sockfd[1]); /* never returns */
}
