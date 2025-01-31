// Songer 2025.1.25

#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h> // socket能力支持
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <fcntl.h>

#define TCP_SERVER_PORT ((uint16_t)13877)
#define RECV_BUF_SIZE 1024
#define MAX_CLIENT_NUM __FD_SETSIZE / __NFDBITS

static int g_listenSocket = -1;
struct sockaddr_in g_listenAddr = {0};

static int g_clientSocket[MAX_CLIENT_NUM] = {0};

// 设置socket为非阻塞模式
// 注意，在多路复用I/O中，select/epoll需要阻塞，
// 但具体的单个socket必须是非阻塞模式，否则无法处理多路复用
static void set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0); // GETFL时，返回值为fd当前标志
    if (flags < 0) {
        fprintf(stderr, "fcntl F_GETFL failed! errno: %d\n", errno);
        return;
    }

    fcntl(fd, F_SETFL, flags | O_NONBLOCK); // SETFL时，返回值为0，不需要关注
}

// 在非阻塞I/O中，忽略这三种errno
static bool is_ignorable_errno(void)
{
    return (errno == EAGAIN) || (errno == EWOULDBLOCK) || (errno == EINTR);
}

// 创建监听新连接的socket，并绑定到地址上
static int create_socket(void)
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

    // 绑定到特定的监听地址上，目的地址非该地址的不处理
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

    set_nonblocking(g_listenSocket);

    return 0;
}

// 阻塞式的等待并处理新的客户端链接，返回新的客户端socket fd
static int accept_connection(void)
{
    struct sockaddr_in clientAddr = {0};
    socklen_t clientAddrLen = sizeof(clientAddr);

    // accept时，如果不关注客户端地址，可以传入NULL；如果关注，后两个参数均不能为NULL！
    int connFd = accept(g_listenSocket, (struct sockaddr *)&clientAddr, &clientAddrLen);
    if (connFd < 0) {
        fprintf(stderr, "accept failed! errno: %d\n", errno);
        return connFd;
    }

    // 设置为非阻塞模式
    set_nonblocking(connFd);

    printf("new connection fd:%d %s:%d=>%s:%d\n", connFd,
        inet_ntoa(clientAddr.sin_addr), ntohs(clientAddr.sin_port),
        inet_ntoa(g_listenAddr.sin_addr), ntohs(g_listenAddr.sin_port));
    return connFd;
}

// 最简单的版本，同时只能处理一个连接的一个请求
/*
static void accept_single_req(void)
{
    int newConnFd = accept_connection();

    char buf[RECV_BUF_SIZE] = {0};
    int ret = recv(newConnFd, (void *)buf, sizeof(buf), 0); // 阻塞，直到有数据可读
    if (ret < 0) {
        fprintf(stderr, "recv failed! errno: %d\n", errno);
        close(newConnFd);
        return;
    }

    buf[RECV_BUF_SIZE - 1] = '\0'; // 防止缓冲区溢出
    printf("recv data: %s\n", buf);
}
*/

// 使用了select，可以同时处理多个连接的多个请求
static void accept_select(void)
{
    fd_set readFds;
    FD_ZERO(&readFds);
    
    // 加入关注的所有socket到集合
    FD_SET(g_listenSocket, &readFds);
    for (int *p = g_clientSocket;
        p < g_clientSocket + sizeof(g_clientSocket) / sizeof(g_clientSocket[0]);
        ++p) {
        if (*p != -1) {
            FD_SET(*p, &readFds);
        }
    }

    // 阻塞，直到有socket可读；
    // 第一个参数传入关注的fd数量，后三个参数分别是可读、可写、异常的fd集合，返回值是可读的fd数量
    // 不关心write和except socket，也不设置超时，故全置NULL
    int actFdNum = select(sizeof(g_clientSocket) / sizeof(g_clientSocket[0]) + 1,
        &readFds, NULL, NULL, NULL);
    if (actFdNum <= 0) {
        // errno == EINTR 时，意味着用户按下了Ctrl + C，程序被中断，其实可忽略
        fprintf(stderr, "select failed! errno: %d\n", errno);
        return;
    }

    // 如果是监听新连接的socket
    printf("select actFdNum: %d\n", actFdNum);
    if (FD_ISSET(g_listenSocket, &readFds)) {
        int newConnFd = accept_connection();
        for (int *p = g_clientSocket;
            p < g_clientSocket + sizeof(g_clientSocket) / sizeof(g_clientSocket[0]);
            ++p) {
            // 找到第一个空闲的socket，将新连接的socket fd放入
            if (*p == -1) {
                *p = newConnFd;
                break;
            }
        }
    }

    // 否则，处理客户端socket
    for (int *p = g_clientSocket;
        p < g_clientSocket + sizeof(g_clientSocket) / sizeof(g_clientSocket[0]);
        ++p) {
        if ((*p != -1) && FD_ISSET(*p, &readFds)) {
            char buf[RECV_BUF_SIZE] = {0};

            int recvSize = recv(*p, (void *)buf, sizeof(buf), MSG_DONTWAIT); // 显式指定非阻塞
            if ((recvSize < 0) && !is_ignorable_errno()) { // 接收故障
                fprintf(stderr, "client %d recv failed! errno: %d\n", *p, errno);
                close(*p);
                *p = -1;
                continue;
            } else if (recvSize == 0) { // 对端已关闭
                fprintf(stderr, "client %d close connection, clean it\n", *p);
                close(*p);
                *p = -1;
                continue;
            }

            buf[recvSize] = '\0'; // 防止缓冲区溢出
            printf("client %d msg: %s\n", *p, buf);
        }
    }

}

int main()
{
    for (unsigned int i = 0; i < sizeof(g_clientSocket) / sizeof(g_clientSocket[0]); ++i) {
        g_clientSocket[i] = -1;
    }

    printf("create socket...\n");
    int ret = create_socket();
    if (ret != 0) {
        return ret;
    }

    printf("waiting for connection...\n");
    for (;;) {
        //accept_single_req();
        accept_select();
    }

    return 0;
}