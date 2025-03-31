#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/epoll.h>
#include <signal.h>

#define SERVER_PORT 4000
#define MAX_EVENTS 1024
#define MSG_SIZE 1024

typedef struct {
    int fd;
    struct sockaddr_in client_addr;
} sock_context;

int total_contexts = 0;
sock_context *contexts[MAX_EVENTS];

void cleanup_contexts(sock_context **contexts, int num_of_contexts) {
    for (int i = 0; i < num_of_contexts; i++) {
        close(contexts[i]->fd);
        free(contexts[i]);
    }
}

int handle_client(sock_context *context) {
    char buffer[MSG_SIZE];

    while (1) {
        int bytes_received = recv(context->fd, buffer, MSG_SIZE, MSG_DONTWAIT);
        if (bytes_received <= 0) {
            if (bytes_received == 0 || errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            perror("recv error");
            return -1;
        }
        
        send(context->fd, buffer, MSG_SIZE, 0);
    }
    return 0;
}

int main() {
    struct epoll_event ev, events[MAX_EVENTS];
    int server_sock, epollfd, nfds;

    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    if ((server_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Server socket cannot be created.");
        return -1;
    }

    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(SERVER_PORT);

    if (bind(server_sock, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed.");
        return -1;
    }

    if (listen(server_sock, 128) < 0) {
        perror("Listen failed.");
        return -1;
    }

    epollfd = epoll_create1(0);
    if (epollfd == -1) {
        perror("epoll_create1 failed");
        return -1;
    }

    ev.events = EPOLLIN;
    ev.data.fd = server_sock;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, server_sock, &ev);
    while (1) {
        nfds = epoll_wait(epollfd, events, MAX_EVENTS, -1);
        if (nfds == -1) {
            perror("epoll_wait");
            break;
        }

        for (int n = 0; n < nfds; n++) {
            if (events[n].data.fd == server_sock) {
                int client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &client_len);
                if (client_sock < 0) {
                    perror("accept failed");
                    continue;
                }
                
                sock_context *context = malloc(sizeof(sock_context));
                context->fd = client_sock;
                context->client_addr = client_addr;
                
                ev.events = EPOLLIN | EPOLLET;
                ev.data.ptr = context;
                epoll_ctl(epollfd, EPOLL_CTL_ADD, client_sock, &ev);
                
                contexts[total_contexts++] = context;
                printf("New client connected from %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
            } else {
                sock_context *context = (sock_context *)events[n].data.ptr;
                if (handle_client(context) == -1) {
                    close(context->fd);
                    free(context);
                }
            }
        }
    }

    cleanup_contexts(contexts, total_contexts);
    close(server_sock);
    close(epollfd);
    return 0;
}
