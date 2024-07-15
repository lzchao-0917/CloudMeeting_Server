#include "unpthread.h"
#include "msg.h"
#include "unp.h"
#include <map>
#define SENDTHREADSIZE 5
SEND_QUEUE sendqueue; //save data

enum USER_TYPE
{
    GUEST=2,
    OWNER
};
static volatile int maxfd;
STATUS volatile roomstatus = ON;

typedef struct pool
{
    fd_set fdset;//一个文件描述符集合，用于select函数的参数
    pthread_mutex_t lock; //一个互斥锁，用于在修改fdset时提供线程安全
    int owner;
    int num;
    int status[1024 + 10]; //一个整数数组，用于存储每个文件描述符的状态
    std::map<int, uint32_t> fdToIp; //这是一个映射，将文件描述符映射到对应的IP地址
    pool() //构造函数
    {
        memset(status, 0, sizeof(status)); //将status数组的所有元素设置为0
        owner = 0;
        FD_ZERO(&fdset); //将fdset中的所有文件描述符清除
        lock = PTHREAD_MUTEX_INITIALIZER; //初始化互斥锁
        num = 0;
    }
    //成员函数
    void clear_room()
    {
        Pthread_mutex_lock(&lock); //锁定互斥锁，以防止其他线程同时修改Pool对象
        roomstatus = CLOSE; //将房间状态设置为CLOSE
        //遍历所有的文件描述符，如果文件描述符的状态是ON，那么关闭文件描述符
        for(int i = 0; i <= maxfd; i++)
        {
            if(status[i] == ON)
            {
                Close(i);
            }
        }
        //将status数组的所有元素设置为0
        memset(status, 0, sizeof(status));
        num = 0;
        owner = 0;
        FD_ZERO(&fdset); //将fdset中的所有文件描述符清除
        fdToIp.clear(); //清除fdToIp映射中的所有元素
        sendqueue.clear(); //清除发送队列中的所有消息
        Pthread_mutex_unlock(&lock); //解锁互斥锁
    }
}Pool;

Pool * user_pool = new Pool();

// room process
void process_main(int i, int fd) // room start
{
    //create accpet fd thread
    //打印一条消息，显示当前进程（房间）的ID
    printf("room %d starting \n", getpid());
    //忽略SIGPIPE信号。这个信号通常在写入一个没有读端的管道时产生
    Signal(SIGPIPE, SIG_IGN);
    //声明一个pthread_t类型的变量,用于存储新创建的线程的线程ID
    pthread_t pfd1;
    //以下三个函数为函数声明
    void* accept_fd(void *);
    void* send_func(void *);
    void  fdclose(int, int);

    //分配内存并存储文件描述符fd
    int *ptr = (int *)malloc(4);
    *ptr = fd;
    //创建一个新的线程，这个线程将执行accept_fd函数。ptr被传递给这个函数作为参数
    Pthread_create(&pfd1, NULL, accept_fd, ptr); // accept fd
    //创建SENDTHREADSIZE个新的线程，这些线程将执行send_func函数
    for(int i = 0; i < SENDTHREADSIZE; i++)
    {
        Pthread_create(&pfd1, NULL, send_func, NULL);
    }

    //进入一个无限循环，用于监听文件描述符和处理数据
    //listen read data from fds
    for(;;)
    {
        //获取用户池的文件描述符集合
        fd_set rset = user_pool->fdset;
        //声明并初始化一个timeval结构体，用于select函数的超时参数
        int nsel;
        struct timeval time;
        memset(&time, 0, sizeof(struct timeval));
        //使用select函数监听文件描述符。如果没有文件描述符准备好，那么更新文件描述符集合并继续监听
        while((nsel = Select(maxfd + 1, &rset, NULL, NULL, &time))== 0)
        {
            rset = user_pool->fdset; // make sure rset update
        }
        //遍历所有的文件描述符，检查哪些文件描述符准备好了
        for(int i = 0; i <= maxfd; i++)
        {
            //check data arrive
            if(FD_ISSET(i, &rset))
            {
                //读取11个字节的数据到head数组中
                char head[15] = {0};
                int ret = Readn(i, head, 11); // head size = 11
                if(ret <= 0) //如果读取的字节数小于等于0，那么打印一条消息并关闭文件描述符
                {
                    printf("peer close\n");
                    fdclose(i, fd);
                }
                
                else if(ret == 11) //如果读取的字节数等于11，那么处理数据
                {
                    if(head[0] == '$') //如果数据的第一个字符是’$'，那么这是一个有效的消息
                    {
                        //solve datatype(获取消息的类型)
                        MSG_TYPE msgtype;
                        memcpy(&msgtype, head + 1, 2);
                        msgtype = (MSG_TYPE)ntohs(msgtype);

                        //声明并初始化一个MSG结构体，用于存储消息的内容
                        MSG msg;
                        memset(&msg, 0, sizeof(MSG));
                        //获取消息的目标文件描述符、IP地址和长度
						msg.targetfd = i;
						memcpy(&msg.ip, head + 3, 4);
						int msglen;
						memcpy(&msglen, head + 7, 4);
						msg.len = ntohl(msglen);

                        //如果消息的类型是IMG_SEND、AUDIO_SEND或TEXT_SEND，那么读取消息的内容并将消息添加到发送队列
                        if(msgtype == IMG_SEND || msgtype == AUDIO_SEND || msgtype == TEXT_SEND)
                        {
                            msg.msgType = (msgtype == IMG_SEND) ? IMG_RECV : ((msgtype == AUDIO_SEND)? AUDIO_RECV : TEXT_RECV);
                            msg.ptr = (char *)malloc(msg.len);
                            msg.ip = user_pool->fdToIp[i];
                            if((ret = Readn(i, msg.ptr, msg.len)) < msg.len)
                            {
                                err_msg("3 msg format error");
                            }
                            else
                            {
                                int tail;
                                Readn(i, &tail, 1);
                                if(tail != '#')
                                {
                                    err_msg("4 msg format error");
                                }
                                else
                                {
                                    sendqueue.push_msg(msg);
                                }
                            }
                        }
                        //如果消息的类型是CLOSE_CAMERA，那么将消息添加到发送队列
						else if(msgtype == CLOSE_CAMERA)
						{
							char tail;
							Readn(i, &tail, 1);
							if(tail == '#' && msg.len == 0)
							{
								msg.msgType = CLOSE_CAMERA;
								sendqueue.push_msg(msg);
							}
							else //如果消息的格式不正确，那么打印一条错误消息
							{
								err_msg("camera data error ");
							}
						}
                    }
                    else //如果消息的格式不正确，那么打印一条错误消息
                    {
                        err_msg("1 msg format error");
                    }
                }
                else
                {
                    err_msg("2 msg format error");
                }
                if(--nsel <= 0) break; //如果所有准备好的文件描述符都已经处理完，那么跳出循环
            }
        }
    }
}

