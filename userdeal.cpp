#include "unpthread.h"
#include <stdlib.h>
#include "unp.h"
#include "netheader.h"
#include "msg.h"

pthread_mutex_t mlock = PTHREAD_MUTEX_INITIALIZER; // accept lock
extern socklen_t addrlen;
extern int listenfd;
extern int nprocesses;
extern Room *room;

void* thread_main(void *arg)
{
    void dowithuser(int connfd);
    //这行代码获取了传递给thread_main函数的整数参数
    int i = *(int *)arg;
    free(arg); //free
    //这行代码声明了一个名为connfd的整数，用于存储连接的文件描述符
    int connfd;
    //这行代码将当前线程设置为分离状态
    Pthread_detach(pthread_self()); //detach child thread

//    printf("thread %d starting\n", i);

    //这三行代码声明并初始化了一个套接字地址结构体cliaddr，用于存储接受的连接的地址
    SA *cliaddr;
    socklen_t clilen;
    cliaddr = (SA *)Calloc(1, addrlen);
    char buf[MAXSOCKADDR];
    for(;;)
    {
        //这行代码将clilen设置为addrlen，clilen是一个套接字地址的长度，addrlen是监听套接字地址的长度
        clilen = addrlen;
        //lock accept,锁定了一个互斥锁
        Pthread_mutex_lock(&mlock);
        // 接受一个新的连接，新连接的文件描述符存储在connfd中，新连接的地址存储在cliaddr中
        connfd = Accept(listenfd, cliaddr, &clilen);
        //unlock accept,解锁了互斥锁，允许其他线程接受新的连接
        Pthread_mutex_unlock(&mlock);
        //打印了一个消息，显示了新连接的地址
        printf("connection from %s\n", Sock_ntop(buf, MAXSOCKADDR, cliaddr, clilen));

        //主要作用是处理与特定客户端的交互。
        //它接收一个客户端的连接文件描述符connfd作为参数，
        //然后读取和解析从客户端发送过来的数据，并根据数据的类型执行相应的操作
        dowithuser(connfd); // process user


    }
    return NULL;
}


/*
 *
 *read data from client
 *
 */

