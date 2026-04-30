#include "w7-common.h"
#define MAX_EVENTS 7

void usage(char* name)
{
    fprintf(stderr, "USAGE: %s port\n", name);
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) { 

    if(argc != 2) {
        usage(argv[0]);
    }
    int port = atoi(argv[1]);

    int listenfd = bind_tcp_socket(port, 1);
    int epollfd = epoll_create1(0);
    if(epollfd < 0) {
        ERR("epoll_create1");
        close(listenfd);
        exit(EXIT_FAILURE);
    }

    struct epoll_event event;
    struct epoll_event events[MAX_EVENTS];
    
    event.events = EPOLLIN;
    event.data.fd = listenfd;
    if(epoll_ctl(epollfd, EPOLL_CTL_ADD, listenfd, &event) < 0) {
        close(listenfd);
        close(epollfd);
        ERR("epoll_ctl");
    }

    while(1){
        int nofd=epoll_wait(epollfd, events, MAX_EVENTS, -1);
        if(nofd == -1){
            if(errno==EINTR) continue;
            close(listenfd);
            close(epollfd);
            ERR("epoll_wait");
        }
        for(int i=0; i<nofd; i++){
            if(events[i].data.fd==listenfd){
                int connfd = accept(listenfd, NULL, NULL);
                if(connfd < 0) {
                    if(errno == EINTR) continue;
                    close(listenfd);
                    close(epollfd);
                    ERR("accept");
                }
                bulk_write(connfd, "Welcome elector!\n", 18);

                struct epoll_event tmp_event;
                tmp_event.events = EPOLLIN;
                tmp_event.data.fd = connfd;
                if(epoll_ctl(epollfd, EPOLL_CTL_ADD, connfd, &tmp_event)<0) {
                    close(listenfd);
                    close(epollfd);
                    ERR("epoll_ctl");
                }
            }
            else{
                char buf[1024];
                ssize_t bytes_read = TEMP_FAILURE_RETRY(read(events[i].data.fd, buf, sizeof(buf)));
                if(bytes_read < 0) {
                    if(errno == EINTR) continue;
                    close(listenfd);
                    close(epollfd);
                    ERR("read");
                }
                else if(bytes_read == 0) {
                    epoll_ctl(epollfd, EPOLL_CTL_DEL, events[i].data.fd, NULL);
                    close(events[i].data.fd);
                    continue;
                }
                else{
                    buf[bytes_read] = '\0';
                    printf("Received: %s", buf);
                    fflush(stdout);
                }
            }
        }
    }

    close(epollfd);
    close(listenfd);
    return 0; 
}
