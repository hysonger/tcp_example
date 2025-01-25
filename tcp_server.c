// Songer 2025.1.25

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h> // socket能力支持
#include <netinet/in.h>
#include <arpa/inet.h>

#define TCP_SERVER_PORT (uint16_t)13877u
#define RECV_BUF_SIZE 1024

static int g_listenSocket = -1;
struct sockaddr_in g_listenAddr = {0};

// 创建监听新连接的socket，并绑定到地址上
static int create_socket (void)
{
    // 第一步，创建socket
    g_listenSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (g_listenSocket < 0)
    {
        fprintf(stderr, "create socket6 failed! errno: %d\n", errno);
        return -1;
    }

    // 第二步，绑定地址
    g_listenAddr.sin_family = AF_INET; // IPv4
    // 填入地址；inet_addr()将点分十进制IP地址转换为32位二进制网络字节序IP地址
    g_listenAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // inet_addr("127.0.0.1"); 
    // 填入端口；htons()将主机字节序转换为网络字节序; ntohs()将网络字节序转换为主机字节序; s, l分别代表uint16_t, uint32_t
    g_listenAddr.sin_port = htons(TCP_SERVER_PORT);

    int ret = bind(g_listenSocket, (struct sockaddr *)&g_listenAddr, sizeof(g_listenAddr));
    if (ret != 0) {
        fprintf(stderr, "bind failed! errno: %d\n", errno);
        close(g_listenSocket);
        return ret;
    }

    // 第三步，监听新连接
    ret = listen(g_listenSocket, 1);
    if (ret != 0) {
        fprintf(stderr, "listen failed! errno: %d\n", errno);
        close(g_listenSocket);
        return ret;
    }

    return 0;
}

// 最简单的版本，同时只能处理一个连接的一个请求
static void accept_single_req(void)
{
    // 会在这里阻塞，直到有新的连接进来
    struct sockaddr_in clientAddr = {0};
    socklen_t clientAddrLen = sizeof(clientAddr);
    // 如果不关注客户端地址，可以传入NULL，如果关注，后两个参数均不能为NULL
    int connFd = accept(g_listenSocket, (struct sockaddr *)&clientAddr, &clientAddrLen);
    if (connFd < 0) {
        fprintf(stderr, "accept failed! errno: %d\n", errno);
        return;
    }

    printf("new connection fd:%d %s:%d=>%s:%d\n", connFd,
        inet_ntoa(clientAddr.sin_addr), ntohs(clientAddr.sin_port),
        inet_ntoa(g_listenAddr.sin_addr), ntohs(g_listenAddr.sin_port));

    char buf[RECV_BUF_SIZE] = {0};
    int ret = recv(connFd, (void *)buf, sizeof(buf), 0); // 阻塞，直到有数据可读
    if (ret < 0) {
        fprintf(stderr, "recv failed! errno: %d\n", errno);
        close(connFd);
        return;
    }

    buf[RECV_BUF_SIZE - 1] = '\0'; // 防止缓冲区溢出
    printf("data: %s\n", buf);
    close(connFd); // 即用即抛
}

int main(int argc, char *argv[])
{
    printf("create socket...\n");
    int ret = create_socket();
    if (ret != 0) {
        return ret;
    }

    printf("waiting for connection...\n");
    for (;;) {
        accept_single_req();
    }

    return 0;
}