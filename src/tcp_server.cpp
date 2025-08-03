extern "C" {
#include <unistd.h>
#include <fcntl.h>
// 网络库四大天王
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <netinet/tcp.h> // 获取tcp建链状态依赖
#include <arpa/inet.h>
}

#include <cstdint>
#include <iostream>
#include <string>

#include "tcp_server.hpp"

void TcpServer::accept_new_client(int32_t listen_fd)
{
    int32_t new_socket = -1;

    sockaddr_in client_addr;
    socklen_t client_address_size = sizeof(client_addr);

    // 当有多个连接接入时，需要加循环处理
    // epoll中socket的模式是ET（边缘）触发时，必须在一次事件处理中**处理完全部的新输入**
    // 否则其他连接不会再继续触发事件，会导致事件丢失
    for (uint32_t retry_times = 0; retry_times < MAX_ACCEPT_SIZE; ) {
        // 在接受新连接时，可以使用accept4而非accept，当场指定新连接的socket为非阻塞模式
        new_socket = accept4(listen_fd, (sockaddr *)&client_addr, &client_address_size, SOCK_NONBLOCK);
        if (new_socket < 0) {
            // 非阻塞accept下，返回-1并不一定是出错
            break;
        }
        /*
        // 或者，使用fcntl手动修改为非阻塞模式
        int32_t rc = fcntl(new_socket, F_SETFL, O_NONBLOCK);
        if (rc < 0) {
            close(new_socket);
            throw TcpRuntimeException("Failed to set new socket to nonblock", __FILENAME__, __LINE__);
        }
        */

        struct epoll_event event = { .events = EPOLLIN | EPOLLRDHUP, .data = { .fd = new_socket } };
        int32_t rc = epoll_ctl(this->epoll_fd, EPOLL_CTL_ADD, new_socket, &event);
        if (rc < 0) {
            close(new_socket);
            throw TcpRuntimeException("Failed to add new client to epoll", __FILENAME__, __LINE__);
        }

        this->deal_new_client(new_socket, client_addr);
    }

    // EAGAIN表示的是已无新连接，其他错误码若出现均为错误
    if (errno != EAGAIN) {
        throw TcpRuntimeException("Failed to accept new client", __FILENAME__, __LINE__);
    }
}

void TcpServer::close_client(int32_t client_fd)
{
    if (client_fd <= 2) {
        throw TcpRuntimeException("Invalid client fd, fd cannot be stdio", __FILENAME__, __LINE__);
    }

    epoll_ctl(this->epoll_fd, EPOLL_CTL_DEL, client_fd, nullptr);
    close(client_fd);
}

void TcpServer::deal_client_msg(int32_t client_fd)
{
    try {
        constexpr uint32_t BUFFER_SIZE = UINT16_MAX + 1;
        constexpr uint32_t SIZE_OFFSET = sizeof(uint16_t);

        LOG_INFO("Start to deal client %d message", client_fd);

        char buf[BUFFER_SIZE] = {0};
        recv_data_nonblock(client_fd, buf, SIZE_OFFSET);

        uint16_t msg_size = ntohs(*(uint16_t *)buf);
        if (msg_size < SIZE_OFFSET) {
            throw TcpRuntimeException("The message size is invalid, msg_size=" + std::to_string(msg_size), __FILENAME__, __LINE__);
        }
        recv_data_nonblock(client_fd, buf, msg_size - SIZE_OFFSET); // msg_size包含头长，要减去

        buf[msg_size] = '\0'; // msg_size的最大值为65535，缓冲区已留足长度
        LOG_INFO("The client %d message is %s", client_fd, buf);
    }
    catch (TcpRuntimeException& e) {
        RETHROW(e);
    }
}

void TcpServer::deal_new_client(int32_t client_fd, const sockaddr_in& client_addr)
{
    char peer_ip[INET_ADDRSTRLEN] = {0};
    static_cast<void>(inet_ntop(AF_INET, &client_addr.sin_addr, peer_ip, sizeof(peer_ip)));
    LOG_INFO("New client connected from %s:%hu, fd is %d", peer_ip, ntohs(client_addr.sin_port), client_fd);
}