void dowithuser(int connfd)
{
    void writetofd(int fd, MSG msg);

    char head[15]  = {0}; //声明并初始化一个字符数组，用于存储从客户端读取的数据的头部
    //read head,用于不断地读取和处理从客户端发送过来的数据
    while(1)
    {
        ssize_t ret = Readn(connfd, head, 11);//从客户端的连接文件描述符读取11个字节的数据到head数组中
        //如果读取的字节数小于等于0，那么关闭连接，打印一条消息，并退出函数
        if(ret <= 0)
        {
            close(connfd); //close
            printf("%d close\n", connfd);
            return;
        }
        else if(ret < 11) //如果读取的字节数小于11，那么打印一条消息
        {
            printf("data len too short\n");
        }
        else if(head[0] != '$') //如果数据的第一个字符不是’$'，那么打印一条消息
        {
            printf("data format error\n");
        }
        else //如果数据的第一个字符是’$'，那么执行以下操作
        {
            //solve datatype,获取消息的类型
            MSG_TYPE msgtype;
            memcpy(&msgtype, head + 1, 2);
            msgtype = (MSG_TYPE)ntohs(msgtype);

            //solve ip,获取消息的IP地址
            uint32_t ip;
            memcpy(&ip, head + 3, 4);
            ip = ntohl(ip);

            //solve datasize,获取消息的数据大小
            uint32_t datasize;
            memcpy(&datasize, head + 7, 4);
            datasize = ntohl(datasize);

    //        printf("msg type %d\n", msgtype);

            if(msgtype == CREATE_MEETING)
            {
                //读取数据的尾部
                char tail;
                Readn(connfd, &tail, 1);
                //read data from client,如果数据的大小为0并且尾部字符是’#'，那么执行以下操作
                if(datasize == 0 && tail == '#')
                {
                    //打印一条消息，显示创建会议的IP地址
                    char *c = (char *)&ip;
                    printf("create meeting  ip: %d.%d.%d.%d\n", (u_char)c[3], (u_char)c[2], (u_char)c[1], (u_char)c[0]);
                    //如果没有可用的房间，那么执行以下操作
                    if(room->navail <=0) 
                    {
                        MSG msg;//创建一个新的消息
                        memset(&msg, 0, sizeof(msg));
                        msg.msgType = CREATE_MEETING_RESPONSE; //消息的类型是CREATE_MEETING_RESPONSE
                        int roomNo = 0; //消息的内容是房间号（这里是0，表示没有可用的房间）
                        msg.ptr = (char *) malloc(sizeof(int));
                        memcpy(msg.ptr, &roomNo, sizeof(int));
                        msg.len = sizeof(roomNo);
                        writetofd(connfd, msg); //并将消息写入到客户端的连接文件描述符
                    }
                    else //如果有可用的房间，那么执行以下操作
                    {
                        int i;
                        //锁定房间，并找到一个空闲的房间
                        Pthread_mutex_lock(&room->lock);
                        for(i = 0; i < nprocesses; i++)
                        {
                            if(room->pptr[i].child_status == 0) break;
                        }
                        
                        if(i == nprocesses) //如果没有找到空闲的房间，那么执行以下操作
                        {
                            //创建一个新的消息，消息的类型是CREATE_MEETING_RESPONSE，
                            //消息的内容是房间号（这里是0，表示没有空闲的房间），
                            //并将消息写入到客户端的连接文件描述符
                            MSG msg;
                            memset(&msg, 0, sizeof(msg));
                            msg.msgType = CREATE_MEETING_RESPONSE;
                            int roomNo = 0;
                            msg.ptr = (char *) malloc(sizeof(int));
                            memcpy(msg.ptr, &roomNo, sizeof(int));
                            msg.len = sizeof(roomNo);
                            writetofd(connfd, msg);
                        }
                        else //如果找到了一个空闲的房间，那么执行以下操作
                        {
                            //向子进程的管道写入一个字符’C’，表示创建一个新的会议。如果写入失败，那么打印一条错误消息
                            char cmd = 'C';
                            if(write_fd(room->pptr[i].child_pipefd, &cmd, 1, connfd) < 0)
                            {
                                printf("write fd error");
                            }
                            else //如果写入成功，那么执行以下操作
                            {
                                close(connfd); //关闭客户端的连接
                                printf("room %d empty\n", room->pptr[i].child_pid); //打印一条消息，显示房间现在是空闲的。
                                room->pptr[i].child_status = 1; // occupy,将子进程的状态设置为1（表示被占用）
                                room->navail--; //减少可用的房间的数量
                                room->pptr[i].total++; //增加子进程处理的客户端的数量
                                Pthread_mutex_unlock(&room->lock);
                                return;
                            }
                        }
                        Pthread_mutex_unlock(&room->lock);

                    }
                }
                else //如果数据的大小不为0或者尾部字符不是’#'，那么打印一条消息
                {
                    printf("1 data format error\n");
                }
            }
            else if(msgtype == JOIN_MEETING) //如果消息的类型是JOIN_MEETING，那么执行以下操作
            {
                //read msgsize,获取消息的大小
                uint32_t msgsize, roomno;
                memcpy(&msgsize, head + 7, 4);
                msgsize = ntohl(msgsize);
                //read data + #, 读取消息的内容和尾部字符
                int r =  Readn(connfd, head, msgsize + 1 );
                if(r < msgsize + 1) //如果读取的字节数小于消息的大小加1，那么打印一条消息
                {
                    printf("data too short\n");
                }
                else //如果读取的字节数等于消息的大小加1，那么执行以下操作
                {
                    if(head[msgsize] == '#') //如果尾部字符是’#'
                    {
                        //获取房间号
                        memcpy(&roomno, head, msgsize);
                        roomno = ntohl(roomno);
    //                    printf("room : %d\n", roomno);
                        //find room no,查找指定的房间。如果找到了房间并且房间的状态是1（表示被占用），那么将ok设置为true
                        bool ok = false;
                        int i;
                        for(i = 0; i < nprocesses; i++)
                        {
                            if(room->pptr[i].child_pid == roomno && room->pptr[i].child_status == 1)
                            {
                                ok = true; //find room
                                break;
                            }
                        }

                        //创建一个新的消息，消息的类型是JOIN_MEETING_RESPONSE，消息的长度是uint32_t的大小
                        MSG msg;
                        memset(&msg, 0, sizeof(msg));
                        msg.msgType = JOIN_MEETING_RESPONSE;
                        msg.len = sizeof(uint32_t);
                        if(ok) //如果找到了指定的房间，那么执行以下操作
                        {
                            if(room->pptr[i].total >= 1024) //如果房间的客户端数量大于等于1024，
                            {
                                //将消息的内容设置为-1，表示房间已满，并将消息写入到客户端的连接文件描述符
                                msg.ptr = (char *)malloc(msg.len);
                                uint32_t full = -1;
                                memcpy(msg.ptr, &full, sizeof(uint32_t));
                                writetofd(connfd, msg);
                            }
                            else //如果房间的客户端数量小于1024
                            {
                                Pthread_mutex_lock(&room->lock);

                                //向子进程的管道写入一个字符’J’，表示有一个新的客户端请求加入房间。
                                char cmd = 'J';
    //                            printf("i  =  %d\n", i);
                                //如果写入失败，那么打印一条错误消息
                                if(write_fd(room->pptr[i].child_pipefd, &cmd, 1, connfd) < 0)
                                {
                                    err_msg("write fd:");
                                }
                                else //如果写入成功
                                {

                                    msg.ptr = (char *)malloc(msg.len);
                                    memcpy(msg.ptr, &roomno, sizeof(uint32_t));
                                    writetofd(connfd, msg);
                                    room->pptr[i].total++;// add 1,增加子进程处理的客户端的数量
                                    Pthread_mutex_unlock(&room->lock);
                                    close(connfd); //关闭客户端的连接
                                    return;
                                }
                                Pthread_mutex_unlock(&room->lock);
                            }
                        }
                        else
                        {
                            msg.ptr = (char *)malloc(msg.len);
                            uint32_t fail = 0;
                            memcpy(msg.ptr, &fail, sizeof(uint32_t));
                            writetofd(connfd, msg);
                        }
                    }
                    else
                    {
                        printf("format error\n");
                    }
                }
            }
            else
            {
                printf("data format error\n");
            }
        }
    }
}


