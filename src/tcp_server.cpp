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

static inline bool is_ignorable_error()
{
    return (errno == EAGAIN) || (errno == EWOULDBLOCK) || (errno == EINTR);
}

// 获取当前计算机本地时间并转换成字符串
static std::string get_current_time()
{
    time_t now = time(nullptr);
    char buf[64] = {0};
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&now));
    return std::string(buf); 
}

void TcpServer::accept_new_client(int32_t listen_fd)
{
    int32_t new_socket = -1;

    sockaddr_in client_address;
    socklen_t client_address_size = sizeof(client_address);

    new_socket = accept(listen_fd, (sockaddr *)&client_address, &client_address_size);
    if (new_socket < 0) {
        throw SocketInitException("Failed to accept new client", __FILE__, __LINE__);
    }

    struct epoll_event event = { .events = EPOLLIN | EPOLLRDHUP, .data = { .fd = new_socket } };
    int32_t rc = epoll_ctl(this->epoll_fd, EPOLL_CTL_ADD, new_socket, &event);
    if (rc < 0) {
        close(new_socket);
        throw SocketInitException("Failed to add new client to epoll", __FILE__, __LINE__);
    }

    auto pair_ret = this->client_fds.insert(new_socket);
    if (!pair_ret.second) {
        close(new_socket);
        throw SocketInitException("The client already exists in the map, fd is " + std::to_string(new_socket), __FILE__, __LINE__);
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

void TcpServer::read_data(int32_t client_fd, char *buf, uint16_t recv_size)
{
    uint16_t recvd_size = 0;
    for (uint32_t recv_times = 0; recv_times < MAX_RETRY_TIMES; recv_times++) {
        int32_t len = static_cast<int32_t>(recv(client_fd, buf + recvd_size, recv_size - recvd_size, 0));
        
        if (len < 0) {
            if (is_ignorable_error()) {
                continue;
            }
            throw SocketFaultException("Failed to recv data, errno=" + std::to_string(errno), __FILE__, __LINE__);
        }

        recvd_size += (uint16_t)len;
        if (recvd_size >= recv_size) {
            return;
        }
    }

    throw SocketFaultException("Reached max retries, errno=" + std::to_string(errno), __FILE__, __LINE__);
}

void TcpServer::deal_client_msg(int32_t client_fd)
{
    struct tcp_info tcpinfo;
    socklen_t tcpinfo_len = sizeof(tcpinfo);

    // 获取socket的tcp建链状态；当tcp连接被关闭时，获取出的tcp状态会是非ESTABLISHED
    getsockopt(client_fd, IPPROTO_TCP, TCP_INFO, &tcpinfo, &tcpinfo_len);
    if (tcpinfo.tcpi_state != TCP_ESTABLISHED) {
        std::cout << "===> The client " + std::to_string(client_fd) + " is not in ESTABLISHED state, tcp state is "
            + std::to_string(tcpinfo.tcpi_state) << std::endl;
        this->close_client(client_fd); // 如果不关闭，epoll中的事件会一直存留，导致无限触发
        return;
    }

    std::cout << "===> Start to deal client " + std::to_string(client_fd) + " message" << std::endl;

    char buf[BUFFER_SIZE] = {0};
    this->read_data(client_fd, buf, SIZE_OFFSET);

    uint16_t msg_size = ntohs(*(uint16_t *)buf);
    if (msg_size < SIZE_OFFSET) {
        throw SocketFaultException("The message size is invalid, msg_size=" + std::to_string(msg_size), __FILE__, __LINE__);
    }
    this->read_data(client_fd, buf, msg_size - SIZE_OFFSET); // msg_size包含头长，要减去

    buf[msg_size] = '\0'; // msg_size的最大值为65535，缓冲区已留足长度
    std::cout << "===> [" << get_current_time() << "] The client " + std::to_string(client_fd) + " message is \n" << buf << std::endl;
}

TcpServer::TcpServer(std::string &listen_address, int16_t listen_port)
{
    // 创建epoll
    this->epoll_fd = epoll_create(MAX_EPOLL_SIZE);
    if (epoll_fd < 0) {
        throw SocketInitException("epoll_create", __FILE__, __LINE__);
    }

    // 创建监听新连接进入的socket
    int32_t new_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (new_socket < 0) {
        close(this->epoll_fd);
        throw SocketInitException("socket", __FILE__, __LINE__);
    }

    // 打开SO_REUSEADDR，防止之前处于TIME_WAIT状态的socket无法再次绑定
    int32_t optval = 1;
    int32_t rc = setsockopt(new_socket, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    if (rc < 0) {
        close(new_socket);
        close(this->epoll_fd);
        throw SocketInitException("Failed to set reuse addr", __FILE__, __LINE__);
    }

    // 文本型地址转换为二进制
    struct sockaddr_in socket_address = {
        .sin_family = AF_INET,
        .sin_port = htons(listen_port),
        .sin_addr = {
            .s_addr = INADDR_ANY
        },
        .sin_zero = {0}
    };
    rc = inet_pton(AF_INET, listen_address.c_str(), &socket_address.sin_addr);
    if (rc != 1) {
        close(new_socket);
        close(this->epoll_fd);
        throw SocketInitException("inet_pton ret is " + std::to_string(rc), __FILE__, __LINE__);
    }

    // 绑定地址
    rc = bind(new_socket, (struct sockaddr *)&socket_address, sizeof(socket_address));
    if (rc < 0) {
        close(new_socket);
        close(this->epoll_fd);
        throw SocketInitException("bind ret is " + std::to_string(rc) + " addr is " +
            std::to_string(socket_address.sin_addr.s_addr) + ":" + std::to_string(listen_port), __FILE__, __LINE__);
    }

    rc = listen(new_socket, 5);
    if (rc < 0) {
        close(new_socket);
        close(this->epoll_fd);
        throw SocketInitException("Failed to listen", __FILE__, __LINE__);
    }

    // 添加到epoll监听
    struct epoll_event event = { .events = EPOLLIN, .data = { .fd = new_socket } };
    rc = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, new_socket, &event);
    if (rc < 0) {
        close(new_socket);
        close(this->epoll_fd);
        throw SocketInitException("epoll_ctl ret is " + std::to_string(rc), __FILE__, __LINE__);
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

void TcpServer::listen_loop()
{
    struct epoll_event event[MAX_EPOLL_EVENT_SIZE];

    int32_t event_count = epoll_wait(epoll_fd, event, MAX_EPOLL_EVENT_SIZE, EPOLL_TIMEOUT);

    for (int32_t i = 0; i < event_count; i++) {
        try {
            if ((event[i].events & EPOLLERR) || (event[i].events & EPOLLHUP) || (event[i].events & EPOLLRDHUP)) {
                this->close_client(event[i].data.fd);
                throw SocketFaultException("abnormal event, close socket, event: " + std::to_string(event[i].events), __FILE__, __LINE__);
            }

            if (event[i].data.fd == listen_fd) {
                // 连接监听fd的事件说明有新连接
                this->accept_new_client(this->listen_fd);
            } else {
                // 其他fd的事件说明有数据可读
                this->deal_client_msg(event[i].data.fd);
            }
        } catch (TcpServerException &e) {
            std::cerr << e.what() << std::endl;
            continue;
        }
    }
}