#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

int main(const int argc, const char *argv[])
{
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <server_ip> <server_port>\n", argv[0]);
        return -1;
    } 

    struct sockaddr_in server_addr = {0};

    // 创建套接字
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        fprintf(stderr, "create socket failed! errno=%d\n", errno);
        return -1;
    }

    // 连接服务器
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons((uint16_t)strtoul(argv[2], NULL, 10));
    int rc = inet_pton(AF_INET, argv[1], &server_addr.sin_addr);
    if (rc != 1)
    {
        fprintf(stderr, "inet_pton failed! errno=%d\n", errno);
        return -1;
    }
    
    printf("connecting to %s:%hu...\n", argv[1], ntohs(server_addr.sin_port));
    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        fprintf(stderr, "connect failed! errno=%d\n", errno);
        return -1;
    }

    // 发送数据
    for (;;) {
        char buf[1024] = {0};

        fgets(buf, sizeof(buf), stdin); // 不使用fscanf，因为有空格就会截断了
        buf[sizeof(buf) / sizeof(buf[0]) - 1] = '\0';

        int32_t len = send(sockfd, buf, strlen(buf), 0);
        if (len < 0) {
            fprintf(stderr, "send failed! errno=%d\n", errno);
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            } else {
                return -1;
            }
        }

        if (len == 0) {
            fprintf(stderr, "send len is zero! \n");
        }
    }

    // 关闭套接字
    close(sockfd);
    return 0;
}