// test_tcp_communication.cpp
extern "C" {
#include <netinet/in.h>
}

#include <iostream>
#include <thread>
#include <chrono>
#include <string>
#include <cstring>

#include "tcp_server.hpp"
#include "tcp_client.hpp"

int test_tcp_communication() {
    const std::string server_addr = "127.0.0.1";
    const uint16_t server_port = 18080;
    
    try {
        // 启动服务器线程
        std::thread server_thread([&]() {
            try {
                TcpServer server(server_addr, server_port);
                std::cout << "Server started at " << server.get_listen_addr() 
                          << ":" << server.get_listen_port() << std::endl;
                
                // 运行服务器循环，处理客户端连接和消息
                for (int i = 0; i < 10; ++i) { // 运行一段时间以接收消息
                    server.listen_loop();
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            } catch (const TcpRuntimeException& e) {
                std::cerr << "Server error: " << e.what() << std::endl;
            }
        });
        
        // 确保服务器有时间启动
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        // 创建客户端并连接到服务器
        try {
            TcpClient client(server_addr, server_port);
            
            // 发送测试消息
            std::string test_message = "Hello, TCP Server! This is a test message.";
            
            // 构造带长度前缀的消息
            uint16_t msg_size = htons(static_cast<uint16_t>(test_message.size() + sizeof(uint16_t)));
            char* send_buffer = new char[test_message.size() + sizeof(uint16_t)];
            memcpy(send_buffer, &msg_size, sizeof(uint16_t));
            memcpy(send_buffer + sizeof(uint16_t), test_message.c_str(), test_message.size());
            
            std::cout << "Client sending message: " << test_message << std::endl;
            send_data_nonblock(client.get_fd(), send_buffer, static_cast<uint16_t>(test_message.size() + sizeof(uint16_t)));
            
            delete[] send_buffer;
            
            // 等待服务器处理消息
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        } catch (const TcpRuntimeException& e) {
            std::cerr << "Client error: " << e.what() << std::endl;
        }
        
        // 让服务器继续运行一小段时间以确保消息被处理
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        
        // 结束服务器线程
        if (server_thread.joinable()) {
            server_thread.join();
        }
        
        std::cout << "Test completed successfully!" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}