/************************************************************************
	> File Name: fakehttp.h
	> Author: shadowwen-annsshadow
	> Mail: cravenboy@163.com
	> Created Time: Fri 29 Jul 2016 14:35:31 PM HKT
	> Function: simulate as http server but echo time
************************************************************************/

//import the header files
#include <sys/types.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/sendfile.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <net/if.h>
#include <fcntl.h>
#include <time.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <assert.h>
#include <signal.h>
#include <sys/epoll.h>
#include <pthread.h>
#include <errno.h>

//allow epoll to create fd,limit by machine
#define MAX_EPOLL_FD 1024
//buffer size = 1048576b=128KB
#define MAX_BUF_SIZE (1<<20)
//num of worker thread
#define WORKER_COUNT 1
//listen queue size
#define MAX_LISTEN_QUEUE_SIZE 32

//define epoll fd
int ep_fd[WORKER_COUNT]={0},listen_fd=0;
//some flags
int g_delay=0;
int g_shutdown_flag=0;
//log file
FILE *g_logger=NULL;
//pipe
int g_pipe[WORKER_COUNT][2];

//get the version of HTTP
enum version_t {
    HTTP_1_0 = 10,
    HTTP_1_1 = 11
};

//struct of IO data
struct io_data_t {
    int fd;
    struct sockaddr_in addr;
    char *in_buf;
    char *out_buf;
    int in_buf_cur;
    int out_buf_cur;
    int out_buf_total;
    int keep_alive;
    enum version_t version;
};

//slice
struct slice_t {
    char *begin;
    size_t size;
};

//thread
struct thread_data_t {
    struct slice_t data_from_file;
    int myep_fd;
    int mypipe_fd;
};

/**
 * [sig_exit_handle description]
 * @param number [description]
 */
void sig_exit_handle(int number);
static void usage();
static void set_noblocking(int fd);

/**
 * [destroy_fd description]
 * @param myep_fd   [description]
 * @param client_fd [description]
 * @param data_ptr  [description]
 */
static void destroy_fd(int myep_fd, int client_fd, struct io_data_t *data_ptr);

/**
 * [io_event_loop description]
 * @param  param [description]
 * @return       [description]
 */
static void *io_event_loop(void *param);

/**
 * [destroy_io_data description]
 * @param io_data_ptr [description]
 */
static void destroy_io_data(struct io_data_t *io_data_ptr);
static struct slice_t sth_to_send();

/**
 * [init_io description]
 * @param  client_fd   [description]
 * @param  client_addr [description]
 * @return             [description]
 */
static struct io_data_t * init_io(int client_fd, struct sockaddr_in *client_addr);
