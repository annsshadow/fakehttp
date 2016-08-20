/************************************************************************
	> File Name: fakehttp.c
	> Author: shadowwen-annsshadow
	> Mail: cravenboy@163.com
	> Created Time: Wed 27 Jul 2016 13:51:11 PM HKT
	> Function: simulate as http server but echo time
************************************************************************/

#include "fakehttp.h"

//define epoll fd
int ep_fd[WORKER_COUNT] = {0}, listen_fd = 0;
//some flags
int g_delay = 0;
int g_shutdown_flag = 0;
//log file
FILE *g_logger = NULL;
//pipe
int g_pipe[WORKER_COUNT][2];

int main(int argc, char **argv)
{
    const char *ip_binding = "0.0.0.0";
    int port_listening = 12321;
    char *data_file = NULL;
    int opt = 0;
    int on = 1;

    int client_fd = 0;
    int i = 0;
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
    if (argc == 1)
    {
        usage();
        return 1;
    }

    //get opt to set property
    while ((opt = getopt(argc, argv, "l:p:d:hq")) != -1)
    {
        switch (opt)
        {
        case 'l':
            //copy the ip address to ip_binding
            ip_binding = strdup(optarg);
            break;
        case 'p':
            port_listening = atoi(optarg);
            if (port_listening == 0)
            {
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
    for (i = 0; i < WORKER_COUNT; i++)
    {
        if (pipe(g_pipe[i]) < 0)
        {
            perror("failed to create pipe");
            exit(1);
        }
    }

    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (-1 == listen_fd)
    {
        perror("listen faild!");
        exit(-1);
    }

    //allow to use the address and port again
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    //not to use Nagle
    setsockopt(listen_fd, IPPROTO_TCP, TCP_NODELAY, (int[])
    {
        1
    }, sizeof(int));
    //send ACK as quickly as possible
    setsockopt(listen_fd, IPPROTO_TCP, TCP_QUICKACK, (int[])
    {
        1
    }, sizeof(int));

    //set the server infos
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons((short)port_listening);
    server_addr.sin_addr.s_addr = inet_addr(ip_binding);

    //bind--->listen
    if (-1 == bind(listen_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)))
    {
        perror("bind error");
        exit(-1);
    }
    if (-1 == listen(listen_fd, MAX_LISTEN_QUEUE_SIZE))
    {
        perror("listen error");
        exit(-1);
    }

    //create epoll for each worker
    for (i = 0; i < WORKER_COUNT; i++)
    {
        ep_fd[i] = epoll_create(MAX_EPOLL_FD);
        if (ep_fd[i] < 0)
        {
            perror("epoll_create failed.");
            exit(-1);
        }
    }

    //create thread and set their data
    for (i = 0; i < WORKER_COUNT; i++)
    {
        pthread_attr_init(tattr + i);
        // creat normally to get final status
        pthread_attr_setdetachstate(tattr + i, PTHREAD_CREATE_JOINABLE);
        tdata[i].data_from_file = data_from_file;
        tdata[i].myep_fd = ep_fd[i];
        //g_pipe[i][0] for read--->g_pipe[i][1] for write
        tdata[i].mypipe_fd = g_pipe[i][0];
        if (pthread_create(tid + i, tattr + i, io_event_loop, tdata + i ) != 0)
        {
            fprintf(stderr, "pthread_create failed\n");
            return -1;
        }

    }

    for (;;)
    {
        if ((client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_n)) > 0)
        {
            if (write(g_pipe[worker_pointer][1], (char*)&client_fd, sizeof(int)) < 0)
            {
                perror("failed to write pipe");
                exit(1);
            }
            //transfer binary to numbers-and-dots notation
            inet_ntop(AF_INET, &client_addr.sin_addr, ip_buf, sizeof(ip_buf));
            worker_pointer++;
            if (worker_pointer == WORKER_COUNT) worker_pointer = 0;
        }
        //can't accept, bad file number
        else if (errno == EBADF && g_shutdown_flag)
        {
            break;
        }
        else
        {
            //have no enough fd to use, wait for system to close and just sleep
            if (0 == g_shutdown_flag)
            {
                perror("please check ulimit -n");
                //unit:second
                sleep(1);
            }
        }
    }

    //after process
    free(data_from_file.begin);
    //close each epoll fd
    for (i = 0; i < WORKER_COUNT; i++)
    {
        close(ep_fd[i]);
    }
    //can't accept client
    if (client_fd < 0 && 0 == g_shutdown_flag)
    {
        perror("Accept failed, try 'ulimit -n' to get 'open file'");
        g_shutdown_flag = 1;
    }
    //fclose(g_logger);
    printf(">>>> [%d]waiting for worker thread....\n", getpid());

    //make the thread to be detachable to free resources
    //not interested in return code, so just NULL
    for (i = 0; i < WORKER_COUNT; i++)
        pthread_join(tid[i], NULL);

    printf(">>>> [%d]hope to see you again\n", getpid());
    return 0;
}
