// http_server.hpp
#ifndef HTTP_SERVER_HPP
#define HTTP_SERVER_HPP

#include "tcp_server.hpp"
#include <string>
#include <unordered_map>
#include <thread>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <atomic>

// 添加客户端请求信息结构体
struct ClientRequest {
    int32_t client_fd;
    std::string filepath;
    
    ClientRequest(int32_t fd, const std::string& path) : client_fd(fd), filepath(path) {}
};

class HttpServer : public TcpServer {
private:
    std::string web_root;
    std::unordered_map<std::string, std::string> mime_types;
    
    // 线程相关成员
    std::atomic<bool> stop_flag{false};
    std::vector<std::thread> worker_threads;
    std::queue<ClientRequest> request_queue;
    std::mutex queue_mutex;
    std::condition_variable queue_cv;
    static const size_t MAX_WORKER_THREADS = 4;

    std::string get_mime_type(const std::string& filepath);
    std::string extract_path(const char* request);
    std::string generate_error_response(int error_code, const std::string& message);
    
    // 新增的线程处理函数
    void process_file_requests();
    void send_http_response_threaded(int32_t client_fd, const std::string& filepath);

public:
    HttpServer(const std::string &listen_addr, uint16_t listen_port, const std::string& web_root = "./html");
    ~HttpServer();

    void deal_client_msg(int32_t client_fd) override;
    void deal_new_client(int32_t client_fd, const sockaddr_in& client_addr) override;
    
    // 添加停止方法
    void stop();
};

#endif // HTTP_SERVER_HPP