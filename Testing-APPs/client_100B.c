#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <time.h>
#include <sys/epoll.h>
#include <signal.h>
#include <errno.h>
#include "homa.h"
#define SERVER_PORT 4000
#define BUFFER_SIZE (1024 * HOMA_BPAGE_SIZE) // According to the "tens of MB" instruction
typedef struct {
    int fd;
    struct msghdr hdr;
    struct homa_recvmsg_args args;
    struct sockaddr_in server_addr;
    struct homa_rcvbuf_args buf_args;
    int sent_but_not_received;
} client_sock_context;
int timed_out = 0, sent_msgs = 0;
void set_timedout(int alarm_sig) {
    timed_out = 1;
}
void cleanup(client_sock_context **contexts, int context_in_total) {
    for (int i = 0; i < context_in_total; i++) {
        close(contexts[i]->fd);
        munmap(contexts[i]->buf_args.start, contexts[i]->buf_args.length);
        free(contexts[i]);
    }
}
int client_sock_sendmsg(client_sock_context *context, char* buffer, ssize_t len) {
    if (context->sent_but_not_received) {
        return 0;
    }
    else {
        if (homa_send_connected(context->fd, buffer, len, 0) < 0) 
            return -1;
        //sent a msg to server, response not received yet
        context->sent_but_not_received = 1;
        sent_msgs++;
    }
    return 0;
}
int client_sock_recvmsg(client_sock_context *context) {
        context->args.flags = HOMA_RECVMSG_RESPONSE;
        context->args.id    = 0;
        context->hdr.msg_controllen = sizeof(context->args); // Reset controllen
        int msg_len;
        msg_len = recvmsg(context->fd, &context->hdr, MSG_DONTWAIT);
        if (msg_len < 0) {
            if (errno == EAGAIN)
                return 0;
            else {
                printf("err in recvmsg, fd %d, errno %d", context->fd, errno);
                return -1;
            }
        }
        else {
            //received response, no longer blocking sendmsg
            context->sent_but_not_received = 0;
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
        if ((client_sockets[i] = socket(AF_INET, SOCK_DGRAM, IPPROTO_HOMA)) < 0) {
            perror("Client socket cannot be created.\n");
            exit(EXIT_FAILURE);
        }
        client_sock_context *context = malloc(sizeof(client_sock_context));
        context->fd = client_sockets[i];
        context->sent_but_not_received = 0;
        context->server_addr.sin_family      = AF_INET;
        context->server_addr.sin_addr.s_addr = inet_addr("10.10.1.1");
        context->server_addr.sin_port        = htons(SERVER_PORT);
        char* msg_rcv_buffer = (char *) mmap(NULL, 1024*HOMA_BPAGE_SIZE,
            PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, 0, 0);
        if (msg_rcv_buffer == MAP_FAILED) {
            perror("mmapping failed!");
            for (int n = 0; n < i + 1; n++) {
                close(client_sockets[n]);
                munmap(contexts[n]->buf_args.start, contexts[n]->buf_args.length);
                free(contexts[n]);
            }
        }
        memset(&context->args, 0, sizeof(context->args));
        context->args.id = 0;
        context->args.flags = HOMA_RECVMSG_RESPONSE;
        context->hdr.msg_control = &context->args;
        context->hdr.msg_controllen = sizeof(context->args);
        context->hdr.msg_name = &context->server_addr;
        context->hdr.msg_namelen = sizeof(&context->server_addr);
        context->hdr.msg_iov     = NULL;
        context->hdr.msg_iovlen  = 0;
        context->buf_args.start = msg_rcv_buffer;
        context->buf_args.length = BUFFER_SIZE;
        if (setsockopt(context->fd, IPPROTO_HOMA, SO_HOMA_RCVBUF, &context->buf_args, sizeof(struct homa_rcvbuf_args)) < 0) {
            perror("cannot set sockopts\n");
            for (int n = 0; n < i + 1; n++) {
                close(client_sockets[n]);
                munmap(contexts[n]->buf_args.start, contexts[n]->buf_args.length);
                free(contexts[n]);
            }
        }
        if (connect(context->fd, (struct sockaddr *) &context->server_addr, sizeof(context->server_addr)) < 0) {
            perror("cannot connect\n");
            for (int n = 0; n < i + 1; n++) {
                close(client_sockets[n]);
                munmap(contexts[n]->buf_args.start, contexts[n]->buf_args.length);
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
                munmap(contexts[n]->buf_args.start, contexts[n]->buf_args.length);
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
            printf("throughput %d\n", sent_msgs/5);
            sleep(5);
            cleanup(contexts, total_contexts);
            exit(EXIT_SUCCESS);
        }
        for (int m = 0; m < nfds; m++) {
            if (events[m].events & EPOLLIN) {
                if (client_sock_recvmsg(events[m].data.ptr) < 0) {
                    if(errno != EAGAIN) {
                        perror("recvmsg");
                        sleep(3);
                        cleanup(contexts, total_contexts);
                        exit(EXIT_FAILURE);
                    }
                }
            }    
            if (events[m].events & EPOLLOUT) {
                if (client_sock_sendmsg(events[m].data.ptr, msg_to_send, sizeof(msg_to_send)) < 0) {
                    perror("sendmsg");
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