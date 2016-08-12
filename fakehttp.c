/************************************************************************
	> File Name: fakehttp.c
	> Author: shadowwen-annsshadow
	> Mail: cravenboy@163.com
	> Created Time: Wed 27 Jul 2016 13:51:11 PM HKT
	> Function: simulate as http server but echo time
************************************************************************/

#include "fakehttp.h"

int main(int argc, char **argv)
{
    const char *ip_binding = "0.0.0.0";
    int port_listening = 12321;
    char *data_file=NULL;
    int opt=0;
    int on = 1;

    int client_fd=0;
    int i=0;
    register int worker_pointer = 0;

    struct sockaddr_in server_addr;
    struct slice_t data_from_file;

    //for each thread to control
    pthread_t tid[WORKER_COUNT];
    //for property of each thread
    pthread_attr_t tattr[WORKER_COUNT];
    struct thread_data_t tdata[WORKER_COUNT];

    char ip_buf[256] = { 0 };
    struct sockaddr_in client_addr;
    socklen_t client_n;

    //show usage if wrong
    if (argc == 1) {
        usage();
        return 1;
    }

    //get opt to set property
    while ((opt = getopt(argc, argv, "l:p:d:hq")) != -1) {
        switch (opt) {
        case 'l':
            //copy the ip address to ip_binding
            ip_binding = strdup(optarg);
            break;
        case 'p':
            port_listening = atoi(optarg);
            if (port_listening == 0) {
                printf(">>>> invalid port : %s\n", optarg);
                exit(1);
            }
            break;
        case 'd':
            g_delay = atoi(optarg);
            break;
        case 'h':
            usage();
            return 1;
        }

    }
    printf(">>>> IP listening to:%s\n", ip_binding);
    printf(">>>> Port: %d\n", port_listening);
    printf(">>>> Reponse delay(MS): %d\n", g_delay);

    //get the date from file and get begin pointer and size in struct
    data_from_file = sth_to_send(data_file);

    //register the signal
    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, sig_exit_handle);
    signal(SIGKILL, sig_exit_handle);
    signal(SIGQUIT, sig_exit_handle);
    signal(SIGTERM, sig_exit_handle);
    signal(SIGHUP, sig_exit_handle);

    //create pipe for every worker
    for (i=0;i<WORKER_COUNT;i++) {
        if (pipe(g_pipe[i])<0) {
            perror("failed to create pipe");
            exit(1);
        }
    }

    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (-1 == listen_fd) {
        perror("listen faild!");
        exit(-1);
    }

    //allow to use the address and port again
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    //not to use Nagle
    setsockopt(listen_fd, IPPROTO_TCP, TCP_NODELAY, (int[]) {
        1
    }, sizeof(int));
    //send ACK as quickly as possible
    setsockopt(listen_fd, IPPROTO_TCP, TCP_QUICKACK, (int[]) {
        1
    }, sizeof(int));

    //set the server infos
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons((short)port_listening);
    server_addr.sin_addr.s_addr = inet_addr(ip_binding);

    //bind--->listen
    if (-1 == bind(listen_fd, (struct sockaddr *)&server_addr, sizeof(server_addr))) {
        perror("bind error");
        exit(-1);
    }
    if (-1 == listen(listen_fd, MAX_LISTEN_QUEUE_SIZE)) {
        perror("listen error");
        exit(-1);
    }

    //create epoll for each worker
    for (i=0;i<WORKER_COUNT;i++) {
        ep_fd[i] = epoll_create(MAX_EPOLL_FD);
        if (ep_fd[i]<0) {
            perror("epoll_create failed.");
            exit(-1);
        }
    }

    //create thread and set their data
    for (i=0;i<WORKER_COUNT;i++) {
        pthread_attr_init(tattr+i);
        // creat normally to get final status
        pthread_attr_setdetachstate(tattr+i, PTHREAD_CREATE_JOINABLE);
        tdata[i].data_from_file = data_from_file;
        tdata[i].myep_fd = ep_fd[i];
        //g_pipe[i][0] for read--->g_pipe[i][1] for write
        tdata[i].mypipe_fd = g_pipe[i][0];
        if (pthread_create(tid+i, tattr+i, io_event_loop, tdata+i ) != 0) {
            fprintf(stderr, "pthread_create failed\n");
            return -1;
        }

    }

    for (;;) {
        if ((client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_n)) > 0) {
            if (write(g_pipe[worker_pointer][1],(char*)&client_fd,sizeof(int))<0) {
                perror("failed to write pipe");
                exit(1);
            }
            //transfer binary to numbers-and-dots notation
            inet_ntop(AF_INET, &client_addr.sin_addr, ip_buf, sizeof(ip_buf));
            worker_pointer++;
            if (worker_pointer == WORKER_COUNT) worker_pointer=0;
        }
        //can't accept, bad file number
        else if (errno == EBADF && g_shutdown_flag) {
            break;
        }
        else {
            //have no enough fd to use, wait for system to close and just sleep
            if (0 == g_shutdown_flag) {
                perror("please check ulimit -n");
                //unit:second
                sleep(1);
            }
        }
    }

    //after process
    free(data_from_file.begin);
    //close each epoll fd
    for (i=0; i< WORKER_COUNT; i++) {
        close(ep_fd[i]);
    }
    //can't accept client
    if (client_fd<0 && 0==g_shutdown_flag) {
        perror("Accept failed, try 'ulimit -n' to get 'open file'");
        g_shutdown_flag = 1;
    }
    //fclose(g_logger);
    printf(">>>> [%d]waiting for worker thread....\n",getpid());

    //make the thread to be detachable to free resources
    //not interested in return code, so just NULL
    for (i=0; i< WORKER_COUNT; i++)
        pthread_join(tid[i], NULL);

    printf(">>>> [%d]hope to see you again\n",getpid());
    return 0;
}

