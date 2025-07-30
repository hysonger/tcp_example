#ifndef TCP_SERVER_HPP
#define TCP_SERVER_HPP

#include <cstdint>
#include <stdexcept>
#include <unordered_set>

// === EXCEPTION ===

class TcpServerException : public std::runtime_error {
public:
    TcpServerException(const std::string& message, const std::string& file_name, const uint32_t line_number) :
        std::runtime_error("["  + file_name + ":" + std::to_string(line_number) + "] " + message +
        " [errno=" + std::to_string(errno) + "]") {}
};

class SocketInitException : public TcpServerException {
public:
    SocketInitException(const std::string& message, const std::string& file_name, const uint32_t line_number) :
        TcpServerException("Failed to init socket: " + message, file_name, line_number) {}
};

class SocketFaultException : public TcpServerException {
public:
    SocketFaultException(const std::string& message, const std::string& file_name, const uint32_t line_number) :
        TcpServerException("Socket fault: " + message, file_name, line_number) {}
};

// === EXCEPTION END ===

class TcpServer {
public:
    constexpr static uint32_t MAX_ACCEPT_SIZE = 5; // listen_fd的最大accept量
    constexpr static uint32_t MAX_EPOLL_SIZE = 10; // epoll创建时的容量
    constexpr static uint32_t MAX_EPOLL_EVENT_SIZE = 10; // epoll的最大事件容量
    constexpr static uint32_t EPOLL_TIMEOUT = 2000; // ms
    constexpr static uint32_t BUFFER_SIZE = UINT16_MAX + 1;
    constexpr static uint32_t SIZE_OFFSET = sizeof(uint16_t);
    constexpr static uint32_t MAX_RETRY_TIMES = 5;

    TcpServer(std::string &listen_address, int16_t listen_port);
    ~TcpServer();

    void listen_loop();

private:
    int32_t epoll_fd = -1;
    int32_t listen_fd = -1;
    std::unordered_set<int32_t> client_fds;

    void accept_new_client(int32_t listen_fd);
    void close_client(int32_t client_fd);
    void read_data(int32_t client_fd, char *buf, uint16_t recv_size);
    void deal_client_msg(int32_t client_fd);
};

#endif // TCP_SERVER_HPP