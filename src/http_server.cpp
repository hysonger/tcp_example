// http_server.cpp
extern "C" {
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/sendfile.h>
}

#include <filesystem>

#include "http_server.hpp"

std::filesystem::path HttpServer::validate_file(const std::string& target_path) {
    // 构造目标文件的绝对路径
    std::filesystem::path abs_target_path;
    
    abs_target_path = web_root.string() + target_path;
    LOG_DEBUG("abs_target_path: %s", abs_target_path.string().c_str());
    
    // 规范化路径（解析 .. 和 .）
    std::filesystem::path normalized_path = std::filesystem::weakly_canonical(abs_target_path);
    std::filesystem::path normalized_web_root = std::filesystem::weakly_canonical(web_root);
    
    // 检查规范化后的路径是否以web_root开头
    auto web_root_str = normalized_web_root.string();
    auto target_str = normalized_path.string();
    
    // 确保web_root路径以路径分隔符结尾，防止部分匹配
    if (web_root_str.back() != std::filesystem::path::preferred_separator) {
        web_root_str += std::filesystem::path::preferred_separator;
    }
    
    // 检查目标路径是否在web_root内
    if (target_str.find(web_root_str) != 0) {
        throw HttpRequestException("path is invalid", HTTP_ERR_FORBIDDEN);
    }

    // 检查文件是否存在且可读
    if (access(target_str.c_str(), R_OK) != 0) {
        throw HttpRequestException("cannot access file", HTTP_ERR_NOT_FOUND);
    }
    
    return normalized_path;
}

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

    // 校验web根目录是否存在
    if (!std::filesystem::exists(web_root) || !std::filesystem::is_directory(web_root)) {
        throw std::runtime_error("web_root is not a valid directory");
    }
    
    // 启动工作线程
    for (size_t i = 0; i < MAX_WORKER_THREADS; ++i) {
        worker_threads.emplace_back(&HttpServer::process_requests, this);
    }

    LOG_INFO("HTTP server started on %s:%hu, serving files from %s", listen_addr.c_str(), listen_port, web_root.c_str());
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

void HttpServer::process_requests() {
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
        HttpRequest request = request_queue.front();
        request_queue.pop();
        lock.unlock();
        
        try {
            // 构造完整文件路径
            std::string full_path = validate_file(request.filepath);
            
            // 获取文件状态
            struct stat file_stat;
            if (stat(full_path.c_str(), &file_stat) < 0 || S_ISDIR(file_stat.st_mode)) {
                LOG_ERR("cannot access file: %s", full_path.c_str());
                throw HttpRequestException("cannot access file", HTTP_ERR_NOT_FOUND);
            }
            
            // 获取 MIME 类型
            std::string mime_type = get_mime_type(request.filepath);

            // 完整文件传输
            std::string headers = 
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: " + mime_type + "\r\n"
                "Content-Length: " + std::to_string(file_stat.st_size) + "\r\n"
                "Connection: close\r\n"
                "\r\n";
            
            // 发送 HTTP 响应头
            uint16_t header_size = static_cast<uint16_t>(headers.length());
            send_data_nonblock(request.client_fd, headers.c_str(), header_size);
            
            // 使用 sendfile_single 发送文件内容
            sendfile_single(request.client_fd, full_path);
        } catch (const HttpRequestException& e) {
            // 发送错误信息
            std::string err_resp = e.get_err_resp();
            uint16_t err_size = static_cast<uint16_t>(err_resp.length());
            send_data_nonblock(request.client_fd, err_resp.c_str(), err_size);
        } catch (const TcpRuntimeException& e) {
            std::string error_response = HttpRequestException("while sending file", HTTP_ERR_INTERNAL_SERVER_ERROR)
                .get_err_resp();
            uint16_t response_size = static_cast<uint16_t>(error_response.length());
            send_data_nonblock(request.client_fd, error_response.c_str(), response_size);
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
            
            // 检查是否收到完整的HTTP头部
            if (total_received >= 4 && 
                strcmp(buf + total_received - 4, "\r\n\r\n") == 0) {
                header_complete = true;
                break;
            }
        }
        
        buf[total_received] = '\0';
        if (!header_complete) {
            LOG_ERR("Header is incomplete! received %d bytes: %s", total_received, buf);
            throw HttpRequestException("Header is incomplete!", 400);
        }

        LOG_DEBUG("Received request: %s", buf);
        HttpRequest request(client_fd, std::string(buf));
        
        // 将文件传输请求交给工作线程处理（包含 Range 信息）
        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            request_queue.emplace(request);
        }
        queue_cv.notify_one();
        
    } catch (HttpRequestException& e) {
        std::string error_response = e.get_err_resp();
        uint16_t response_size = static_cast<uint16_t>(error_response.length());
        send_data_nonblock(client_fd, error_response.c_str(), response_size);
    } catch (TcpRuntimeException& e) {
        std::string error_response = HttpRequestException("while parsing request: " + std::string(e.what()),
            HTTP_ERR_INTERNAL_SERVER_ERROR).get_err_resp();
        uint16_t response_size = static_cast<uint16_t>(error_response.length());
        send_data_nonblock(client_fd, error_response.c_str(), response_size);
    }
}