//set fd with O_NONBLOCK
static void set_noblocking(int fd)
{
    int opts;
    opts = fcntl(fd, F_GETFL);
    if (opts < 0) {
        fprintf(stderr, "fcntl failed\n");
        return;
    }
    opts = opts | O_NONBLOCK;
    if (fcntl(fd, F_SETFL, opts) < 0) {
        fprintf(stderr, "fcntl failed\n");
        return;
    }
    return;
}

static void usage()
{
    printf("Usage:  fakestub -l <local address> -p <port> -d <delay (ms)> \n");
}

static struct slice_t sth_to_send()
{
    char *bin = NULL;
    struct slice_t result;
    int n;
    time_t t;
    struct tm *gmt;
    char *gmtp=NULL;
    tzset();
    t=time(NULL);
    gmt=gmtime(&t);
    gmtp=asctime(gmt);

    n=strlen(gmtp);
    bin = (char *)malloc(sizeof(char) * n + 1);
    strcpy(bin,gmtp);
    bin[n] = '\0';

    result.size = n;
    result.begin = bin;
    return result;
}

//according client to fill up each io info
static struct io_data_t * init_io(int client_fd, struct sockaddr_in *client_addr)
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
static void destroy_io_data(struct io_data_t *io_data_ptr)
{
    if (NULL == io_data_ptr)return;
    if (io_data_ptr->in_buf)free(io_data_ptr->in_buf);
    if (io_data_ptr->out_buf)free(io_data_ptr->out_buf);
    io_data_ptr->in_buf = NULL;
    io_data_ptr->out_buf = NULL;
    free(io_data_ptr);
}

//the number of signal
void sig_exit_handle(int number)
{
    close(listen_fd);
    g_shutdown_flag=1;
    printf(">>>> [%d]will shutdown soon...[%d]\n", getpid(),number);
}

static void destroy_fd(int myep_fd, int client_fd, struct io_data_t *data_ptr)
{
    struct epoll_event ev;
    ev.data.ptr = data_ptr;
    epoll_ctl(myep_fd, EPOLL_CTL_DEL, client_fd, &ev);
    shutdown(client_fd, SHUT_RDWR);
    close(client_fd);
    destroy_io_data(data_ptr);
}

