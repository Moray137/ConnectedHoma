#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#include "homa.h"
#include <errno.h>
#include <sys/epoll.h>
#include <signal.h>
#define SERVER_PORT 4000
#define BUFFER_SIZE (1024 * HOMA_BPAGE_SIZE) // According to the "tens of MB" instruction
typedef struct {
    int fd;
    struct msghdr hdr;
    struct homa_recvmsg_args args;
    struct sockaddr_in client_addr;
    struct homa_rcvbuf_args buf_args;
} sock_context;
#define MAX_EVENTS 1024
int total_contexts = 0;
sock_context *contexts[MAX_EVENTS];
int peeloff_setup(sock_context *context) {
    char* msg_rcv_buffer = (char *) mmap(NULL, 1024*HOMA_BPAGE_SIZE,
        PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, 0, 0);
    if (msg_rcv_buffer == MAP_FAILED) {
        perror("mmapping failed!");
        return -1;
    }
    context->buf_args.start = msg_rcv_buffer;
    context->buf_args.length = BUFFER_SIZE;
    if (setsockopt(context->fd, IPPROTO_HOMA, SO_HOMA_RCVBUF, &context->buf_args, sizeof(struct homa_rcvbuf_args)) < 0) {
        perror("cannot set sockopts\n");
        return -1;
    }
    return 0;
}

int peeloff_sock_ops(sock_context *context) {
    while(1) {
        context->args.flags = HOMA_RECVMSG_REQUEST;
        context->args.id    = 0;
        context->hdr.msg_controllen = sizeof(context->args); // Reset controllen
        int msg_len;
        msg_len = recvmsg(context->fd, &context->hdr, MSG_DONTWAIT);
        if (msg_len < 0) {
            if (errno == EAGAIN)
                break;
            else {
                printf("err in recvmsg, fd %d, errno %d", context->fd, errno);
                return -1;
            }
        }
        else {
            char* peeloff_msg_buffer = context->buf_args.start;
            if (homa_reply_connected(context->fd, peeloff_msg_buffer, msg_len, context->args.id) < 0) {
                perror("failed to reply!");
                return -1;
            }
        }
    }
    return 0;
}

void cleanup_contexts(sock_context **contexts, int num_of_contexts) {
    for (int i = 0; i < num_of_contexts; i++) {
        close(contexts[i]->fd);
        munmap(contexts[i]->buf_args.start, contexts[i]->buf_args.length);
        free(contexts[i]);
    }
}
void handle_quit(int signal) {
    cleanup_contexts(contexts, total_contexts);
}