void writetofd(int fd, MSG msg)//接受两个参数：一个文件描述符fd和一个消息msg
{
    char *buf = (char *) malloc(100); //分配一个大小为100字节的缓冲区，用于存储要写入的数据
    memset(buf, 0, 100); //将缓冲区的所有字节设置为0
    int bytestowrite = 0; //声明一个整数变量，用于跟踪要写入的字节数
    buf[bytestowrite++] = '$'; //将’$'字符添加到缓冲区的开始位置，并将要写入的字节数增加1

    //获取消息的类型，将其转换为网络字节序，并将其添加到缓冲区
    uint16_t type = msg.msgType;
    type = htons(type);
    memcpy(buf + bytestowrite, &type, sizeof(uint16_t));

    bytestowrite += 2;//skip type,将要写入的字节数增加2，因为消息类型占用了2个字节

    bytestowrite += 4; //skip ip，将要写入的字节数增加4，这里跳过了IP地址的部分

    //获取消息的长度，将其转换为网络字节序，并将其添加到缓冲区
    uint32_t size = msg.len;
    size = htonl(size);
    memcpy(buf + bytestowrite, &size, sizeof(uint32_t));
    bytestowrite += 4; //将要写入的字节数增加4，因为消息长度占用了4个字节

    memcpy(buf + bytestowrite, msg.ptr, msg.len); //将消息的内容添加到缓冲区
    bytestowrite += msg.len; //将要写入的字节数增加msg.len，因为消息的内容占用了msg.len个字节

    buf[bytestowrite++] = '#'; //将’#'字符添加到缓冲区的末尾，并将要写入的字节数增加1

    //将缓冲区的内容写入到文件描述符。如果写入的字节数小于要写入的字节数，那么打印一条错误消息。
    if(writen(fd, buf, bytestowrite) < bytestowrite)
    {
        printf("write fail\n");
    }

    //如果消息的内容不为空，那么释放消息的内容所占用的内存，并将msg.ptr设置为NULL
    if(msg.ptr)
    {
        free(msg.ptr);
        msg.ptr = NULL;
    }
    free(buf); //释放缓冲区所占用的内存
}
