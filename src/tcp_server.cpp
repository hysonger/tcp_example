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
#include <vector>
#include <thread>

#include "tcp_server.hpp"

void TcpServer::accept_new_client(int32_t listen_fd)
{
    int32_t new_socket = -1;

    sockaddr_in client_address;
    socklen_t client_address_size = sizeof(client_address);

    new_socket = accept(listen_fd, (sockaddr *)&client_address, &client_address_size);
    if (new_socket < 0) {
        throw TcpRuntimeException("Failed to accept new client", __FILE__, __LINE__);
    }

    struct epoll_event event = { .events = EPOLLIN | EPOLLRDHUP, .data = { .fd = new_socket } };
    int32_t rc = epoll_ctl(this->epoll_fd, EPOLL_CTL_ADD, new_socket, &event);
    if (rc < 0) {
        close(new_socket);
        throw TcpRuntimeException("Failed to add new client to epoll", __FILE__, __LINE__);
    }

    auto pair_ret = this->client_fds.insert(new_socket);
    if (!pair_ret.second) {
        close(new_socket);
        throw TcpRuntimeException("The client already exists in the map, fd is " + std::to_string(new_socket), __FILE__, __LINE__);
    }

    char peer_ip[INET_ADDRSTRLEN] = {0};
    static_cast<void>(inet_ntop(AF_INET, &client_address.sin_addr, peer_ip, sizeof(peer_ip)));
    std::cout << "===> New client connected from " << peer_ip << ":" << ntohs(client_address.sin_port) << ", fd is " << new_socket << std::endl;
    
    // 一个测试线程，测试从本端关闭socket
    /*
    auto close_thread = std::thread([this, new_socket]() {
        sleep(5);
        this->close_client(new_socket);
    });
    close_thread.detach();
    */
}

void TcpServer::close_client(int32_t client_fd)
{
    epoll_ctl(this->epoll_fd, EPOLL_CTL_DEL, client_fd, nullptr);
    close(client_fd);
    this->client_fds.erase(client_fd); 
}

// 带重试机制的接收函数
void TcpServer::recv_data(int32_t client_fd, char *buf, uint16_t recv_size)
{
    for (uint32_t retry_times = 0; retry_times < MAX_RETRY_TIMES; ) {
        ssize_t len = recv(client_fd, buf, recv_size, 0);
        if (len < 0) {
            if (is_ignorable_error()) {
                retry_times++;
                continue;
            }
            throw TcpRuntimeException("Failed to recv data, error cannot be ignored", __FILE__, __LINE__);
        }

        if (len == 0) {
            retry_times++;
            continue;
        }

        // 只有发送长度小于剩余长度的时候，才做减法并继续循环
        // 否则，视为接收完毕，返回
        // 这样的写法是为了谨慎的预防无符号数回绕
        if ((len < UINT16_MAX) && (len < (ssize_t)recv_size)) {
            recv_size -= (uint16_t)len;
            buf += len;
        } else {
            return;
        }
    }

    throw TcpRuntimeException("Failed to recv data, Reached max retries", __FILE__, __LINE__);
}

void TcpServer::deal_client_msg(int32_t client_fd)
{
    // 在读取消息之前，获取socket的tcp建链状态，防止假活
    // 如果建链socket只监听了EPOLLIN事件，或外层没有检查epoll事件是否含EPOLLRDHUP，
    // 则对端断链也会走到这里，所以需要检查。（当然本例中是又监听又检查了的）
    // 当tcp连接被关闭时，获取出的tcp状态会是非ESTABLISHED
    struct tcp_info tcpinfo;
    socklen_t tcpinfo_len = sizeof(tcpinfo);
    getsockopt(client_fd, IPPROTO_TCP, TCP_INFO, &tcpinfo, &tcpinfo_len);
    if (tcpinfo.tcpi_state != TCP_ESTABLISHED) {
        std::cout << "===> The client " + std::to_string(client_fd) + " is not in ESTABLISHED state, tcp state is "
            + std::to_string(tcpinfo.tcpi_state) << std::endl;
        this->close_client(client_fd); // 如果不关闭，epoll中该socket的事件会一直存留，导致该事件无限触发
        return;
    }

    std::cout << "===> Start to deal client " + std::to_string(client_fd) + " message" << std::endl;

    char buf[BUFFER_SIZE] = {0};
    this->recv_data(client_fd, buf, SIZE_OFFSET);

    uint16_t msg_size = ntohs(*(uint16_t *)buf);
    if (msg_size < SIZE_OFFSET) {
        throw TcpRuntimeException("The message size is invalid, msg_size=" + std::to_string(msg_size), __FILE__, __LINE__);
    }
    this->recv_data(client_fd, buf, msg_size - SIZE_OFFSET); // msg_size包含头长，要减去

    buf[msg_size] = '\0'; // msg_size的最大值为65535，缓冲区已留足长度
    std::cout << "===> [" << get_current_time() << "] The client " + std::to_string(client_fd) + " message is \n" << buf << std::endl;
}

