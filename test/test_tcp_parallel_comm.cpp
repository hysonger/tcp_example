// test_bidirectional_communication.cpp
#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <queue>
#include <mutex>
#include <condition_variable>

#include "tcp_server.hpp"
#include "tcp_client.hpp"

// 自定义服务器类，实现双向通信功能
class TestTcpServerParallel : public TcpServer {
private:
    std::atomic<bool> stop_flag{false};
    std::mutex client_mutex;
    std::vector<int32_t> client_fds;

public:
    TestTcpServerParallel(const std::string &listen_addr, uint16_t listen_port) 
        : TcpServer(listen_addr, listen_port) {}

    void deal_new_client(int32_t client_fd, const sockaddr_in& client_addr) override {
        TcpServer::deal_new_client(client_fd, client_addr);
        
        // 记录客户端连接
        {
            std::lock_guard<std::mutex> lock(client_mutex);
            client_fds.push_back(client_fd);
        }
        
        // 为每个新客户端启动发送线程
        std::thread send_thread([this, client_fd]() {
            this->client_send_thread(client_fd);
        });
        send_thread.detach();
    }

    void deal_client_msg(int32_t client_fd) override {
        try {
            constexpr uint32_t BUFFER_SIZE = UINT16_MAX + 1;
            constexpr uint32_t SIZE_OFFSET = sizeof(uint16_t);

            LOG_INFO("Receiving message from client %d", client_fd);

            char buf[BUFFER_SIZE] = {0};
            recv_data_nonblock(client_fd, buf, SIZE_OFFSET);

            uint16_t msg_size = ntohs(*(uint16_t *)buf);
            if (msg_size < SIZE_OFFSET) {
                throw TcpRuntimeException("Invalid message size: " + std::to_string(msg_size), __FILENAME__, __LINE__);
            }
            LOG_INFO("Receiving message size %d from client %d", msg_size, client_fd);
            recv_data_nonblock(client_fd, buf, msg_size);
            buf[msg_size] = '\0';
            
            LOG_INFO("SERVER received from CLIENT %d: %s", client_fd, buf);
        }
        catch (TcpRuntimeException& e) {
            RETHROW(e);
        }
    }

    void client_send_thread(int32_t client_fd) {
        uint32_t msg_counter = 0;
        auto start_time = std::chrono::steady_clock::now();
        
        while (!stop_flag.load()) {
            auto current_time = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                current_time - start_time).count();
            
            if (elapsed >= 10) {
                break;
            }
            
            // 构造消息
            std::string message = "Server message #" + std::to_string(msg_counter++) + 
                                " at " + std::to_string(elapsed) + "s";
            
            // 添加消息头（长度）
            uint16_t msg_size = htons(static_cast<uint16_t>(message.length()));
            char send_buffer[UINT16_MAX];
            memcpy(send_buffer, &msg_size, sizeof(msg_size));
            memcpy(send_buffer + sizeof(msg_size), message.c_str(), message.length());
            
            try {
                send_data_nonblock(client_fd, send_buffer, 
                                 sizeof(msg_size) + message.length());
                LOG_INFO("Server sent to client %d: %s", client_fd, message.c_str());
            } catch (const TcpRuntimeException& e) {
                LOG_ERR("Server failed to send to client %d: %s", client_fd, e.what());
                break;
            }
            
            // 控制发送频率
            std::this_thread::sleep_for(std::chrono::milliseconds(75));
        }
        
        LOG_INFO("Server send thread for client %d finished", client_fd);
    }

    void stop() {
        stop_flag.store(true);
    }
    
    std::vector<int32_t> get_client_fds() {
        std::lock_guard<std::mutex> lock(client_mutex);
        return client_fds;
    }
};