TcpServer::TcpServer(const std::string &listen_addr, uint16_t listen_port) :
    listen_addr(listen_addr), listen_port(listen_port)
{
    // 创建epoll
    // 在现在的Linux内核，epoll_create的size参数已经无意义
    this->epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        throw TcpRuntimeException("epoll_create", __FILENAME__, __LINE__);
    }

    // 创建处理新连接进入的监听socket
    // 为了不使单个接入连接把accept阻塞而饿死整个流程，该socket本身应声明为非阻塞
    // 这样accept也需要像非阻塞收发数据一样做特殊判断逻辑，见accept处注释
    int32_t new_socket = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (new_socket < 0) {
        close(this->epoll_fd);
        throw TcpRuntimeException("socket", __FILENAME__, __LINE__);
    }

    // 打开SO_REUSEADDR，防止同地址socket无法再次绑定
    // 该情况通常出现在socket由对端提出关闭，本端第三次挥手后进入TIME_WAIT状态
    // 此时同一地址上又产生新连接，若不做该配置，则无法再次绑定
    int32_t optval = 1;
    int32_t rc = setsockopt(new_socket, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    if (rc < 0) {
        close(new_socket);
        close(this->epoll_fd);
        throw TcpRuntimeException("Failed to set reuse addr", __FILENAME__, __LINE__);
    }

    // 文本型地址转换为二进制
    struct sockaddr_in socket_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(listen_port),
        .sin_addr = {
            .s_addr = INADDR_ANY
        },
        .sin_zero = {0}
    };
    rc = inet_pton(AF_INET, listen_addr.c_str(), &socket_addr.sin_addr);
    if (rc != 1) {
        close(new_socket);
        close(this->epoll_fd);
        throw TcpRuntimeException("inet_pton ret is " + std::to_string(rc), __FILENAME__, __LINE__);
    }

    // 绑定地址
    rc = bind(new_socket, (struct sockaddr *)&socket_addr, sizeof(socket_addr));
    if (rc < 0) {
        close(new_socket);
        close(this->epoll_fd);
        throw TcpRuntimeException("bind ret is " + std::to_string(rc) + " addr is " +
            std::to_string(socket_addr.sin_addr.s_addr) + ":" + std::to_string(listen_port), __FILENAME__, __LINE__);
    }

    rc = listen(new_socket, 5);
    if (rc < 0) {
        close(new_socket);
        close(this->epoll_fd);
        throw TcpRuntimeException("Failed to listen", __FILENAME__, __LINE__);
    }

    // 添加到epoll监听，这里使用ET边缘触发！
    struct epoll_event event = { .events = EPOLLIN | EPOLLET, .data = { .fd = new_socket } };
    rc = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, new_socket, &event);
    if (rc < 0) {
        close(new_socket);
        close(this->epoll_fd);
        throw TcpRuntimeException("epoll_ctl ret is " + std::to_string(rc), __FILENAME__, __LINE__);
    }

    this->listen_fd = new_socket;
}

TcpServer::~TcpServer()
{
    static_cast<void>(epoll_ctl(epoll_fd, EPOLL_CTL_DEL, listen_fd, NULL));
    close(listen_fd);
    close(epoll_fd);
}

const std::string &TcpServer::get_listen_addr() const
{
    return this->listen_addr;
}

uint16_t TcpServer::get_listen_port() const
{
    return this->listen_port;
}

// 服务器初始化后，调用该函数进入消息循环
void TcpServer::listen_loop()
{
    struct epoll_event event[MAX_EPOLL_EVENT_SIZE];

    int32_t event_count = epoll_wait(epoll_fd, event, MAX_EPOLL_EVENT_SIZE, EPOLL_TIMEOUT);

    for (int32_t i = 0; i < event_count; i++) {
        try {
            // 先检查是否是错误事件
            if (event[i].events & EPOLLRDHUP) {
                // EPOLLRDHUP 表示对端关闭了连接，不算做错误
                this->close_client(event[i].data.fd);
                LOG_INFO("Client %d is closed", event[i].data.fd);
                continue;
            }
            if ((event[i].events & EPOLLERR) || (event[i].events & EPOLLHUP)) {
                this->close_client(event[i].data.fd);
                throw TcpRuntimeException("abnormal event, close socket, event: " + std::to_string(event[i].events), __FILENAME__, __LINE__);
            }

            if (event[i].data.fd == this->listen_fd) {
                // 监听fd上的事件说明有新连接进入
                this->accept_new_client(this->listen_fd);
            } else {
                // 其他fd的事件说明连接上有新报文
                this->deal_client_msg(event[i].data.fd);
            }
        } catch (TcpRuntimeException &e) {
            LOG_ERR(e.what());
            continue;
        }
    }
}