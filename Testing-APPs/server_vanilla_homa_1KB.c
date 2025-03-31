#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#include "homa.h"
#include <errno.h>
#define SERVER_PORT 4000
#define BUFFER_SIZE (1024 * HOMA_BPAGE_SIZE) // According to the "tens of MB" instruction
int main() {
    int sock, status;
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
    socklen_t *addr_len = malloc(sizeof(socklen_t));
    buf_args.start = msg_rcv_buffer;
    buf_args.length = BUFFER_SIZE;
    if (msg_rcv_buffer == MAP_FAILED) {
        perror("mmapping failed!");
        return -1;
    }
    struct sockaddr_in *server_addr = malloc(sizeof(struct sockaddr_in));
    if (!server_addr) {
        perror("Failed to allocate memory for server_addr");
        return -1;
    }
    if ((sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_HOMA)) < 0) {
        perror("Server socket cannot be created.\n");
        return -1;
    }
    if (setsockopt(sock, IPPROTO_HOMA, SO_HOMA_RCVBUF, &buf_args, sizeof(struct homa_rcvbuf_args)) < 0) {
        perror("cannot set sockopts\n");
        return -1;
    }
    server_addr->sin_family        = AF_INET;
    server_addr->sin_addr.s_addr   = INADDR_ANY;
    server_addr->sin_port          = htons(SERVER_PORT);
    if ((status = bind(sock, (struct sockaddr *) server_addr, sizeof(struct sockaddr_in))) < 0) {
        perror("Bind process failed.\n");
        return -1;
    }
    while (1) {  
        int msg_len;
        args.flags = HOMA_RECVMSG_REQUEST;
        args.id    = 0;
        hdr.msg_controllen = sizeof(args); // Reset controllen
        msg_len = recvmsg(sock, &hdr, 0);
        if(msg_len < 0) {
            perror("recvmsg");
            exit(EXIT_FAILURE);
        }
        if((status = homa_reply(sock, buf_args.start, msg_len, (struct sockaddr *) hdr.msg_name, hdr.msg_namelen, args.id)) < 0) {
            perror("reply");
            exit(EXIT_FAILURE);
        }
        
    }
    sleep(10);
    return 0;
}