// 客户端接收线程函数
void client_receive_threadfunc(TcpClient& client, std::atomic<bool>& stop_flag) {
    int32_t client_fd = client.get_fd();
    LOG_INFO("Client receive thread started with fd %d", client_fd);
    
    while (!stop_flag.load()) {
        try {
            constexpr uint32_t BUFFER_SIZE = UINT16_MAX + 1;
            constexpr uint32_t SIZE_OFFSET = sizeof(uint16_t);

            char buf[BUFFER_SIZE] = {0};
            recv_data_nonblock(client_fd, buf, SIZE_OFFSET);

            uint16_t msg_size = ntohs(*(uint16_t *)buf);
            if (msg_size < SIZE_OFFSET) {
                LOG_ERR("Invalid message size received: %d", msg_size);
                break;
            }
            
            recv_data_nonblock(client_fd, buf, msg_size);
            buf[msg_size] = '\0';
            
            LOG_INFO("CLIENT received from SERVER: %s", buf);
        }
        catch (const TcpRuntimeException& e) {
            if (!stop_flag.load()) {
                LOG_ERR("Client receive error: %s", e.what());
            }
            break;
        }
    }
    
    LOG_INFO("Client receive thread finished");
}

// 客户端发送线程函数
void client_send_threadfunc(TcpClient& client, std::atomic<bool>& stop_flag) {
    int32_t client_fd = client.get_fd();
    uint32_t msg_counter = 0;
    auto start_time = std::chrono::steady_clock::now();
    
    LOG_INFO("Client send thread started with fd %d", client_fd);
    
    while (!stop_flag.load()) {
        auto current_time = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            current_time - start_time).count();
        
        if (elapsed >= 10) {
            break;
        }
        
        // 构造消息
        std::string message = "Client message #" + std::to_string(msg_counter++) + 
                            " at " + std::to_string(elapsed) + "s";
        
        // 添加消息头（长度）
        uint16_t msg_size = htons(static_cast<uint16_t>(message.length()));
        char send_buffer[UINT16_MAX];
        memcpy(send_buffer, &msg_size, sizeof(msg_size));
        memcpy(send_buffer + sizeof(msg_size), message.c_str(), message.length());
        
        try {
            send_data_nonblock(client_fd, send_buffer, 
                             sizeof(msg_size) + message.length());
            LOG_INFO("Client sent: %s", message.c_str());
        } catch (const TcpRuntimeException& e) {
            LOG_ERR("Client send error: %s", e.what());
            break;
        }
        
        // 控制发送频率
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    
    LOG_INFO("Client send thread finished");
}

int test_parallel_communication() {
    const std::string server_addr = "127.0.0.1";
    const uint16_t server_port = 18080;
    
    try {
        // 启动服务器线程
        std::thread server_thread([&]() {
            try {
                TestTcpServerParallel server(server_addr, server_port);
                LOG_INFO("Server started on %s:%d", server_addr.c_str(), server_port);
                
                auto start_time = std::chrono::steady_clock::now();
                while (true) {
                    server.listen_loop();
                    
                    auto current_time = std::chrono::steady_clock::now();
                    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                        current_time - start_time).count();
                    
                    if (elapsed >= 15) { // 稍微延长服务器运行时间
                        break;
                    }
                }
                
                server.stop();
            } catch (const TcpRuntimeException& e) {
                LOG_ERR("Server error: %s", e.what());
            }
        });
        
        // 等待服务器启动
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        // 启动客户端
        LOG_INFO("Starting client...");
        TcpClient client(server_addr, server_port);
        
        std::atomic<bool> client_stop_flag{false};
        
        // 启动客户端接收线程
        std::thread client_recv_thread(client_receive_threadfunc, std::ref(client), 
                                      std::ref(client_stop_flag));
        
        // 启动客户端发送线程
        std::thread client_send_thread(client_send_threadfunc, std::ref(client), 
                                      std::ref(client_stop_flag));
        
        // 运行10秒后停止
        std::this_thread::sleep_for(std::chrono::seconds(10));
        
        // 停止客户端线程
        client_stop_flag.store(true);
        
        // 等待客户端线程结束
        if (client_recv_thread.joinable()) {
            client_recv_thread.join();
        }
        if (client_send_thread.joinable()) {
            client_send_thread.join();
        }
        
        // 等待服务器线程结束
        if (server_thread.joinable()) {
            server_thread.join();
        }
        
        LOG_INFO("Test completed successfully");
        
    } catch (const TcpRuntimeException& e) {
        LOG_ERR("Test failed with exception: %s", e.what());
        return 1;
    }
    
    return 0;
}