//send data to client
static void io_output(int myep_fd, struct io_data_t *client_io_ptr)
{
    int client_fd, ret;
    struct epoll_event ev;

    client_fd = client_io_ptr->fd;
    ret = send(client_fd, client_io_ptr->out_buf + client_io_ptr->out_buf_cur, client_io_ptr->out_buf_total - client_io_ptr->out_buf_cur, MSG_NOSIGNAL);
    //update the position of offset
    if (ret >= 0)
        client_io_ptr->out_buf_cur += ret;

    //EAGAIN == EWOULDBLOCK
    if (0 == ret || (ret < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
        destroy_fd(myep_fd, client_fd, client_io_ptr);
        return;
    }
    //have sent all the data
    if (client_io_ptr->out_buf_cur == client_io_ptr->out_buf_total) {
        //short connection
        if (client_io_ptr->version == HTTP_1_0 && 0 == client_io_ptr->keep_alive) {
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
static void io_input(int myep_fd, struct io_data_t *client_io_ptr, struct slice_t data_from_file, const char *rsps_msg_fmt, int delay)
{
    int npos = 0;
    int total = 0;
    int ret = 0;
    char headmsg[256];
    char *sep = NULL;
    const char *CRLF = "\r\n\r\n";
    const char *LF = "\n\n";
    const char *sep_flag=NULL;

    struct epoll_event ev;
    int client_fd = client_io_ptr->fd;
    int pkg_len = 0;

    assert(client_io_ptr->in_buf_cur >= 0);
    //set no blocking IO
    ret = recv(client_fd, client_io_ptr->in_buf + client_io_ptr->in_buf_cur, 512, MSG_DONTWAIT);
    if (0 == ret || (ret < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
        destroy_fd(myep_fd, client_fd, client_io_ptr);
        return;
    }

    //get the receive info from client
    client_io_ptr->in_buf_cur += ret;
    client_io_ptr->in_buf[client_io_ptr->in_buf_cur] = '\0';

    //find the segment positon of header info
    sep = strstr(client_io_ptr->in_buf, CRLF);
    if (NULL == sep) {
        //find the position of LF
        sep = strstr(client_io_ptr->in_buf, LF);
        if (NULL == sep)
            return;
        else
            sep_flag = LF;
    } else {
        sep_flag = CRLF;
    }

    //only support GET
    if (strstr(client_io_ptr->in_buf, "GET ") == client_io_ptr->in_buf) {
        //figure out HTTP version
        if (strstr(client_io_ptr->in_buf, "HTTP/1.0") != NULL) {
            client_io_ptr->version = HTTP_1_0;
            if (NULL == strstr(client_io_ptr->in_buf, "Connection: Keep-Alive")) {
                client_io_ptr->keep_alive = 0;
            }
        } else {
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
    if (delay > 0) {
        //unit:microsecond, also can decrease the priority
        usleep(delay);
    }
}

static void * io_event_loop(void *param)
{
    register int i=0;
    int client_fd=0, nfds=0,new_sock_fd=0;
    //events[MAX_EPOLL_FD]: handle callback events
    //ev: register events
    struct epoll_event events[MAX_EPOLL_FD],ev;

    struct io_data_t *client_io_ptr;
    struct thread_data_t my_tdata  = *(struct thread_data_t*)param;

    const char *rsps_msg_fmt = "HTTP/1.1 200 OK\r\nContent-Length: %d\r\nConnection: Keep-Alive\r\nContent-Type: text/html\r\n\r\n";

    //my_tdata.mypipe_fd: for read
    ev.data.fd = my_tdata.mypipe_fd;
    ev.events = EPOLLIN;
    epoll_ctl(my_tdata.myep_fd,EPOLL_CTL_ADD,my_tdata.mypipe_fd,&ev);

    for (;;) {
        //wait for 1s to timeout
        nfds = epoll_wait(my_tdata.myep_fd, events, MAX_EPOLL_FD, 1000);
        //printf("nfds:%d, epoll fd:%d\n",nfds,my_tdata.myep_fd);
        if (nfds<=0 && 0!=g_shutdown_flag) {
            break;
        }
        for (i = 0; i < nfds && nfds>0; i++) {
            //have new connection
            if ( (events[i].data.fd == my_tdata.mypipe_fd) && (events[i].events & EPOLLIN)) {
                //read new socket to mypipe_fd
                if (read(my_tdata.mypipe_fd,&new_sock_fd,sizeof(int))==-1) {
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
            if (events[i].events & EPOLLIN) {
                my_tdata.data_from_file = sth_to_send();
                io_input(my_tdata.myep_fd, client_io_ptr, my_tdata.data_from_file, rsps_msg_fmt, (int)(g_delay * 1000 / nfds));
                //have sth to write
            } else if (events[i].events & EPOLLOUT) {
                io_output(my_tdata.myep_fd, client_io_ptr);
                //have sth error
            } else if (events[i].events & EPOLLERR) {
                client_fd = client_io_ptr->fd;
                destroy_fd(my_tdata.myep_fd, client_fd, client_io_ptr);
            }
        }
    }
    return NULL;
}
