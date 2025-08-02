// http_server.cpp
#include "http_server.hpp"
#include <sys/stat.h>
#include <unistd.h>
#include <regex>
#include <fstream>
#include <sstream>

HttpServer::HttpServer(const std::string &listen_addr, uint16_t listen_port, const std::string& web_root) 
    : TcpServer(listen_addr, listen_port), web_root(web_root) {
    
    // 初始化 MIME 类型映射
    mime_types[".html"] = "text/html";
    mime_types[".htm"] = "text/html";
    mime_types[".css"] = "text/css";
    mime_types[".js"] = "application/javascript";
    mime_types[".jpg"] = "image/jpeg";
    mime_types[".jpeg"] = "image/jpeg";
    mime_types[".png"] = "image/png";
    mime_types[".gif"] = "image/gif";
    mime_types[".ico"] = "image/x-icon";
    mime_types[".mp4"] = "video/mp4";
    mime_types[".webm"] = "video/webm";
    mime_types[".ogg"] = "video/ogg";
    mime_types[".avi"] = "video/x-msvideo";
    mime_types[".mov"] = "video/quicktime";
    mime_types[".wmv"] = "video/x-ms-wmv";
    mime_types[".flv"] = "video/x-flv";
    mime_types[".mkv"] = "video/x-matroska";
    mime_types[".m3u8"] = "application/vnd.apple.mpegurl";
    mime_types[".ts"] = "video/mp2t";
    
    // 启动工作线程
    for (size_t i = 0; i < MAX_WORKER_THREADS; ++i) {
        worker_threads.emplace_back(&HttpServer::process_file_requests, this);
    }
}

HttpServer::~HttpServer() {
    stop();
}

void HttpServer::stop() {
    stop_flag.store(true);
    queue_cv.notify_all();
    
    // 等待所有工作线程结束
    for (auto& thread : worker_threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
}

void HttpServer::deal_new_client(int32_t client_fd, const sockaddr_in& client_addr) {
    TcpServer::deal_new_client(client_fd, client_addr);
}

std::string HttpServer::get_mime_type(const std::string& filepath) {
    // 从文件路径获取文件扩展名
    size_t dot_pos = filepath.find_last_of('.');
    if (dot_pos != std::string::npos) {
        std::string extension = filepath.substr(dot_pos);
        auto it = mime_types.find(extension);
        if (it != mime_types.end()) {
            return it->second;
        }
    }
    return "application/octet-stream"; // 默认二进制流
}

std::string HttpServer::extract_path(const char* request) {
    // 使用正则表达式提取 HTTP 请求路径
    std::string req(request);
    std::regex request_pattern(R"(GET\s+(.*?)\s+HTTP/1\.[01])");
    std::smatch match;
    
    if (std::regex_search(req, match, request_pattern)) {
        std::string path = match[1].str();
        if (path.empty() || path == "/") {
            path = "/index.html"; // 默认页面
        }
        return path;
    }
    
    return ""; // 无效请求
}

std::string HttpServer::generate_error_response(int error_code, const std::string& message) {
    std::string html = 
        "<html>"
        "<head><title>" + std::to_string(error_code) + " " + message + "</title></head>"
        "<body><h1>" + std::to_string(error_code) + " " + message + "</h1></body>"
        "</html>";
    
    std::string response = 
        "HTTP/1.1 " + std::to_string(error_code) + " " + message + "\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: " + std::to_string(html.length()) + "\r\n"
        "Connection: close\r\n"
        "\r\n" + html;
    
    return response;
}

void HttpServer::send_http_response_threaded(int32_t client_fd, const std::string& filepath) {
    // 将请求添加到队列中，由工作线程处理
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        request_queue.emplace(client_fd, filepath);
    }
    queue_cv.notify_one();
}

void HttpServer::process_file_requests() {
    while (!stop_flag.load()) {
        std::unique_lock<std::mutex> lock(queue_mutex);
        queue_cv.wait(lock, [this] { return !request_queue.empty() || stop_flag.load(); });
        
        if (stop_flag.load() && request_queue.empty()) {
            break;
        }
        
        if (request_queue.empty()) {
            continue;
        }
        
        // 获取请求
        ClientRequest request = request_queue.front();
        request_queue.pop();
        lock.unlock();
        
        try {
            // 构造完整文件路径
            std::string full_path = web_root + request.filepath;
            
            // 检查文件是否存在且可读
            if (access(full_path.c_str(), R_OK) != 0) {
                std::string error_response = generate_error_response(404, "Not Found");
                uint16_t response_size = static_cast<uint16_t>(error_response.length());
                send_data_nonblock(request.client_fd, error_response.c_str(), response_size);
                continue;
            }
            
            // 获取文件状态
            struct stat file_stat;
            if (stat(full_path.c_str(), &file_stat) < 0 || S_ISDIR(file_stat.st_mode)) {
                std::string error_response = generate_error_response(404, "Not Found");
                uint16_t response_size = static_cast<uint16_t>(error_response.length());
                send_data_nonblock(request.client_fd, error_response.c_str(), response_size);
                continue;
            }
            
            // 构造 HTTP 响应头（支持Range请求）
            std::string mime_type = get_mime_type(request.filepath);
            std::string headers = 
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: " + mime_type + "\r\n"
                "Content-Length: " + std::to_string(file_stat.st_size) + "\r\n"
                "Accept-Ranges: bytes\r\n"
                "Connection: close\r\n"
                "\r\n";
            
            // 发送 HTTP 响应头
            uint16_t header_size = static_cast<uint16_t>(headers.length());
            send_data_nonblock(request.client_fd, headers.c_str(), header_size);
            
            // 使用 sendfile_nonblock 发送文件内容
            sendfile_nonblock(request.client_fd, full_path);
            
        } catch (const TcpRuntimeException& e) {
            LOG_ERR("Error processing file request: %s", e.what());
        } catch (...) {
            LOG_ERR("Unknown error processing file request");
        }
    }
}

void HttpServer::deal_client_msg(int32_t client_fd) {
    try {
        constexpr uint32_t BUFFER_SIZE = UINT16_MAX + 1;
        char buf[BUFFER_SIZE] = {0};
        
        // 循环读取数据直到找到HTTP头部结束符 \r\n\r\n
        std::string request_data;
        bool header_complete = false;
        size_t total_received = 0;
        
        while (!header_complete && total_received < BUFFER_SIZE - 1) {
            // 逐字节读取，直到找到头部结束符
            recv_data_nonblock(client_fd, buf + total_received, 1);
            total_received++;
            
            request_data.assign(buf, total_received);
            
            // 检查是否收到完整的HTTP头部
            if (total_received >= 4 && 
                request_data.substr(total_received - 4, 4) == "\r\n\r\n") {
                header_complete = true;
                break;
            }
        }
        
        if (!header_complete) {
            std::string error_response = generate_error_response(400, "Bad Request");
            uint16_t response_size = static_cast<uint16_t>(error_response.length());
            send_data_nonblock(client_fd, error_response.c_str(), response_size);
            return;
        }
        
        buf[total_received] = '\0';
        
        // 提取请求路径
        std::string path = extract_path(buf);
        if (path.empty()) {
            std::string error_response = generate_error_response(400, "Bad Request");
            uint16_t response_size = static_cast<uint16_t>(error_response.length());
            send_data_nonblock(client_fd, error_response.c_str(), response_size);
            return;
        }
        
        LOG_INFO("HTTP request for: %s", path.c_str());
        
        // 将文件传输请求交给工作线程处理
        send_http_response_threaded(client_fd, path);
        
    } catch (TcpRuntimeException& e) {
        RETHROW(e);
    }
}