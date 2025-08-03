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
#include <filesystem>

#include "http_request.hpp"

/**
 * @brief HTTP服务器类，继承自TcpServer，用于处理HTTP请求，支持工作线程池机制
 * 
 * 主要还是让AI帮我生成的，不过初始版本的代码结构过于惨烈，不得不手动重构了一把
 * 说明不要一次性让AI实现大量细节，要一小步一小步进行……
 */
class HttpServer : public TcpServer {
private:
    std::filesystem::path web_root; // 网站根目录
    
    // 工作线程相关数据结构
    std::atomic<bool> stop_flag{false};
    std::vector<std::thread> worker_threads;
    std::queue<HttpRequest> request_queue;
    std::mutex queue_mutex;
    std::condition_variable queue_cv;
    static constexpr size_t MAX_WORKER_THREADS = 4;

    // 验证访问路径，如果访问路径合法，返回完整转义后的文件路径
    // @exception 路径不合法时抛出HttpRequestException
    std::filesystem::path validate_file(const std::string& target_path);

    std::string get_mime_type(const std::string& filepath);

    void handle_full_file_request(int32_t client_fd, const std::string& file_path);
    void handle_request(HttpRequest& request);

    // 工作线程的核心处理函数
    void process_requests();
public:
    HttpServer(const std::string &listen_addr, uint16_t listen_port, const std::string& web_root = "./html");
    ~HttpServer();

    void deal_client_msg(int32_t client_fd) override;
    void deal_new_client(int32_t client_fd, const sockaddr_in& client_addr) override;
    
    // 添加停止方法
    void stop();
};

#endif // HTTP_SERVER_HPP