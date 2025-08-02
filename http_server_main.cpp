// main.cpp
#include "http_server.hpp"
#include <iostream>
#include <csignal>

static HttpServer* g_server = nullptr;

void signal_handler(int signal) {
    LOG_INFO("Received signal %d, shutting down...", signal);
    if (g_server) {
        // 这里可以添加清理逻辑
    }
    exit(0);
}

int main() {
    try {
        // 注册信号处理器
        signal(SIGINT, signal_handler);
        signal(SIGTERM, signal_handler);
        
        // 创建 HTTP 服务器实例
        HttpServer server("127.0.0.1", 8080, "./html");
        g_server = &server;
        
        LOG_INFO("HTTP server started on 127.0.0.1:8080, serving files from ./html");
        LOG_INFO("Press Ctrl+C to stop the server");
        
        // 进入事件循环
        while (true) {
            server.listen_loop();
        }
        
    } catch (const TcpRuntimeException& e) {
        LOG_ERR("Server error: %s", e.what());
        return 1;
    }
    
    return 0;
}