TcpServer::TcpServer(const std::string &listen_addr, uint16_t listen_port) :
    listen_addr(listen_addr), listen_port(listen_port)
{
    // 创建epoll
    this->epoll_fd = epoll_create(MAX_EPOLL_SIZE);
    if (epoll_fd < 0) {
        throw TcpRuntimeException("epoll_create", __FILE__, __LINE__);
    }

    // 创建监听新连接进入的socket
    int32_t new_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (new_socket < 0) {
        close(this->epoll_fd);
        throw TcpRuntimeException("socket", __FILE__, __LINE__);
    }

    // 打开SO_REUSEADDR，防止之前处于TIME_WAIT状态的socket无法再次绑定
    int32_t optval = 1;
    int32_t rc = setsockopt(new_socket, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    if (rc < 0) {
        close(new_socket);
        close(this->epoll_fd);
        throw TcpRuntimeException("Failed to set reuse addr", __FILE__, __LINE__);
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
        throw TcpRuntimeException("inet_pton ret is " + std::to_string(rc), __FILE__, __LINE__);
    }

    // 绑定地址
    rc = bind(new_socket, (struct sockaddr *)&socket_addr, sizeof(socket_addr));
    if (rc < 0) {
        close(new_socket);
        close(this->epoll_fd);
        throw TcpRuntimeException("bind ret is " + std::to_string(rc) + " addr is " +
            std::to_string(socket_addr.sin_addr.s_addr) + ":" + std::to_string(listen_port), __FILE__, __LINE__);
    }

    rc = listen(new_socket, 5);
    if (rc < 0) {
        close(new_socket);
        close(this->epoll_fd);
        throw TcpRuntimeException("Failed to listen", __FILE__, __LINE__);
    }

    // 添加到epoll监听
    struct epoll_event event = { .events = EPOLLIN, .data = { .fd = new_socket } };
    rc = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, new_socket, &event);
    if (rc < 0) {
        close(new_socket);
        close(this->epoll_fd);
        throw TcpRuntimeException("epoll_ctl ret is " + std::to_string(rc), __FILE__, __LINE__);
    }

    this->listen_fd = new_socket;
}

TcpServer::~TcpServer()
{
    static_cast<void>(epoll_ctl(epoll_fd, EPOLL_CTL_DEL, listen_fd, NULL));
    close(listen_fd);

    for (auto fd : client_fds) {
        static_cast<void>(epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL));
        close(fd);
    }

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
            if ((event[i].events & EPOLLERR) || (event[i].events & EPOLLHUP) || (event[i].events & EPOLLRDHUP)) {
                this->close_client(event[i].data.fd);
                throw TcpRuntimeException("abnormal event, close socket, event: " + std::to_string(event[i].events), __FILE__, __LINE__);
            }

            if (event[i].data.fd == listen_fd) {
                // 监听fd上的事件说明有新连接进入
                this->accept_new_client(this->listen_fd);
            } else {
                // 其他fd的事件说明连接上有新报文
                this->deal_client_msg(event[i].data.fd);
            }
        } catch (TcpRuntimeException &e) {
            std::cerr << e.what() << std::endl;
            continue;
        }
    }
}