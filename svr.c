/************************************************************************
    > File Name: svr.c
    > Author: shadowwen
    > Mail: cravenboy@163.com
    > Created Time: Wed 27 Jul 2016 14:35:31 PM HKT
    > Function: simple test svr 
************************************************************************/

#include <string.h> // bzero, strerror  
#include <errno.h> // errno  
#include <stdio.h>  // fprintf, fflush  
#include <stdlib.h>  // exit  
#include <sys/socket.h> // socket, bind, listen, accept  
#include <netinet/in.h> // struct socketaddr_t, struct in_addr, htonl, htons  
#include <unistd.h> // fork  
#include <signal.h> // signal  
#include <sys/types.h>
#include <sys/wait.h>   // waitpid  
#include <pthread.h>    // pthread_create
#include <time.h>   //for time

#define MAX_BUF_SIZE 512
#define MAX_LISTEN_QUEUE_LENGTH 512

int Socket(int domain, int type, int protocol) {
    int sock = socket(domain, type, protocol);
    if (sock < 0) {
        SYSERR("fail to init socket fd");
    }
    return sock;
}

void Listen(int sock) {
    int ret = listen(sock, MAX_LISTEN_QUEUE_LENGTH);
    if (ret != 0) {
        SYSERR("fail to listen on socket %d", sock);
    }
}

void Bind(int sock, const struct sockaddr_in* servaddr) {
    // use struct sockaddr for compatibility of history reason
    int ret = bind(sock, (const struct sockaddr*)servaddr, sizeof(struct sockaddr));
    if (ret != 0) {
        SYSERR("fail to bind socket %d", sock);
    }
}

int Accept(int sock, struct sockaddr_in* cliaddr, socklen_t* cli_len) {
    int ret = accept(sock, (struct sockaddr*)cliaddr, cli_len);
    NOTICE("accpet and give socket %d", ret);
    if (ret < 0) {
        SYSERR("fail to accept");
    }
    return ret;
}

void single_echo(int conn_sock) {
    char buf[MAX_BUF_SIZE];
    time_t t;
    struct tm *gmt;
    char *gmtp=NULL;
    tzset();
    t=time(NULL);
    gmt=gmtime(&t);
    gmtp=asctime(gmt);
    strcpy(buf,gmtp);
    send(conn_sock,buf,strlen(buf),MSG_DONTWAIT);
}

pthread_t PthreadCreate(int conn_sock, void* run(void*)) {
    pthread_t tid;
    int sock = conn_sock;
    int ret = pthread_create(&tid, NULL, run, (void*)&sock);
    if (ret) {
        POSIXERR(ret, "fail to create thread");
    }
    return tid;
}

void* thread_run(void* arg) {
    int sock = *(int*)arg;
    single_echo(sock);
    return NULL;
}

int main(int argc, char** argv) {

    // init port
    int port = 12321;

    // init sock fd
    int sock = Socket(PF_INET, SOCK_STREAM, 0);

    // init server sock address
    struct sockaddr_in  servaddr;
    bzero(&servaddr, sizeof(servaddr));

    // populate protocol fields
    servaddr.sin_family        = AF_INET;
    servaddr.sin_addr.s_addr   = htonl(INADDR_ANY);
    servaddr.sin_port          = htons(port);

    // bind sock fd and sock address
    Bind(sock, &servaddr);

    // start to listen
    Listen(sock);

    // init client address
    struct sockaddr_in cliaddr;
    socklen_t cli_len = sizeof(cliaddr);

    while (1) {
        // accpet connection request
        int conn_sock = Accept(sock, &cliaddr, &cli_len);
        PthreadCreate(conn_sock, thread_run);
    }
    return 0;

}