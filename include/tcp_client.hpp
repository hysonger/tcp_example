#ifndef TCP_CLIENT_HPP
#define TCP_CLIENT_HPP

#include <cstdint>
#include <string>

#include "tcp_public.hpp"

class TcpClient {
private:
    std::string server_addr;
    uint16_t server_port;
    int32_t server_fd;
public:
    constexpr static uint32_t BUFFER_SIZE = UINT16_MAX + 1;
    constexpr static uint32_t MAX_RETRY_TIMES = 5;

    constexpr static uint32_t SIZE_OFFSET = sizeof(uint16_t);

    TcpClient(const std::string& server_addr, uint16_t port);
    ~TcpClient();
    void send_data(const char *buf, uint16_t send_size);
};

#endif // TCP_CLIENT_HPP