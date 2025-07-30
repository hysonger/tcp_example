

#include <iostream>
#include <memory>
#include "tcp_server.hpp"

int main(const int argc, const char **argv)
{
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <port>" << std::endl;
        return -1;
    }

    try {
        auto listen_addr = std::string(argv[1]);
        int16_t listen_port = static_cast<int16_t>(std::stoi(argv[2]));
        if (std::to_string(listen_port).compare(argv[2]) != 0) {
            throw std::invalid_argument(std::string("Invalid port number: ") + argv[2]);
        }

        auto tcp_server = std::make_unique<TcpServer>(listen_addr, listen_port);

        std::cout << "===> TCP server listening on " << listen_addr << ":" << listen_port << "..." << std::endl;
        for (;;) {
            tcp_server->listen_loop();
        }
    } catch (std::exception &e) {
        std::cerr << e.what() << std::endl;
        return -1;
    }

    return 0;
}