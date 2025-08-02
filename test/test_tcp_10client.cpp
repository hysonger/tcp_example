// test_tcp_10client.cpp
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <string>
#include <cstring>
#include <atomic>

#include "tcp_server.hpp"
#include "tcp_client.hpp"

// 自定义服务器类，实现deal_client_msg方法
class TestTcpServer : public TcpServer {
private:
    std::atomic<int> message_count;
    
public:
    TestTcpServer(const std::string &listen_addr, uint16_t listen_port) 
        : TcpServer(listen_addr, listen_port), message_count(0) {}
    
    // 重写处理客户端消息的方法
    void deal_client_msg(int32_t client_fd) override {
        constexpr static uint32_t BUFFER_SIZE = UINT16_MAX + 1;
        constexpr static uint32_t SIZE_OFFSET = sizeof(uint16_t);

        char buf[BUFFER_SIZE] = {0};
        recv_data_nonblock(client_fd, buf, SIZE_OFFSET, 5);

        uint16_t msg_size = ntohs(*(uint16_t *)buf);
        if (msg_size < SIZE_OFFSET) {
            std::cerr << "Invalid message size: " << msg_size << std::endl;
            return;
        }
        
        recv_data_nonblock(client_fd, buf, msg_size - SIZE_OFFSET, 5);
        buf[msg_size] = '\0';
        
        message_count++;
        std::cout << "Received message #" << message_count.load() 
                  << " from client fd " << client_fd << std::endl;
        
        // 回复客户端
        std::string reply = "Server received your message";
        uint16_t reply_len = htons(reply.length() + SIZE_OFFSET);
        std::vector<char> reply_buf(reply.length() + SIZE_OFFSET);
        memcpy(reply_buf.data(), &reply_len, SIZE_OFFSET);
        memcpy(reply_buf.data() + SIZE_OFFSET, reply.c_str(), reply.length());
        
        try {
            send_data_nonblock(client_fd, reply_buf.data(), reply.length() + SIZE_OFFSET, 5);
        } catch (const TcpRuntimeException& e) {
            std::cerr << "Failed to send reply to client " << client_fd << ": " << e.what() << std::endl;
        }
    }
    
    int get_message_count() const {
        return message_count.load();
    }
};

// 客户端工作线程函数
void client_worker(int client_id, const std::string& server_addr, uint16_t server_port) {
    try {
        TcpClient client(server_addr, server_port);
        
        auto start_time = std::chrono::steady_clock::now();
        int msg_sent = 0;
        
        while (true) {
            auto current_time = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                current_time - start_time).count();
            
            // 运行10秒
            if (elapsed >= 10) {
                break;
            }
            
            // 发送消息到服务器
            std::string message = "Client " + std::to_string(client_id) + 
                                " message #" + std::to_string(++msg_sent);
            uint16_t msg_len = htons(message.length() + sizeof(uint16_t));
            
            std::vector<char> send_buf(message.length() + sizeof(uint16_t));
            memcpy(send_buf.data(), &msg_len, sizeof(uint16_t));
            memcpy(send_buf.data() + sizeof(uint16_t), message.c_str(), message.length());
            
            send_data_nonblock(client.get_fd(), send_buf.data(), message.length() + sizeof(uint16_t), 5);
            
            // 等待服务器回复 (可选)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        std::cout << "Client " << client_id << " sent " << msg_sent << " messages" << std::endl;
        
    } catch (const TcpRuntimeException& e) {
        std::cerr << "Client " << client_id << " error: " << e.what() << std::endl;
    }
}

// 服务器工作线程函数
void server_worker(TestTcpServer& server, std::atomic<bool>& running) {
    while (running) {
        try {
            server.listen_loop();
        } catch (const TcpRuntimeException& e) {
            std::cerr << "Server error: " << e.what() << std::endl;
        }
        
        // 短暂休眠避免过度占用CPU
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

int test_tcp_10client() {
    const std::string server_addr = "127.0.0.1";
    const uint16_t server_port = 8080;
    const int num_clients = 10;
    
    std::cout << "Starting concurrent client test..." << std::endl;
    std::cout << "Server address: " << server_addr << ":" << server_port << std::endl;
    std::cout << "Number of clients: " << num_clients << std::endl;
    
    try {
        // 创建服务器
        TestTcpServer server(server_addr, server_port);
        std::cout << "Server started successfully" << std::endl;
        
        // 启动服务器线程
        std::atomic<bool> server_running(true);
        std::thread server_thread(server_worker, std::ref(server), std::ref(server_running));
        
        // 短暂等待服务器初始化
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // 创建并启动客户端线程
        std::vector<std::thread> client_threads;
        for (int i = 0; i < num_clients; i++) {
            client_threads.emplace_back(client_worker, i + 1, server_addr, server_port);
        }
        
        std::cout << "All clients started, running test for 10 seconds..." << std::endl;
        
        // 等待所有客户端线程完成
        for (auto& t : client_threads) {
            if (t.joinable()) {
                t.join();
            }
        }
        
        std::cout << "All clients finished sending messages" << std::endl;
        
        // 停止服务器
        server_running = false;
        if (server_thread.joinable()) {
            server_thread.join();
        }
        
        std::cout << "Total messages received by server: " << server.get_message_count() << std::endl;
        std::cout << "Test completed successfully!" << std::endl;
        
    } catch (const TcpRuntimeException& e) {
        std::cerr << "Test failed with error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}