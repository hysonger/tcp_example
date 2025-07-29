#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdint.h>
#include <signal.h>
#include <stdarg.h>

#define SIZE_OFFSET sizeof(uint16_t)

void log_err(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    fprintf(stderr, "===> ");
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
}

void sigpipe_handler(int sig)
{
    log_err("SIG %d caught!", sig);
}

int32_t tcp_send_msg(int sockfd, char *buf, uint16_t send_len)
{
    uint16_t sent_len = 0;
    for (uint32_t retry_times = 0; retry_times < 3; retry_times++) {
        int32_t len = send(sockfd, buf + sent_len, send_len - sent_len, 0);
        if (len < 0) {
            if ((errno == EINTR) || (errno == EAGAIN) || (errno == EWOULDBLOCK)) {
                continue;
            } else {
                log_err("send failed! errno=%d", errno);
                return -1;
            }
        }

        if (len == 0) {
            log_err("send len is zero! ");
        }

        sent_len += len;
        if (sent_len >= send_len) {
            return (int32_t)sent_len;
        }
    }

    log_err("reached max retry times, give up!");
    return -1;
}

int main(const int argc, const char *argv[])
{
    if (argc != 3) {
        printf("Usage: %s <server_ip> <server_port>", argv[0]);
        return -1;
    } 

    struct sockaddr_in server_addr = {0};

    // 为SIGPIPE加入处理函数；
    // 当使用write()发送数据时，如果对方已经关闭了连接，那么write()会返回-1，并且errno会设置成EPIPE，这时需要处理这个错误，否则程序会退出并返回128+SIGPIPE
    // 用Ctrl + C退出服务端进程会在客户端触发这个问题
    // 但是如果截留该信号，也会使得下次send时的errno=EPIPE
    signal(SIGPIPE, sigpipe_handler);

    // 创建套接字
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        log_err("create socket failed! errno=%d", errno);
        return -1;
    }

    // 连接服务器
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons((uint16_t)strtoul(argv[2], NULL, 10));
    int rc = inet_pton(AF_INET, argv[1], &server_addr.sin_addr);
    if (rc != 1)
    {
        log_err("inet_pton failed! errno=%d", errno);
        return -1;
    }
    
    printf("===> connecting to %s:%hu...\n", argv[1], ntohs(server_addr.sin_port));
    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        log_err("connect failed! errno=%d", errno);
        return -1;
    }

    printf("===> connected to %s:%hu, waiting for input...\n", argv[1], ntohs(server_addr.sin_port));

    // 发送数据
    for (;;) {
        char buf[UINT16_MAX + 1] = {0}; // 报文长度的最大值为65535，最后一个字节留作\0使用

        fgets(buf + SIZE_OFFSET, sizeof(buf) - SIZE_OFFSET, stdin); // 不使用fscanf，因为有空格就会截断了
        buf[sizeof(buf) / sizeof(buf[0]) - 1] = '\0';

        uint16_t msg_len = strlen(buf + SIZE_OFFSET) + SIZE_OFFSET;
        *(uint16_t *)buf = htons(msg_len); // 转换为大端序，抓包时方便观察

        if (send(sockfd, buf, msg_len, 0) < 0) {
            log_err("send() error! ");
            // signal(SIGPIPE, sigpipe_handler); // signal只能截留一次，不能一直截留，如果要反复截留，需要放在这里重新截留
        }
    }

    // 关闭套接字
    close(sockfd);
    return 0;
}