/************************************************************************
	> File Name: fakecommon.c
	> Author: shadowwen-annsshadow
	> Mail: cravenboy@163.com
	> Created Time: Sat 20 Aug 2016 19:37:31 PM HKT
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

/**
 * [sth_to_send just send something]
 */
struct slice_t sth_to_send()
{
    char *bin = NULL;
    struct slice_t result;
    int n;
    time_t t;
    struct tm *gmt;
    char *gmtp = NULL;
    tzset();
    t = time(NULL);
    gmt = gmtime(&t);
    gmtp = asctime(gmt);

    n = strlen(gmtp);
    bin = (char *)malloc(sizeof(char) * n + 1);
    strcpy(bin, gmtp);
    bin[n] = '\0';

    result.size = n;
    result.begin = bin;
    return result;
}


/**
 * [init_io according client to fill up each io info]
 * @param  client_fd   [client fd]
 * @param  client_addr [client data address]
 * @return             [io_data_ptr]
 */
struct io_data_t * init_io(int client_fd, struct sockaddr_in *client_addr)
{
    struct io_data_t *io_data_ptr = (struct io_data_t *)malloc(sizeof(struct io_data_t));
    io_data_ptr->fd = client_fd;
    io_data_ptr->in_buf = (char *)malloc(4096);
    io_data_ptr->out_buf = (char *)malloc(MAX_BUF_SIZE);
    io_data_ptr->in_buf_cur = 0;
    io_data_ptr->out_buf_cur = 0;
    io_data_ptr->keep_alive = 1;
    if (client_addr)
        io_data_ptr->addr = *client_addr;
    return io_data_ptr;
}

//clean the file IO info of client
void destroy_io_data(struct io_data_t *io_data_ptr)
{
    if (NULL == io_data_ptr)return;
    if (io_data_ptr->in_buf)free(io_data_ptr->in_buf);
    if (io_data_ptr->out_buf)free(io_data_ptr->out_buf);
    io_data_ptr->in_buf = NULL;
    io_data_ptr->out_buf = NULL;
    free(io_data_ptr);
}

//send data to client
void io_output(int myep_fd, struct io_data_t *client_io_ptr)
{
    int client_fd, ret;
    struct epoll_event ev;

    client_fd = client_io_ptr->fd;
    ret = send(client_fd, client_io_ptr->out_buf + client_io_ptr->out_buf_cur, client_io_ptr->out_buf_total - client_io_ptr->out_buf_cur, MSG_NOSIGNAL);
    //update the position of offset
    if (ret >= 0)
        client_io_ptr->out_buf_cur += ret;

    //EAGAIN == EWOULDBLOCK
    if (0 == ret || (ret < 0 && errno != EAGAIN && errno != EWOULDBLOCK))
    {
        destroy_fd(myep_fd, client_fd, client_io_ptr);
        return;
    }
    //have sent all the data
    if (client_io_ptr->out_buf_cur == client_io_ptr->out_buf_total)
    {
        //short connection
        if (client_io_ptr->version == HTTP_1_0 && 0 == client_io_ptr->keep_alive)
        {
            destroy_fd(myep_fd, client_fd, client_io_ptr);
            return;
        }
        //keep-alive to go on
        ev.data.ptr = client_io_ptr;
        ev.events = EPOLLIN;
        epoll_ctl(myep_fd, EPOLL_CTL_MOD, client_fd, &ev);
    }
}

