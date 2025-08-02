extern "C" {
#include <unistd.h>
#include <fcntl.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <signal.h>
}

#include <cstring>
#include "tcp_client.hpp"

TcpClient::TcpClient(const std::string& server_addr, uint16_t server_port) :
    server_addr(server_addr),
    server_port(server_port)
{
    struct sockaddr_in socket_addr = {0};

    // 创建套接字；此时在参数2中可指定非阻塞
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        throw TcpRuntimeException("create socket failed!", __FILENAME__, __LINE__);
    }

    // 连接服务器
    socket_addr.sin_family = AF_INET;
    socket_addr.sin_port = htons(server_port);
    int rc = inet_pton(AF_INET, server_addr.c_str(), &socket_addr.sin_addr);
    if (rc != 1)
    {
        throw TcpRuntimeException("inet_pton failed! rc=" + std::to_string(rc), __FILENAME__, __LINE__);
    }
    
    LOG_INFO("connecting to %s:%hu...\n", server_addr.c_str(), server_port);
    rc = connect(sockfd, (struct sockaddr *)&socket_addr, sizeof(socket_addr));
    if (rc < 0) {
        throw TcpRuntimeException("connect failed! rc=" + std::to_string(rc), __FILENAME__, __LINE__);
    }

    // 设置套接字为非阻塞
    // 注意，对于客户端代码来说，一旦在connect之前，提前配置socket非阻塞
    // 会导致connect高概率会不等连接完毕直接返错，errno=115
    // 当然，其实也可之后再使用select/poll等对该socket进行状态变化侦测
    // 触发事件时getsockopt查询SO_ERROR，以此确认建链是否最终成功，这样可不阻塞本线程
    rc = fcntl(sockfd, F_SETFL, O_NONBLOCK);
    if (rc < 0) {
        throw TcpRuntimeException("fcntl failed! rc=" + std::to_string(rc), __FILENAME__, __LINE__);
    }

    LOG_INFO("connected to %s:%hu\n", server_addr.c_str(), server_port);
    this->conn_fd = sockfd;
}

TcpClient::~TcpClient()
{
    close(this->conn_fd); 
}

int32_t TcpClient::get_fd() const
{
    return this->conn_fd;
}