#ifndef TCP_SERVER_HPP
#define TCP_SERVER_HPP

extern "C" {
#include <netinet/in.h>
}

#include <cstdint>
#include <unordered_set>
#include <functional>

#include "tcp_public.hpp"

class TcpServer {
private:
    int32_t epoll_fd = -1;
    int32_t listen_fd = -1;

    std::string listen_addr;
    uint16_t listen_port;

    void accept_new_client(int32_t listen_fd);

protected:
    void close_client(int32_t client_fd);
    void recv_data(int32_t client_fd, char *buf, uint16_t recv_size);
    // 子类覆盖该函数，编写解析客户端消息的逻辑
    virtual void deal_client_msg(int32_t client_fd);

public:
    constexpr static uint32_t MAX_ACCEPT_SIZE = 5; // listen_fd的最大accept量
    constexpr static uint32_t MAX_EPOLL_SIZE = 10; // epoll创建时的容量
    constexpr static uint32_t MAX_EPOLL_EVENT_SIZE = 10; // epoll的最大事件容量
    constexpr static uint32_t EPOLL_TIMEOUT = 2000; // ms

    constexpr static uint32_t MAX_RETRY_TIMES = 5;

    TcpServer(const std::string &listen_addr, uint16_t listen_port);
    ~TcpServer();

    const std::string& get_listen_addr() const;
    uint16_t get_listen_port() const;

    void send_data(int32_t client_fd, const char *buf, uint16_t send_size);

    void listen_loop();
};

#endif // TCP_SERVER_HPP