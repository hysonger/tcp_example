#ifndef TCP_CLIENT_HPP
#define TCP_CLIENT_HPP

#include <cstdint>
#include <string>

#include "tcp_public.hpp"

class TcpClient {
private:
    std::string server_addr;
    uint16_t server_port;
    int32_t conn_fd;
public:
    TcpClient(const std::string& server_addr, uint16_t port);
    ~TcpClient();

    int32_t get_fd() const;
};

#endif // TCP_CLIENT_HPP