extern "C" {
#include <unistd.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <signal.h>
}

#include <cstring>
#include "tcp_client.hpp"

void sigpipe_handler(int sig)
{
    LOG_INFO("SIG %d caught!", sig);
}

TcpClient::TcpClient(const std::string& server_addr, uint16_t server_port) :
    server_addr(server_addr),
    server_port(server_port)
{
    struct sockaddr_in socket_addr = {0};

    // 创建套接字
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

    LOG_INFO("connected to %s:%hu, waiting for input...\n", server_addr.c_str(), server_port);
    this->server_fd = sockfd;
}

TcpClient::~TcpClient()
{
    close(this->server_fd); 
}

void TcpClient::send_data(const char *buf, uint16_t send_size)
{
    for (uint32_t retry_times = 0; retry_times < MAX_RETRY_TIMES; ) {
        signal(SIGPIPE, sigpipe_handler);

        ssize_t len = send(this->server_fd, buf, send_size, 0);
        if (len < 0) {
            if (is_ignorable_error()) {
                retry_times++;
                continue;
            }
            throw TcpRuntimeException("Failed to send data, error cannot be ignored", __FILENAME__, __LINE__);
        }

        if (len == 0) {
            retry_times++;
            continue;
        }

        // 只有发送长度小于剩余长度的时候，才做减法并继续循环
        // 否则，视为发送完毕，返回
        // 这样的写法是为了谨慎的预防无符号数回绕
        if ((len < UINT16_MAX) && (len < (ssize_t)send_size)) {
            send_size -= (uint16_t)len;
            buf += len;
        } else {
            return;
        }
    }

    throw TcpRuntimeException("Failed to send data, reached max retries", __FILENAME__, __LINE__);
}