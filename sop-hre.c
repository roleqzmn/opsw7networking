#define _GNU_SOURCE

#include "w7-common.h"
#define MAX_ELECTORS 7

void usage(char* name)
{
    fprintf(stderr, "USAGE: %s port\n", name);
    exit(EXIT_FAILURE);
}
typedef struct
{
    int port;
    int* votes;
    pthread_mutex_t* mutex;
} thread_arg_t;

void* udp_thread(void* arg)
{
    thread_arg_t* thread_arg = (thread_arg_t*)arg;
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
    {
        ERR("socket");
    }

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(thread_arg->port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    while (1)
    {
        sleep(1);
        char buf[256];
        int results[3] = {0};
        pthread_mutex_lock(thread_arg->mutex);
        for (int i = 0; i < MAX_ELECTORS; i++)
        {
            if (thread_arg->votes[i] > 0 && thread_arg->votes[i] <= 3)
            {
                switch (thread_arg->votes[i])
                {
                    case 1:
                        results[0]++;
                        break;
                    case 2:
                        results[1]++;
                        break;
                    case 3:
                        results[2]++;
                        break;
                }
            }
        }
        pthread_mutex_unlock(thread_arg->mutex);
        snprintf(buf, sizeof(buf), "Votes for candidate 1: %d\nVotes for candidate 2: %d\nVotes for candidate 3: %d\n",
                 results[0], results[1], results[2]);
        if (sendto(fd, buf, strlen(buf), 0, (struct sockaddr*)&addr, sizeof(addr)) < 0)
        {
            ERR("sendto");
        }
    }
    return NULL;
}

int main(int argc, char* argv[])
{
    if (argc != 3)
    {
        usage(argv[0]);
    }
    int port = atoi(argv[1]);
    int udp_port = atoi(argv[2]);

    int electorsfd[MAX_ELECTORS];
    for (int i = 0; i < MAX_ELECTORS; i++)
    {
        electorsfd[i] = -1;
    }
    int votes[MAX_ELECTORS];
    for (int i = 0; i < MAX_ELECTORS; i++)
    {
        votes[i] = 0;
    }
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    thread_arg_t thread_arg = {.port = udp_port, .votes = votes, .mutex = &mutex};

    pthread_t udp_thread_id;
    if (pthread_create(&udp_thread_id, NULL, udp_thread, &thread_arg) != 0)
    {
        ERR("pthread_create");
    }

    int listenfd = bind_tcp_socket(port, 1);
    int epollfd = epoll_create1(0);
    if (epollfd < 0)
    {
        ERR("epoll_create1");
        close(listenfd);
        exit(EXIT_FAILURE);
    }

    struct epoll_event event;
    struct epoll_event events[MAX_ELECTORS];

    event.events = EPOLLIN;
    event.data.fd = listenfd;
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, listenfd, &event) < 0)
    {
        close(listenfd);
        close(epollfd);
        ERR("epoll_ctl");
    }

    while (1)
    {
        int nofd = epoll_wait(epollfd, events, MAX_ELECTORS, -1);
        if (nofd == -1)
        {
            if (errno == EINTR)
                continue;
            close(listenfd);
            close(epollfd);
            ERR("epoll_wait");
        }
        for (int i = 0; i < nofd; i++)
        {
            if (events[i].data.fd == listenfd)
            {
                int connfd = accept(listenfd, NULL, NULL);
                if (connfd < 0)
                {
                    if (errno == EINTR)
                        continue;
                    close(listenfd);
                    close(epollfd);
                    ERR("accept");
                }
                bulk_write(connfd, "Welcome elector! State your state:\n", 36);

                struct epoll_event tmp_event;
                tmp_event.events = EPOLLIN;
                tmp_event.data.fd = connfd;
                if (epoll_ctl(epollfd, EPOLL_CTL_ADD, connfd, &tmp_event) < 0)
                {
                    close(listenfd);
                    close(epollfd);
                    ERR("epoll_ctl");
                }
            }
            else
            {
                char buf[1024];
                ssize_t bytes_read = TEMP_FAILURE_RETRY(read(events[i].data.fd, buf, sizeof(buf)));
                if (bytes_read < 0)
                {
                    if (errno == EINTR)
                        continue;
                    close(listenfd);
                    close(epollfd);
                    ERR("read");
                }
                else if (bytes_read == 0)
                {
                    for (int j = 0; j < MAX_ELECTORS; j++)
                    {
                        if (electorsfd[j] == events[i].data.fd)
                        {
                            electorsfd[j] = -1;
                            break;
                        }
                    }
                    epoll_ctl(epollfd, EPOLL_CTL_DEL, events[i].data.fd, NULL);
                    close(events[i].data.fd);
                    continue;
                }
                else
                {
                    int exists = 0;
                    for (int j = 0; j < MAX_ELECTORS; j++)
                    {
                        if (electorsfd[j] == events[i].data.fd)
                        {
                            electorsfd[j] = events[i].data.fd;
                            exists = 1;
                            break;
                        }
                    }

                    buf[bytes_read] = '\0';
                    if (exists == 0)
                    {
                        int id = buf[0] - '1';
                        if (id < 0 || id >= MAX_ELECTORS)
                        {
                            epoll_ctl(epollfd, EPOLL_CTL_DEL, events[i].data.fd, NULL);
                            close(events[i].data.fd);
                            continue;
                        }
                        if (electorsfd[id] != -1)
                        {
                            epoll_ctl(epollfd, EPOLL_CTL_DEL, events[i].data.fd, NULL);
                            close(events[i].data.fd);
                            continue;
                        }
                        electorsfd[id] = events[i].data.fd;
                        votes[id] = 0;
                        char welcome_msg[64];
                        snprintf(welcome_msg, sizeof(welcome_msg), "Welcome, elector of %d!\n", id + 1);
                        bulk_write(events[i].data.fd, welcome_msg, strlen(welcome_msg));
                    }
                    else
                    {
                        int vote = buf[0] - '0';
                        if (vote <= 0 || vote > 3)
                        {
                            continue;
                        }
                        for (int j = 0; j < MAX_ELECTORS; j++)
                        {
                            if (electorsfd[j] == events[i].data.fd)
                            {
                                pthread_mutex_lock(&mutex);
                                votes[j] = vote;
                                pthread_mutex_unlock(&mutex);
                                break;
                            }
                        }
                        puts("Elector voted!\n");
                        fflush(stdout);
                    }
                }
            }
        }
    }
    // wiem ze na razie nie wyjdzie ale mialem warning unused variable i nie chcialem go zostawiac
    int count_votes[3] = {0};
    for (int i = 0; i < MAX_ELECTORS; i++)
    {
        if (electorsfd[i] != -1)
        {
            if (votes[i] > 0 && votes[i] <= 3)
            {
                count_votes[votes[i] - 1]++;
            }
        }
    }
    printf("Votes for candidate 1: %d\n", count_votes[0]);
    printf("Votes for candidate 2: %d\n", count_votes[1]);
    printf("Votes for candidate 3: %d\n", count_votes[2]);
    close(epollfd);
    close(listenfd);
    return 0;
}
