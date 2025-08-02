#ifndef TCP_SERVER_HPP
#define TCP_SERVER_HPP

extern "C" {
#include <netinet/in.h>
}

#include <cstdint>
#include <unordered_set>
#include <functional>

#include "tcp_public.hpp"

/*
    支持epoll多路并发的TCP服务器类

    构造函数中填入监听地址和端口，然后调用listen_loop()函数开始监听；
    请覆盖deal_client_msg()函数，在该函数中进行数据读取（调用recv_data函数），以及随后的解析工作。
*/
class TcpServer {
private:
    int32_t epoll_fd = -1;
    int32_t listen_fd = -1;

    std::string listen_addr;
    uint16_t listen_port;

    void accept_new_client(int32_t listen_fd);

protected:
    void close_client(int32_t client_fd);
    // 子类覆盖该函数，编写解析客户端消息的逻辑
    virtual void deal_client_msg(int32_t client_fd);
    // 子类覆盖该函数，编写有新客户端连入的额外处理逻辑
    virtual void deal_new_client(int32_t client_fd, const sockaddr_in& client_addr);

public:
    constexpr static uint32_t MAX_ACCEPT_SIZE = 5; // listen_fd的最大accept量
    constexpr static uint32_t MAX_EPOLL_SIZE = 10; // epoll创建时的容量
    constexpr static uint32_t MAX_EPOLL_EVENT_SIZE = 10; // epoll的最大事件容量
    constexpr static uint32_t EPOLL_TIMEOUT = 2000; // ms

    TcpServer(const std::string &listen_addr, uint16_t listen_port);
    ~TcpServer();

    const std::string& get_listen_addr() const;
    uint16_t get_listen_port() const;

    void listen_loop();
};

#endif // TCP_SERVER_HPP