//file description close
void fdclose(int fd, int pipefd)
{

    if(user_pool->owner == fd) // room close
    {
        //room close
        user_pool->clear_room();
		printf("clear room\n");
        //write to father process
        char cmd = 'E';
        if(writen(pipefd, &cmd, 1) < 1)
        {
            err_msg("writen error");
        }
    }
    else
    {
        uint32_t getpeerip(int);
        uint32_t ip;
        //delete fd from pool
        Pthread_mutex_lock(&user_pool->lock);
        ip = user_pool->fdToIp[fd];
        FD_CLR(fd, &user_pool->fdset);
        user_pool->num--;
        user_pool->status[fd] = CLOSE;
        if(fd == maxfd) maxfd--;
        Pthread_mutex_unlock(&user_pool->lock);

        char cmd = 'Q';
        if(writen(pipefd, &cmd, 1) < 1)
        {
            err_msg("write error");
        }

        // msg ipv4

        MSG msg;
        memset(&msg, 0, sizeof(MSG));
        msg.msgType = PARTNER_EXIT;
        msg.targetfd = -1;
        msg.ip = ip; // network order
        Close(fd);
        sendqueue.push_msg(msg);
    }
}

void* accept_fd(void *arg) //accept fd from father，用于跟主进程通信
{
    //声明一个函数，该函数接受一个整数参数（文件描述符），并返回一个无符号32位整数（对等方的IP地址）
    uint32_t getpeerip(int);
    //将当前线程设置为分离状态。这意味着当线程结束时，其资源会立即被回收
    Pthread_detach(pthread_self());
    //从参数中获取文件描述符，并声明一个名为tfd的变量，初始值为-1
    int fd = *(int *)arg, tfd = -1;
    //释放参数所占用的内存
    free(arg);
    while(1)
    {
        int n, c;
        //从文件描述符fd读取数据。如果读取的字节数小于等于0，那么打印一条错误消息并退出
        if((n = read_fd(fd, &c, 1, &tfd)) <= 0)
        {
            err_quit("read_fd error");
        }
        //如果tfd小于0，那么打印一条错误消息并退出
        if(tfd < 0)
        {
            printf("c = %c\n", c);
            err_quit("no descriptor from read_fd");
        }
        //如果读取的字符是’C’，那么锁定用户池，将tfd添加到文件描述符集合，
        //设置tfd的状态为ON，更新maxfd，并将房间状态设置为ON。
        //然后，解锁用户池，创建一个消息，并将消息添加到发送队列
        //add to poll
        if(c == 'C') // create
        {
            Pthread_mutex_lock(&user_pool->lock); //lock
            FD_SET(tfd, &user_pool->fdset); //将文件描述符tfd添加到用户池的文件描述符集合中
            user_pool->owner = tfd; //将用户池的所有者设置为tfd
            //数获取文件描述符tfd的对等方的IP地址,
            //并将这个IP地址存储在用户池的fdToIp映射中，以tfd作为键
            user_pool->fdToIp[tfd] = getpeerip(tfd);
            user_pool->num++; //增加用户池的用户数量
//            user_pool->fds[user_pool->num++] = tfd;
            user_pool->status[tfd] = ON; //将tfd的状态设置为ON
            maxfd = MAX(maxfd, tfd); //更新最大的文件描述符
            //printf("c %d\n", maxfd);
            //write room No to  tfd
            roomstatus = ON; // 将房间状态设置为ON

            Pthread_mutex_unlock(&user_pool->lock); //unlock

            //创建一个新的消息，消息的类型是CREATE_MEETING_RESPONSE，
            //目标文件描述符是tfd，消息的内容是当前进程的进程ID（即房间号），并将消息添加到发送队列
            MSG msg;
            msg.msgType = CREATE_MEETING_RESPONSE;
            msg.targetfd = tfd;
            int roomNo = htonl(getpid());
            msg.ptr = (char *) malloc(sizeof(int));
            memcpy(msg.ptr, &roomNo, sizeof(int));
            msg.len = sizeof(int);
            sendqueue.push_msg(msg);

//            printf("create meeting: %d\n", tfd);

        }
        //如果读取的字符是’J’，那么锁定用户池。如果房间状态为CLOSE，那么关闭tfd并继续下一次循环。
        //否则，将tfd添加到文件描述符集合，更新maxfd，并解锁用户池。然后，创建两个消息，并将这两个消息添加到发送队列
        else if(c == 'J') // join
        {
            Pthread_mutex_lock(&user_pool->lock); //lock
            if(roomstatus == CLOSE) // meeting close (owner close)
            {
                close(tfd);
                Pthread_mutex_unlock(&user_pool->lock); //unlock
                continue;
            }
            else
            {

                FD_SET(tfd, &user_pool->fdset);
                user_pool->num++;
//                user_pool->fds[user_pool->num++] = tfd;
                user_pool->status[tfd] = ON;
                maxfd = MAX(maxfd, tfd);
                user_pool->fdToIp[tfd] = getpeerip(tfd);
                Pthread_mutex_unlock(&user_pool->lock); //unlock

                //broadcast to others
                //创建一个新的消息，消息的类型是PARTNER_JOIN，目标文件描述符是tfd，
                //消息的内容是空，消息的长度是0，消息的IP地址是tfd的对等方的IP地址，并将消息添加到发送队列
                MSG msg;
                memset(&msg, 0, sizeof(MSG));
                msg.msgType = PARTNER_JOIN;
                msg.ptr = NULL;
                msg.len = 0;
                msg.targetfd = tfd;
                msg.ip = user_pool->fdToIp[tfd];
                sendqueue.push_msg(msg);

                //broadcast to others
                //创建一个新的消息，消息的类型是PARTNER_JOIN2，目标文件描述符是tfd，
                //消息的内容是所有状态为ON的文件描述符的对等方的IP地址，并将消息添加到发送队列
                MSG msg1;
                memset(&msg1, 0, sizeof(MSG));
                msg1.msgType = PARTNER_JOIN2;
                msg1.targetfd = tfd;
                int size = user_pool->num * sizeof(uint32_t);

                msg1.ptr = (char *)malloc(size);
                int pos = 0;

                for(int i = 0; i <= maxfd; i++)
                {
                    if(user_pool->status[i] == ON && i != tfd)
                    {
                        uint32_t ip = user_pool->fdToIp[i];
                        memcpy(msg1.ptr + pos, &ip, sizeof(uint32_t));
                        pos += sizeof(uint32_t);
                        msg1.len += sizeof(uint32_t);
                    }
                }
                sendqueue.push_msg(msg1);

                printf("join meeting: %d\n", msg.ip);
            }
        }
    }
    return NULL;
}

