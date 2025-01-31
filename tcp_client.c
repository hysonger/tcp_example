#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>

int main()
{
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
    server_addr.sin_port = htons((uint16_t)13877u);
    server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        fprintf(stderr, "connect failed! errno=%d\n", errno);
        return -1;
    }

    // 发送数据
    for (;;) {
        char buf[1024] = {0};

        fgets(buf, sizeof(buf), stdin); // 不使用fscanf，因为有空格就会截断了
        buf[sizeof(buf) / sizeof(buf[0]) - 1] = '\0';

        if (send(sockfd, buf, strlen(buf), 0) < 0) {
            fprintf(stderr, "send failed! errno=%d\n", errno);
            return -1;
        }
    }

    // 关闭套接字
    //close(sockfd);
    return 0;
}