//get data from client
void io_input(int myep_fd, struct io_data_t *client_io_ptr, struct slice_t data_from_file, const char *rsps_msg_fmt, int delay)
{
    int npos = 0;
    int total = 0;
    int ret = 0;
    char headmsg[256];
    char *sep = NULL;
    const char *CRLF = "\r\n\r\n";
    const char *LF = "\n\n";
    const char *sep_flag = NULL;

    struct epoll_event ev;
    int client_fd = client_io_ptr->fd;
    int pkg_len = 0;

    assert(client_io_ptr->in_buf_cur >= 0);
    //set no blocking IO
    ret = recv(client_fd, client_io_ptr->in_buf + client_io_ptr->in_buf_cur, 512, MSG_DONTWAIT);
    if (0 == ret || (ret < 0 && errno != EAGAIN && errno != EWOULDBLOCK))
    {
        destroy_fd(myep_fd, client_fd, client_io_ptr);
        return;
    }

    //get the receive info from client
    client_io_ptr->in_buf_cur += ret;
    client_io_ptr->in_buf[client_io_ptr->in_buf_cur] = '\0';

    //find the segment positon of header info
    sep = strstr(client_io_ptr->in_buf, CRLF);
    if (NULL == sep)
    {
        //find the position of LF
        sep = strstr(client_io_ptr->in_buf, LF);
        if (NULL == sep)
            return;
        else
            sep_flag = LF;
    }
    else
    {
        sep_flag = CRLF;
    }

    //only support GET
    if (strstr(client_io_ptr->in_buf, "GET ") == client_io_ptr->in_buf)
    {
        //figure out HTTP version
        if (strstr(client_io_ptr->in_buf, "HTTP/1.0") != NULL)
        {
            client_io_ptr->version = HTTP_1_0;
            if (NULL == strstr(client_io_ptr->in_buf, "Connection: Keep-Alive"))
            {
                client_io_ptr->keep_alive = 0;
            }
        }
        else
        {
            client_io_ptr->version = HTTP_1_1;
        }
    }

    //get client header info
    npos = strcspn(client_io_ptr->in_buf, "\r\n");
    if (npos > 250)
        npos = 250;
    memcpy(headmsg, client_io_ptr->in_buf, npos);
    headmsg[npos] = '\0';

    //get package length
    pkg_len = sep - client_io_ptr->in_buf + strlen(sep_flag);

    assert(pkg_len >= 0);
    assert(client_io_ptr->in_buf_cur - pkg_len >= 0);
    memmove(client_io_ptr->in_buf, sep + strlen(sep_flag), client_io_ptr->in_buf_cur - pkg_len);
    client_io_ptr->in_buf_cur -= pkg_len;

    //get data to output buffer
    client_io_ptr->out_buf_cur = 0;
    total = snprintf(client_io_ptr->out_buf, MAX_BUF_SIZE, rsps_msg_fmt, data_from_file.size);
    memcpy(client_io_ptr->out_buf + total, data_from_file.begin, data_from_file.size);
    total += data_from_file.size;
    client_io_ptr->out_buf_total = total;

    //after read, monitor it to write
    ev.data.ptr = client_io_ptr;
    ev.events = EPOLLOUT;
    epoll_ctl(myep_fd, EPOLL_CTL_MOD, client_fd, &ev);
    if (delay > 0)
    {
        //unit:microsecond, also can decrease the priority
        usleep(delay);
    }
}

/**
 * [io_event_loop wait for client-epoll fd to sent or get data]
 * @param  param [point to the thread struct data]
 * @return       [just NULL]
 */
void * io_event_loop(void *param)
{
    register int i = 0;
    int client_fd = 0, nfds = 0, new_sock_fd = 0;
    //events[MAX_EPOLL_FD]: handle callback events
    //ev: register events
    struct epoll_event events[MAX_EPOLL_FD], ev;

    struct io_data_t *client_io_ptr;
    struct thread_data_t my_tdata  = *(struct thread_data_t*)param;

    const char *rsps_msg_fmt = "HTTP/1.1 200 OK\r\nContent-Length: %d\r\nConnection: Keep-Alive\r\nContent-Type: text/html\r\n\r\n";

    //my_tdata.mypipe_fd: for read
    ev.data.fd = my_tdata.mypipe_fd;
    ev.events = EPOLLIN;
    epoll_ctl(my_tdata.myep_fd, EPOLL_CTL_ADD, my_tdata.mypipe_fd, &ev);

    for (;;)
    {
        //wait for 1s to timeout
        nfds = epoll_wait(my_tdata.myep_fd, events, MAX_EPOLL_FD, 1000);
        //printf("nfds:%d, epoll fd:%d\n",nfds,my_tdata.myep_fd);
        if (nfds <= 0 && 0 != g_shutdown_flag)
        {
            break;
        }
        for (i = 0; i < nfds && nfds > 0; i++)
        {
            //have new connection
            if ( (events[i].data.fd == my_tdata.mypipe_fd) && (events[i].events & EPOLLIN))
            {
                //read new socket to mypipe_fd
                if (read(my_tdata.mypipe_fd, &new_sock_fd, sizeof(int)) == -1)
                {
                    perror("faild to read pipe");
                    exit(1);
                }
                set_noblocking(new_sock_fd);
                //set new pointer to file
                ev.data.ptr = init_io(new_sock_fd, (struct sockaddr_in *)NULL);
                ev.events = EPOLLIN;
                epoll_ctl(my_tdata.myep_fd, EPOLL_CTL_ADD, new_sock_fd, &ev);
                continue;
            }
            //have nothing to send to client
            client_io_ptr = (struct io_data_t *)events[i].data.ptr;
            if (client_io_ptr->fd <= 0) continue;

            //have sth to read
            if (events[i].events & EPOLLIN)
            {
                my_tdata.data_from_file = sth_to_send();
                io_input(my_tdata.myep_fd, client_io_ptr, my_tdata.data_from_file, rsps_msg_fmt, (int)(g_delay * 1000 / nfds));
                //have sth to write
            }
            else if (events[i].events & EPOLLOUT)
            {
                io_output(my_tdata.myep_fd, client_io_ptr);
                //have sth error
            }
            else if (events[i].events & EPOLLERR)
            {
                client_fd = client_io_ptr->fd;
                destroy_fd(my_tdata.myep_fd, client_fd, client_io_ptr);
            }
        }
    }
    return NULL;
}