int main() {
    struct epoll_event ev, events[MAX_EVENTS];
    int sock, status, nfds, epollfd, peeled_off_sock;
    char* msg_rcv_buffer = (char *) mmap(NULL, 1024*HOMA_BPAGE_SIZE,
			PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, 0, 0);
    struct homa_rcvbuf_args buf_args ;
    struct sockaddr_in client_addr;
    struct msghdr hdr;
    struct homa_recvmsg_args args;
    memset(&args, 0, sizeof(args));
    args.flags = HOMA_RECVMSG_REQUEST;
    args.id    = 0;
    hdr.msg_name = &client_addr;
    hdr.msg_namelen = sizeof(client_addr);
    hdr.msg_iov = NULL;
    hdr.msg_iovlen = 0;
    hdr.msg_control = &args;
    hdr.msg_controllen = sizeof(args);
    buf_args.start = msg_rcv_buffer;
    buf_args.length = BUFFER_SIZE;
    if (msg_rcv_buffer == MAP_FAILED) {
        perror("mmapping failed!");
        return -1;
    }
    struct sockaddr_in server_addr;
    if ((sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_HOMA)) < 0) {
        perror("Server socket cannot be created.\n");
        return -1;
    }
    if (setsockopt(sock, IPPROTO_HOMA, SO_HOMA_RCVBUF, &buf_args, sizeof(struct homa_rcvbuf_args)) < 0) {
        perror("cannot set sockopts\n");
        return -1;
    }
    server_addr.sin_family        = AF_INET;
    server_addr.sin_addr.s_addr   = INADDR_ANY;
    server_addr.sin_port          = htons(SERVER_PORT);
    if ((status = bind(sock, (struct sockaddr *) &server_addr, sizeof(struct sockaddr_in))) < 0) {
        perror("Bind process failed.\n");
        return -1;
    }
    epollfd = epoll_create1(0);
    if (epollfd == -1) {
        perror("epoll_create1");
        close(sock);
        munmap(buf_args.start, buf_args.length);
        close(epollfd);
        exit(EXIT_FAILURE);
    }
    ev.events = EPOLLIN;
    ev.data.fd = sock;
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, sock, &ev) == -1) {
        perror("epoll_ctl: main sock");
        close(sock);
        munmap(buf_args.start, buf_args.length);
        close(epollfd);
        exit(EXIT_FAILURE);
    }
    signal(SIGINT, handle_quit);
    while (1) {
        nfds = epoll_wait(epollfd, events, MAX_EVENTS, 10000);
        if (nfds == -1) {
            perror("epoll_wait");
            close(sock);
            munmap(buf_args.start, buf_args.length);
            close(epollfd);
            exit(EXIT_FAILURE);
        }
        for (int n = 0; n < nfds; n++) {
            if (events[n].data.fd == sock) {
                int msg_len;
                args.flags = HOMA_RECVMSG_REQUEST;
                args.id    = 0;
                hdr.msg_controllen = sizeof(args); // Reset controllen
                msg_len = recvmsg(sock, &hdr, MSG_DONTWAIT);
                if(msg_len == -1) {
                    if(errno == EAGAIN)
                        continue;
                    else {
                        printf("recvmsg error in main sock, errno %d\n", errno);
                        close(sock);
                        munmap(buf_args.start, buf_args.length);
                        close(epollfd);
                        cleanup_contexts(contexts, total_contexts);
                        exit(EXIT_FAILURE);
                    }
                }
                else {
                    printf("listening socket received a msg from %d \n", ntohs(client_addr.sin_port));
                    printf("successfully received msg on listening sock. \n");
                    printf("args.id is %lu \n", args.id);
                    printf("saddr is %s \n", inet_ntoa(client_addr.sin_addr));
                    if((peeled_off_sock = homa_peeloff(sock, (struct sockaddr *)&client_addr, sizeof(client_addr))) < 0) {
                        if (errno == EISCONN)
                            printf("already connected.");
                        else {
                            printf("peeloff err, %d", errno);
                            close(sock);
                            munmap(buf_args.start, buf_args.length);
                            close(epollfd);
                            cleanup_contexts(contexts, total_contexts);
                            exit(EXIT_FAILURE);
                        }
                    }
                    else {
                        ev.events = EPOLLIN | EPOLLET;
                        ev.data.fd = peeled_off_sock;
                        sock_context *context = malloc(sizeof(sock_context));
                        context->fd = peeled_off_sock;
                        context->hdr.msg_control = &context->args;
                        context->hdr.msg_name = &context->client_addr;
                        ev.data.ptr = context;
                        contexts[total_contexts] = context;
                        total_contexts++; 
                        if (epoll_ctl(epollfd, EPOLL_CTL_ADD, peeled_off_sock,
                            &ev) == -1) {
                            perror("epoll_ctl: conn_sock");
                            close(sock);
                            munmap(buf_args.start, buf_args.length);
                            close(epollfd);
                            cleanup_contexts(contexts, total_contexts);
                            exit(EXIT_FAILURE);
                        }
                        else
                            if (peeloff_setup(context) == -1) {
                                perror("peeloff_setup");
                                close(sock);
                                munmap(buf_args.start, buf_args.length);
                                close(epollfd);
                                cleanup_contexts(contexts, total_contexts);
                                exit(EXIT_FAILURE);
                            }
                    }     
                }
            }
            else
                if (peeloff_sock_ops((sock_context *)events[n].data.ptr)  == -1) {
                    perror("peeloff_sock_ops");
                    close(sock);
                    munmap(buf_args.start, buf_args.length);
                    close(epollfd);
                    cleanup_contexts(contexts, total_contexts);
                    exit(EXIT_FAILURE);
                }
        }
    } 
}