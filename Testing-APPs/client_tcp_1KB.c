#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <sys/epoll.h>
#include <signal.h>
#include <errno.h>
#define SERVER_PORT 4000
typedef struct {
    int fd;
    struct sockaddr_in server_addr;
    int sent_but_not_received;
    char *msg_buffer;
} client_sock_context;
int timed_out = 0, sent_msgs = 0;
void set_timedout(int alarm_sig) {
    timed_out = 1;
}
void cleanup(client_sock_context **contexts, int context_in_total) {
    for (int i = 0; i < context_in_total; i++) {
        close(contexts[i]->fd);
        free(contexts[i]->msg_buffer);
        free(contexts[i]);
    }
}
int client_sock_send(client_sock_context *context, char* buffer, ssize_t len) {
    if (context->sent_but_not_received) 
        return 0;
    else {
        if (send(context->fd, buffer, len, 0) < 0) 
            return -1;
        //sent a msg to server, response not received yet
        context->sent_but_not_received = 1;
        sent_msgs++;
    }
    return 0;
}
int client_sock_recv(client_sock_context *context) {
    while(1) {
        int msg_len;
        msg_len = recv(context->fd, context->msg_buffer, 100, MSG_DONTWAIT);
        if (msg_len < 0) {
            if (errno == EAGAIN)
                return 0;
            else {
                printf("err in recv, fd %d, errno %d", context->fd, errno);
                return -1;
            }
        }
        else {
            //received response, no longer blocking sendmsg
            context->sent_but_not_received = 0;
        }
    }
}
int main(int argc, char** argv) {
    #define MAX_EVENTS 1024
    if (argc != 2) {
        perror("Invalid args");
        return -1;
    }
    struct epoll_event ev, events[MAX_EVENTS];
    client_sock_context *contexts[MAX_EVENTS];
    int epollfd, nfds, i, client_sock_num = atoi(argv[1]), client_sockets[client_sock_num], total_contexts = 0;
    epollfd = epoll_create1(0);
    char msg_to_send[100];
    unsigned int running_time = 5;
    signal(SIGALRM, set_timedout);
    memset(msg_to_send, 7, sizeof(msg_to_send));
    if (epollfd == -1) {
        perror("epoll_create1");
        close(epollfd);
        exit(EXIT_FAILURE);
    }
    // Create all sockets and register interest
    for (i = 0; i < client_sock_num; i++) {
        if ((client_sockets[i] = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
            perror("Client socket cannot be created.\n");
            exit(EXIT_FAILURE);
        }
        client_sock_context *context = malloc(sizeof(client_sock_context));
        context->msg_buffer = malloc(100);
        context->fd = client_sockets[i];
        context->sent_but_not_received = 0;
        context->server_addr.sin_family      = AF_INET;
        context->server_addr.sin_addr.s_addr = inet_addr("10.10.1.1");
        context->server_addr.sin_port        = htons(SERVER_PORT);
        if (connect(context->fd, (struct sockaddr *) &context->server_addr, sizeof(context->server_addr)) < 0) {
            perror("cannot connect\n");
            for (int n = 0; n < i + 1; n++) {
                close(client_sockets[n]);
                free(contexts[n]->msg_buffer);
                free(contexts[n]);
            }
        }
        contexts[total_contexts] = context;
        total_contexts++;
        ev.data.ptr = context;
        ev.events  = EPOLLIN | EPOLLOUT;
        if (epoll_ctl(epollfd, EPOLL_CTL_ADD, client_sockets[i], &ev) == -1) {
            perror("epoll_ctl: sock cannot get added to interest list");
            for (int n = 0; n < i + 1; n++) {
                close(client_sockets[n]);
                free(contexts[n]->msg_buffer);
                free(contexts[n]);
            }
            close(epollfd);
            exit(EXIT_FAILURE);
        }
    }
    alarm(running_time);
    while (!timed_out) {
        nfds = epoll_wait(epollfd, events, MAX_EVENTS, 10000);
        if (nfds == -1) {
            if(timed_out) break;
            perror("epoll_wait error");
            close(epollfd);
            cleanup(contexts, total_contexts);
            exit(EXIT_FAILURE);
        }
        for (int m = 0; m < nfds; m++) {
            if (events[m].events & EPOLLIN) {
                if (client_sock_recv(events[m].data.ptr) < 0) {
                    if(errno != EAGAIN) {
                        perror("recvmsg");
                        sleep(3);
                        close(epollfd);
                        cleanup(contexts, total_contexts);
                        exit(EXIT_FAILURE);
                    }
                }
            }    
            if (events[m].events & EPOLLOUT) {
                if (client_sock_send(events[m].data.ptr, msg_to_send, sizeof(msg_to_send)) < 0) {
                    perror("send");
                    close(epollfd);
                    cleanup(contexts, total_contexts);
                    exit(EXIT_FAILURE);
                }
            }
        }
    }
    printf("throughput %f\n", (double) sent_msgs/5);
    sleep(3);
    cleanup(contexts, total_contexts);
    return 0;
}