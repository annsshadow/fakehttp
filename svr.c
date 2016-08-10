/************************************************************************
    > File Name: epoll.c
    > Author: shadowwen
    > Mail: cravenboy@163.com
    > Created Time: Wed 27 Jul 2016 14:35:31 PM HKT
    > Function: simple test svr 
************************************************************************/

#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <sys/socket.h>
#include <signal.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <errno.h>
#include <assert.h>
#include <time.h>

//最大的事件队列数
#define MAX_EVENT 1024
//listen监听队列数
#define BACKLOG 128
//缓存数组最大长度
#define MAX_DATALEN 1024
//最大epoll打开数
#define MAX_EPOLL_SIZE 1024

typedef struct sockaddr SA;

//避免重启时一直无法监听端口
int set_tcp_reuse(int sock)
{
    int opt = 1;
    return setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (void *)&opt, sizeof(opt));
}

//设置TCP_NODELAY属性，关闭Nagle算法，让信息尽快发送出去
int set_tcp_nodelay(int sock)
{
    int opt = 1;
    return setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (void *)&opt, sizeof(opt));
}

//设置文件描述符为非阻塞
int set_non_block(int fd)
{
    int flags;
    //F_GETFL：获取文件描述符状态
    flags = fcntl(fd, F_GETFL, NULL);
    if (flags == -1) {
        perror("fcntl F_GETFL error");
        return -1;
    }
    flags |= O_NONBLOCK;
    //F_SETFL：设置文件描述符状态
    if (fcntl(fd, F_SETFL, flags) == -1) {
        perror("fcntl F_SETFL error");
        return -1;
    }
    return 0;
}

//忽略SIGPIPE状态，因为客户端关闭连接后，可能服务器还要发送数据
int ignore_sigpipe()
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(struct sigaction));
    sa.sa_handler = SIG_IGN;
    if (sigaction(SIGPIPE, &sa, NULL)) {
        perror("sigaction error");
        return -1;
    }
    return 0;
}


//创建TCP-socket
int create_tcp_server(const char *ip, uint16_t port, int backlog)
{
    int ret = -1;
    socklen_t len = 0;

    if (ignore_sigpipe()) {
        printf("setting ignore sigpipe failed\n");
        return -1;
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("create socket error");
        return -1;
    }
    struct sockaddr_in addr;
    //转换为网络序-短
    addr.sin_port = htons(port);
    addr.sin_family = AF_INET;
    if (!ip) {
        //转换为网络序-长
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    } else {
        ret = inet_pton(AF_INET, ip, (SA *)&addr.sin_addr);
        if (ret <= 0) {
            if (ret == 0) {
                fprintf(stderr, "not invalid ip: [%s]\n", ip);
            } else {
                perror("inet_pton error");
            }
            return -1;
        }
    }

    if (set_tcp_reuse(sock) == -1) {
        perror("setsockopt SO_REUSEADDR error");
        return -1;
    }

    if (set_tcp_nodelay(sock) == -1) {
        perror("setsockopt TCP_NODELAY error");
        return -1;
    }

    len = sizeof(SA);
    if (bind(sock, (SA *)&addr, len) == -1) {
        perror("bind error");
        return -1;
    }

    if (listen(sock, backlog) == -1) {
        perror("listen error");
        return -1;
    }

    return sock;
}

//注册可读-边缘的事件
int set_read_event(int epfd, int ctl, int fd)
{
    struct epoll_event ev;
    ev.data.u64 = 0;
    ev.data.fd = fd;
    //设置标志为‘当可读+边缘触发’
    ev.events = EPOLLIN | EPOLLET;
    //注册事件
    if (epoll_ctl(epfd, ctl, fd, &ev) == -1) {
        perror("epoll_ctl error");
        return -1;
    }
    return 0;
}

//服务器业务逻辑
int handle_recv(int fd)
{
    char buf[MAX_DATALEN] = {0};
    int n;

    bzero(buf, MAX_DATALEN);
    time_t t;
    struct tm *gmt;
    char *gmtp=NULL;
    tzset();
    t=time(NULL);
    gmt=gmtime(&t);
    gmtp=asctime(gmt);
    strcpy(buf,gmtp);
    //send并不保证对方能接到，但保证发到了网卡
    send(fd, buf, strlen(buf), 0);
    return 0;
}

int main(int argc, char *argv[])
{
    const char *ip = NULL;
    uint16_t port = 12321;
    int srv = create_tcp_server(ip, port, BACKLOG);
    if (srv < 0) {
        fprintf(stderr, "create tcp server failed\n");
        return -1;
    }

    struct epoll_event ev, events[MAX_EVENT];
    int epfd = -1;
    //2.6.8之后就忽略掉传入epoll_create的参数了
    if ((epfd = epoll_create(MAX_EPOLL_SIZE)) == -1) {
        perror("epoll_create error");
        return -1;
    }
    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN;
    ev.data.fd = srv;
    //注册新的sock到epfd中
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, srv, &ev) == -1) {
        perror("epoll_ctl add srv error");
        close(srv);
        return -1;
    }
    int i, ret, cli, fd;
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(SA);
    while (1) {
        //监控已经注册的事件，返回的是文件描述符的个数
        ret = epoll_wait(epfd, events, MAX_EVENT, -1);
        if (ret > 0) {
            for (i = 0; i < ret; i++) {
                fd = events[i].data.fd;
                if (events[i].events & EPOLLIN) {//客户端可读
                    if (fd == srv) {//有新的连接
                        cli = accept(srv, (SA *)&addr, &addrlen);
                        if (cli == -1) {
                            perror("accept error");
                        } else {
                            if (set_non_block(cli) == -1) {
                                fprintf(stderr, "set cli non block failed\n");
                                close(cli);
                                continue;
                            }
                            if (set_read_event(epfd, EPOLL_CTL_ADD, cli) != 0) {
                                close(cli);
                                continue;
                            }
                        }
                    } else {
                        if (handle_recv(fd) == -1) {//发送数据给fd失败
                            if (epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL)) {
                                perror("epoll_ctl EPOLL_CTL_DEL cli error");
                            }
                            close(fd);
                            continue;
                        }
                    }
                }
            }
        } else if (ret == 0) {
            //事件还没准备好就继续等待
            continue;
        } else {
            perror("epoll_wait error");
            continue;
        }
    }
    return 0;

}