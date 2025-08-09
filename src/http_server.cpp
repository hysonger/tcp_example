// http_server.cpp
extern "C" {
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/sendfile.h>
}

#include <filesystem>

#include "http_server.hpp"

// 验证访问路径，如果访问路径合法，返回完整转义后的文件路径
// @exception 路径不合法时抛出HttpRequestException
std::filesystem::path HttpServer::validate_file(const std::string& target_path)
{
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
    : TcpServer(listen_addr, listen_port), web_root(web_root)
{
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

HttpServer::~HttpServer()
{
    stop();
}

void HttpServer::stop()
{
    stop_flag.store(true);
    queue_cv.notify_all();
    
    // 等待所有工作线程结束
    for (auto& thread : worker_threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
}

void HttpServer::deal_new_client(int32_t client_fd, const sockaddr_in& client_addr)
{
    // 暂时使用基类版本
    TcpServer::deal_new_client(client_fd, client_addr);
}

std::string HttpServer::get_mime_type(const std::string& filepath)
{
    static std::unordered_map<std::string, std::string> mime_types = {
        {".html", "text/html"},
        {".css", "text/css"},
        {".js", "application/javascript"},
        {".png", "image/png"},
        {".jpg", "image/jpeg"},
        {".gif", "image/gif"},
        {".ico", "image/x-icon"},
        {".svg", "image/svg+xml"},
        {".json", "application/json"},
        {".xml", "application/xml"},
        {".pdf", "application/pdf"},
        {".zip", "application/zip"},
        {".mp4", "video/mp4"},
        {".mkv", "video/x-matroska"},
        {".mpeg", "video/mpeg"},
        {".avi", "video/x-msvideo"},
        {".webm", "video/webm"},
        {".mp3", "audio/mpeg"},
        {".wav", "audio/wav"},
        {".txt", "text/plain"},
        {".flac", "audio/flac"},
    };

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

void HttpServer::reply_error(int32_t client_fd, const HttpRequestException& e) noexcept
{
    // 发送错误信息
    std::string err_resp = e.get_err_resp();
    uint16_t err_size = static_cast<uint16_t>(err_resp.length());
    send_data_nonblock(client_fd, err_resp.c_str(), err_size);
}

// 处理Range请求
void HttpServer::handle_range_request(HttpRequest&& request)
{
    LOG_DEBUG("Handling a range request");
    HttpRequest req = request;

    try {
        // 构造完整文件路径并验证文件
        std::string full_path = validate_file(req.filepath);
        
        // 获取文件状态
        struct stat file_stat;
        if (stat(full_path.c_str(), &file_stat) < 0 || S_ISDIR(file_stat.st_mode)) {
            LOG_ERR("cannot access file: %s", full_path.c_str());
            throw HttpRequestException("cannot access file", HTTP_ERR_NOT_FOUND);
        }

        // 解析Range请求
        std::vector<HttpRange> ranges = req.parse_ranges(file_stat.st_size);
        
        if (ranges.empty()) {
            throw HttpRequestException("invalid range format", HTTP_ERR_BAD_REQUEST);
        }

        // 目前只处理第一个range
        HttpRange range = ranges[0];
        
        // 检查range有效性
        if (!range.valid || range.start < 0 || range.end >= file_stat.st_size || range.start > range.end) {
            throw HttpRequestException("invalid range", HTTP_ERR_BAD_REQUEST);
        }

        off_t content_length = range.end - range.start + 1;
        
        std::string headers = 
            "HTTP/1.1 206 Partial Content\r\n"
            "Content-Type: " + get_mime_type(req.filepath) + "\r\n"
            "Content-Range: bytes " + std::to_string(range.start) + "-" + std::to_string(range.end) + "/" + std::to_string(file_stat.st_size) + "\r\n"
            "Content-Length: " + std::to_string(content_length) + "\r\n"
            "Cache-Control: public\r\n"
            "Connection: keep-alive\r\n"
            "\r\n";
        
        // 发送 HTTP 响应头
        uint16_t header_size = static_cast<uint16_t>(headers.length());
        send_data_nonblock(req.client_fd, headers.c_str(), header_size);
        
        // 使用 sendfile_nonblock 发送文件内容的指定范围
        sendfile_nonblock(req.client_fd, full_path, range.start, content_length);
    } catch (HttpRequestException& e) {
        reply_error(req.client_fd, e);
    } catch (TcpRuntimeException& e) {
        reply_error(req.client_fd,
            HttpRequestException("TcpRuntimeException:" + std::string(e.what()),
            HTTP_ERR_INTERNAL_SERVER_ERROR));
    }
}

// 处理整个文件的数据请求
void HttpServer::handle_full_file_request(HttpRequest&& request)
{
    LOG_DEBUG("Handling a full file request");
    HttpRequest req = request;

    try {
        // 构造完整文件路径并验证文件
        std::string full_path = validate_file(req.filepath);
        
        // 获取文件状态
        struct stat file_stat;
        if (stat(full_path.c_str(), &file_stat) < 0 || S_ISDIR(file_stat.st_mode)) {
            LOG_ERR("cannot access file: %s", full_path.c_str());
            throw HttpRequestException("cannot access file", HTTP_ERR_NOT_FOUND);
        }
        std::string headers = 
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: " + get_mime_type(req.filepath) + "\r\n"
            "Content-Length: " + std::to_string(file_stat.st_size) + "\r\n"
            "Cache-Control: public\r\n"
            "Connection: keep-alive\r\n"
            "\r\n";
        
        // 发送 HTTP 响应头
        uint16_t header_size = static_cast<uint16_t>(headers.length());
        send_data_nonblock(req.client_fd, headers.c_str(), header_size);
        
        // 使用 sendfile_nonblock 发送文件内容
        sendfile_nonblock(req.client_fd, full_path, 0, file_stat.st_size);
    } catch (HttpRequestException& e) {
        reply_error(req.client_fd, e);
    } catch (TcpRuntimeException& e) {
        reply_error(req.client_fd,
            HttpRequestException("TcpRuntimeException:" + std::string(e.what()),
            HTTP_ERR_INTERNAL_SERVER_ERROR));
    }
}

// 在此处根据请求类型，转交对应类别的处理函数
void HttpServer::handle_request(HttpRequest&& request) { 
    HttpRequest req = request;
    if (req.is_range_request) {
        handle_range_request(std::move(req));
        return;
    }

    handle_full_file_request(std::move(req));
}

// 工作线程的处理函数
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
        HttpRequest request = std::move(request_queue.front());
        request_queue.pop();
        lock.unlock();
        
        try {
            handle_request(std::move(request));

            LOG_INFO("Request %s on client %d handled successfully", request.filepath.c_str(), request.client_fd);
        } catch (const HttpRequestException& e) {
            reply_error(request.client_fd, e);
        } catch (const TcpRuntimeException& e) {
            reply_error(request.client_fd,
                HttpRequestException("while sending file\n" + std::string(e.what()), HTTP_ERR_INTERNAL_SERVER_ERROR));
        }
    }
}

void HttpServer::deal_client_msg(int32_t client_fd) {
    try {
        // 循环读取数据直到找到HTTP头部结束符 \r\n\r\n
        std::string request_data = recv_with_eof(client_fd, UINT16_MAX, "\r\n\r\n");

        LOG_DEBUG("Received request: \n%s", request_data.c_str());
        HttpRequest request(client_fd, request_data);
        
        // 将文件传输请求交给工作线程处理（包含 Range 信息）
        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            request_queue.emplace(std::move(request));
        }
        queue_cv.notify_one();
        
    } catch (const HttpRequestException& e) {
        reply_error(client_fd, e);
    } catch (const TcpRuntimeException& e) {
        reply_error(client_fd,
            HttpRequestException("while parsing request: \n" + std::string(e.what()),
            HTTP_ERR_INTERNAL_SERVER_ERROR));
    }
}