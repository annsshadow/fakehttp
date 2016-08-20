/************************************************************************
	> File Name: fakecommon.c
	> Author: shadowwen-annsshadow
	> Mail: cravenboy@163.com
	> Created Time: Sat 20 Aug 2016 19:35:31 PM HKT
	> Function: simulate as http server but echo time
************************************************************************/

#include "fakehttp.h"

//define epoll fd
extern int ep_fd[WORKER_COUNT], listen_fd;
//some flags
extern int g_delay;
extern int g_shutdown_flag;
//log file
extern FILE *g_logger;
//pipe
extern int g_pipe[WORKER_COUNT][2];

void usage()
{
    printf("Usage:  fakehttp -l <local address> -p <port> -d <delay (ms)> \n");
}

/**
 * [set_noblocking set fd with O_NONBLOCK]
 * @param fd [client fd]
 */
void set_noblocking(int fd)
{
    int opts;
    opts = fcntl(fd, F_GETFL);
    if (opts < 0)
    {
        fprintf(stderr, "fcntl get fd options fail\n");
        return;
    }
    opts = opts | O_NONBLOCK;
    if (fcntl(fd, F_SETFL, opts) < 0)
    {
        fprintf(stderr, "fcntl set fd noblocking fail\n");
        return;
    }
    return;
}



/**
 * [sig_exit_handle close listen_fd and set the shutdown flag]
 * @param number [the number of signal]
 */
void sig_exit_handle(int number)
{
    close(listen_fd);
    g_shutdown_flag = 1;
    printf(">>>> [%d]will shutdown soon...[%d]\n", getpid(), number);
}

/**
 * [destroy_fd delete epoll fd, close client fd, destroy data pointer]
 * @param myep_fd   [epoll fd]
 * @param client_fd [client fd]
 * @param data_ptr  [point to data]
 */
void destroy_fd(int myep_fd, int client_fd, struct io_data_t *data_ptr)
{
    struct epoll_event ev;
    ev.data.ptr = data_ptr;
    epoll_ctl(myep_fd, EPOLL_CTL_DEL, client_fd, &ev);
    //let the client_fd can not read or write
    shutdown(client_fd, SHUT_RDWR);
    close(client_fd);
    destroy_io_data(data_ptr);
}
