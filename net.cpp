#include "unp.h"
/*
 * get peer ipv4 (network order)
 */
uint32_t getpeerip(int fd)
{
    sockaddr_in peeraddr;
    socklen_t addrlen;
    if(getpeername(fd, (sockaddr *)&peeraddr, &addrlen) < 0)
    {
        err_msg("getpeername error");
        return -1;
    }
    return peeraddr.sin_addr.s_addr;
}


int Select(int nfds, fd_set * readfds, fd_set * writefds, fd_set * execpfds, struct timeval *timeout)
{
    int n;
    for(;;)
    {
        if((n = select(nfds, readfds, writefds, execpfds, timeout)) < 0)
        {
            if(errno == EINTR) continue;
            else err_quit("select error");
        }
        else break;
    }
    return n; //can return 0 on timeout
}


ssize_t	Readn(int fd, void * buf, size_t size)
{
    ssize_t lefttoread = size, hasread = 0;
    char *ptr = (char *)buf;
    while(lefttoread > 0)
    {
        if((hasread = read(fd, ptr, lefttoread))<0)
        {
            if(errno == EINTR)
            {
                hasread = 0;
            }
            else
            {
                return -1;
            }
        }
        else if(hasread == 0) //eof
        {
            break;
        }
        lefttoread -= hasread;
        ptr += hasread;
    }
    return size - lefttoread;
}

ssize_t writen(int fd, const void * buf, size_t n)
{
    ssize_t lefttowrite = n, haswrite = 0;
    char *ptr = (char *)buf;
    while(lefttowrite > 0)
    {
        if((haswrite = write(fd, ptr, lefttowrite)) < 0)
        {
            if(haswrite < 0 && errno == EINTR)
            {
                haswrite = 0;
            }
            else
            {
                return -1; //error
            }
        }
        lefttowrite -= haswrite;
        ptr += haswrite;
    }
    return n;
}



const char *Sock_ntop(char * str, int size ,const sockaddr * sa, socklen_t salen)
{
    switch (sa->sa_family)
    {
    case AF_INET:
        {
            struct sockaddr_in *sin = (struct sockaddr_in *) sa;
            if(inet_ntop(AF_INET, &sin->sin_addr, str, size) == NULL)
            {
                err_msg("inet_ntop error");
                return NULL;
            }
            if(ntohs(sin->sin_port) > 0)
            {
                snprintf(str + strlen(str), size  -  strlen(str), ":%d", ntohs(sin->sin_port));
            }
            return str;
        }
    case AF_INET6:
        {
            struct sockaddr_in6 *sin = (struct sockaddr_in6 *) sa;
            if(inet_ntop(AF_INET6, &sin->sin6_addr, str, size) == NULL)
            {
                err_msg("inet_ntop error");
                return NULL;
            }
            if(ntohs(sin->sin6_port) > 0)
            {
                snprintf(str + strlen(str), size -  strlen(str), ":%d", ntohs(sin->sin6_port));
            }
            return str;
        }
     default:
        return "server error";
    }
    return NULL;
}


void Setsockopt(int fd, int level, int optname, const void * optval, socklen_t optlen)
{
    if(setsockopt(fd, level, optname, optval, optlen) < 0)
    {
        err_msg("setsockopt error");
    }
}

void Close(int fd)
{
    if(close(fd) < 0)
    {
        err_msg("Close error");
    }
}

void Listen(int fd, int backlog)
{
    if(listen(fd, backlog) < 0)
    {
        err_quit("listen error");
    }
}