void *send_func(void *arg)
{
    //将当前线程设置为分离状态
    Pthread_detach(pthread_self());
    //分配一个大小为4MB的缓冲区，用于存储要发送的数据
    char * sendbuf = (char *)malloc(4 * MB);
    /*
     * $_msgType_ip_size_data_#
    */

    for(;;)
    {
        //将缓冲区的内容设置为0
        memset(sendbuf, 0, 4 * MB);
        //从发送队列中取出一个消息
        MSG msg = sendqueue.pop_msg();
        int len = 0;
        //将’$'字符添加到缓冲区
        sendbuf[len++] = '$';
        //将消息的类型转换为网络字节序，并添加到缓冲区
        short type = htons((short)msg.msgType);
        memcpy(sendbuf + len, &type, sizeof(short)); //msgtype
        len+=2;
        //如果消息的类型是CREATE_MEETING_RESPONSE或PARTNER_JOIN2，那么跳过4个字节
        if(msg.msgType == CREATE_MEETING_RESPONSE || msg.msgType == PARTNER_JOIN2)
        {
            len += 4;
        }
        //如果消息的类型是TEXT_RECV、PARTNER_EXIT、PARTNER_JOIN、
        //IMG_RECV、AUDIO_RECV或CLOSE_CAMERA，那么将消息的IP地址添加到缓冲区
        else if(msg.msgType == TEXT_RECV || msg.msgType == PARTNER_EXIT || msg.msgType == PARTNER_JOIN || msg.msgType == IMG_RECV || msg.msgType == AUDIO_RECV || msg.msgType == CLOSE_CAMERA)
        {
            memcpy(sendbuf + len, &msg.ip, sizeof(uint32_t));
            len+=4;
        }
        //将消息的长度转换为网络字节序，并添加到缓冲区
        int msglen = htonl(msg.len);
        memcpy(sendbuf + len, &msglen, sizeof(int));
        len += 4;
        //将消息的内容添加到缓冲区
        memcpy(sendbuf + len, msg.ptr, msg.len);
        len += msg.len;
        //将’#'字符添加到缓冲区
        sendbuf[len++] = '#';

        //锁定用户池
		Pthread_mutex_lock(&user_pool->lock);
        //如果消息的类型是CREATE_MEETING_RESPONSE，那么将缓冲区的内容发送到目标文件描述符
        if(msg.msgType == CREATE_MEETING_RESPONSE)
        {
            //send buf to target
            if(writen(msg.targetfd, sendbuf, len) < 0)
            {
                err_msg("writen error");
            }
        }
        //如果消息的类型是PARTNER_EXIT、IMG_RECV、AUDIO_RECV、TEXT_RECV或CLOSE_CAMERA，
        //那么将缓冲区的内容发送到所有状态为ON的文件描述符，除了目标文件描述符
        else if(msg.msgType == PARTNER_EXIT || msg.msgType == IMG_RECV || msg.msgType == AUDIO_RECV || msg.msgType == TEXT_RECV || msg.msgType == CLOSE_CAMERA)
        {
            for(int i = 0; i <= maxfd; i++)
            {
                if(user_pool->status[i] == ON && msg.targetfd != i)
                {
                    if(writen(i, sendbuf, len) < 0)
                    {
                        err_msg("writen error");
                    }
                }
            }
        }
        //如果消息的类型是PARTNER_JOIN，那么将缓冲区的内容发送到所有状态为ON的文件描述符，除了目标文件描述符
        else if(msg.msgType == PARTNER_JOIN)
        {
            for(int i = 0; i <= maxfd; i++)
            {
                if(user_pool->status[i] == ON && i != msg.targetfd)
                {
                    if(writen(i, sendbuf, len) < 0)
                    {
                        err_msg("writen error");
                    }
                }
            }
        }
        //如果消息的类型是PARTNER_JOIN2，那么将缓冲区的内容发送到目标文件描述符，如果其状态为ON
        else if(msg.msgType == PARTNER_JOIN2)
        {
            for(int i = 0; i <= maxfd; i++)
            {
                if(user_pool->status[i] == ON && i == msg.targetfd)
                {
                    if(writen(i, sendbuf, len) < 0)
                    {
                        err_msg("writen error");
                    }
                }
            }
        }
        //解锁用户池
		Pthread_mutex_unlock(&user_pool->lock);
        //如果消息的内容不为空，那么释放消息的内容所占用的内存
        //free
        if(msg.ptr)
        {
            free(msg.ptr);
            msg.ptr = NULL;
        }
    }
	free(sendbuf);

    return NULL;
}