int	Tcp_connect(const char * host, const char * serv)
{
    int sockfd, n;
    struct addrinfo hints, *res, *ressave;
    bzero(&hints, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if((n = getaddrinfo(host, serv, &hints, &res)) != 0)
    {
        err_quit("tcp_connect error for %s, %s: %s", host, serv, gai_strerror(n));
    }

    ressave = res;
    do
    {
        sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if(sockfd < 0) continue;
        if(connect(sockfd, res->ai_addr, res->ai_addrlen) == 0) break;
        close(sockfd);
    }while((res = res->ai_next) != NULL);

    if(res == NULL)
    {
        err_quit("tcp_connect error for %s,%s", host, serv);
    }
    freeaddrinfo(ressave);
    return sockfd;
}

//它接受三个参数：一个主机名（或者IP地址）、一个服务名（或者端口号）和一个指向套接字地址长度的指针
int Tcp_listen(const char * host, const char * service, socklen_t * addrlen)
{
    int listenfd, n;
    const int on = 1;
    //声明三个指向addrinfo结构体的指针，
    //hints用于指定getaddrinfo函数的行为，调用者在这个结构中填入关于期望返回的信息类型的暗示
    //res用于存储getaddrinfo函数返回的地址信息，
    //ressave用于保存地址信息的头指针
    struct addrinfo hints, *res, *ressave;
    bzero(&hints, sizeof(struct addrinfo)); //将hints结构体的所有字节设置为0

    hints.ai_flags = AI_PASSIVE; //设置了AI_PASSIVE标志，但没有指定主机名，那么return ipv6和ipv4通配地址
    hints.ai_family = AF_UNSPEC; //返回的是适用于指定主机名和服务名且适合任意协议族的地址
    hints.ai_socktype = SOCK_STREAM;//返回的地址将用于流式套接字（TCP）

    char addr[MAXSOCKADDR]; //声明一个字符数组，用于存储套接字地址的字符串表示

    //getaddrinfo函数根据给定的主机名和服务名，返回一个struct addrinfo结构链表,每个struct addrinfo结构都包含一个互联网地址
    //成功返回0，失败返回非0
    if((n = getaddrinfo(host, service, &hints, &res)) > 0)
    {
        err_quit("tcp listen error for %s %s: %s", host, service, gai_strerror(n));
    }
	ressave = res; //保存地址信息的头指针
	do
	{
        //对于每个地址，创建一个新的套接字
		listenfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
		if(listenfd < 0) //如果套接字的文件描述符小于0，那么跳过当前的迭代，继续下一次迭代
		{
			continue; //error try again
		}
        //设置套接字选项，允许重用本地地址
        Setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
        //尝试将套接字绑定到当前的地址。如果绑定成功，那么打印一条消息并跳出循环
        if(bind(listenfd, res->ai_addr, res->ai_addrlen) == 0)
        {
            printf("server address: %s\n", Sock_ntop(addr, MAXSOCKADDR, res->ai_addr, res->ai_addrlen));
            break; //success
        }
        Close(listenfd); //如果绑定失败，那么关闭套接字
	}while((res = res->ai_next) != NULL); //如果还有更多的地址，那么继续下一次迭代
    freeaddrinfo(ressave); //free,释放地址信息所占用的内存

    if(res == NULL)
    {
        err_quit("tcp listen error for %s: %s", host, service);
    }

    Listen(listenfd, LISTENQ); //将套接字设置为监听状态，准备接受传入的连接

    if(addrlen) //如果addrlen不为NULL，那么将监听套接字地址的长度存储在addrlen指向的变量中
    {
        *addrlen = res->ai_addrlen;
    }

    return listenfd; //返回监听套接字的文件描述符
}

int Accept(int listenfd, SA * addr, socklen_t *addrlen)
{
    int n;
    for(;;)
    {
        if((n = accept(listenfd, addr, addrlen)) < 0)
        {
            if(errno == EINTR)
                continue;
            else
                err_quit("accept error");
        }
        else
        {
            return n;
        }
    }
}
void Socketpair(int family, int type, int protocol, int * sockfd)
{
    if(socketpair(family, type, protocol, sockfd) < 0)
    {
        err_quit("socketpair error");
    }
}

ssize_t write_fd(int fd, void *ptr, size_t nbytes, int sendfd)
{
    struct msghdr msg;
    struct iovec iov[1];

    union{
        struct cmsghdr cm;
        char control[CMSG_SPACE(sizeof(int))];
    }control_un;
    struct cmsghdr *cmptr;

    msg.msg_control = control_un.control;
    msg.msg_controllen = sizeof(control_un.control);

    cmptr = CMSG_FIRSTHDR(&msg);
    cmptr->cmsg_len = CMSG_LEN(sizeof(int));
    cmptr->cmsg_level = SOL_SOCKET;
    cmptr->cmsg_type = SCM_RIGHTS;
    *((int *) CMSG_DATA(cmptr)) = sendfd;

    msg.msg_name = NULL;
    msg.msg_namelen = 0;

    iov[0].iov_base = ptr;
    iov[0].iov_len = nbytes;
    msg.msg_iov = iov;
    msg.msg_iovlen = 1;

    return (sendmsg(fd, &msg, 0));
}


ssize_t Write_fd(int fd, void *ptr, size_t nbytes, int sendfd)
{
    ssize_t n;
    if((n = write_fd(fd, ptr, nbytes, sendfd)) < 0)
    {
        err_quit("write fd error");
    }
    return n;
}

//参数：一个文件描述符fd(sockfd[1]:与父进程通信的文件描述符)，一个指向缓冲区的指针ptr，一个字节数nbytes，和一个指向接收文件描述符的指针recvfd
ssize_t read_fd(int fd, void *ptr, size_t nbytes, int *recvfd)
{
    struct msghdr msg; //声明一个msghdr结构体，用于存储消息的信息
    struct iovec iov[1]; //声明一个iovec数组，用于存储数据的向量
    ssize_t n; //声明一个变量n，用于存储recvmsg函数的返回值

    //声明一个联合体，用于存储控制信息
    union {
        struct cmsghdr cm;
        char control[CMSG_SPACE(sizeof(int))];
    }control_un;

    struct cmsghdr *cmptr; //声明一个指向cmsghdr结构体的指针，用于遍历控制信息
    //将msg的控制信息字段设置为control_un.control，并设置控制信息的长度
    msg.msg_control = control_un.control;
    msg.msg_controllen = sizeof(control_un.control);

    //-------------------------
    //将msg的名字字段设置为NULL，名字的长度设置为0。这是因为我们不需要关心消息的源地址
    msg.msg_name = NULL;
    msg.msg_namelen = 0;

    //将iov的基地址设置为ptr，长度设置为nbytes
    iov[0].iov_base = ptr;
    iov[0].iov_len = nbytes;
    //将msg的数据向量字段设置为iov，数据向量的长度设置为1
    msg.msg_iov = iov;
    msg.msg_iovlen = 1;

    //调用recvmsg函数从文件描述符fd接收消息。如果函数返回值小于0，那么返回n
    if((n  = recvmsg(fd, &msg, MSG_WAITALL)) < 0)
    {
        return n;
    }

    //获取消息的第一个控制信息。如果控制信息存在并且长度正确，那么执行以下操作
    if((cmptr = CMSG_FIRSTHDR(&msg)) != NULL && cmptr->cmsg_len == CMSG_LEN(sizeof(int)))
    {
        //如果控制信息的级别不是SOL_SOCKET，那么打印一条错误消息
        if(cmptr->cmsg_level != SOL_SOCKET) 
        {
            err_msg("control level != SOL_SOCKET");
        }
        //如果控制信息的类型不是SCM_RIGHTS，那么打印一条错误消息
        if(cmptr->cmsg_type != SCM_RIGHTS) 
        {
            err_msg("control type != SCM_RIGHTS");
        }
        *recvfd = *((int *) CMSG_DATA(cmptr)); //获取控制信息的数据，这个数据就是传递过来的文件描述符
    }
    else //如果没有控制信息或者控制信息的长度不正确，那么将recvfd设置为-1，表示没有文件描述符被传递过来
    {
        *recvfd = -1; // descroptor was not passed
    }

    return n;//返回recvmsg函数的返回值，这个值表示接收到的字节数
}
