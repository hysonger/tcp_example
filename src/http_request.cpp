#include <string>
#include <unordered_map>
#include <regex>

#include "tcp_public.hpp"
#include "http_request.hpp"

std::string HttpRequestException::get_err_text() const
{
    // 使用unordered_map存储错误码和错误文本的映射关系
    static const std::unordered_map<int, std::string> error_texts = {
        {HTTP_ERR_BAD_REQUEST, "Bad Request"},
        {HTTP_ERR_FORBIDDEN, "Forbidden"},
        {HTTP_ERR_NOT_FOUND, "Not Found"},
        {HTTP_ERR_INTERNAL_SERVER_ERROR, "Internal Server Error"}
    };

    // 查找错误码对应的文本
    auto it = error_texts.find(this->err_code);
    if (it != error_texts.end()) {
        return it->second;
    }
    
    // 如果未找到对应错误码，返回默认值
    return "Unknown Error";
}

// 处理异常时，使用本函数生成提示客户端出错的HTML报文
std::string HttpRequestException::get_err_resp() const
{
    std::string html = 
        "<html>"
        "<head><title>" + std::to_string(this->err_code) + " " + get_err_text() + "</title></head>"
        "<body><h1>" + std::to_string(this->err_code) + " " + get_err_text() + ": " + this->what() + "</h1></body>"
        "</html>";
    
    std::string response = 
        "HTTP/1.1 " + std::to_string(this->err_code) + " " + get_err_text() + "\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: " + std::to_string(html.size()) + "\r\n"
        "Connection: close\r\n"
        "\r\n" + html;
    
    return response;
}

// 使用正则表达式提取 HTTP 请求路径
std::string HttpRequest::extract_path(const std::string &req)
{
    std::regex request_pattern(R"(GET\s+(.*?)\s+HTTP/1\.[01])");
    std::smatch match;
    
    if (std::regex_search(req, match, request_pattern)) {
        std::string path = match[1].str();
        if (path.empty()){
            path = "/";
        }
        if (path[path.length() - 1] == '/') {
            path += "index.html"; // 默认页面
        }
        LOG_DEBUG("path: %s", path.c_str());
        return path;
    }
    
    throw HttpRequestException("Invalid HTTP request path", 400);
}

// 解析Range头部的字符串为HttpRange结构体数组，需要提供文件大小
std::vector<HttpRange> HttpRequest::parse_ranges(off_t file_size)
{
    std::vector<HttpRange> ranges;
    
    // 检查是否以"bytes="开头
    if (range_header.substr(0, 6) != "bytes=") {
        return ranges;
    }
    
    // 提取范围部分
    std::string range_part = range_header.substr(6);
    
    // 分割多个范围（用逗号分隔）
    std::stringstream ss(range_part);
    std::string range_str;
    
    while (std::getline(ss, range_str, ',')) {
        // 去除空格
        range_str.erase(0, range_str.find_first_not_of(' '));
        range_str.erase(range_str.find_last_not_of(' ') + 1);
        
        // 解析范围格式: start-end 或 start- 或 -suffix_length
        size_t dash_pos = range_str.find('-');
        if (dash_pos == std::string::npos) {
            continue; // 格式错误
        }
        
        std::string start_str = range_str.substr(0, dash_pos);
        std::string end_str = range_str.substr(dash_pos + 1);
        
        off_t start, end;
        
        if (start_str.empty() && !end_str.empty()) {
            // 格式: -suffix_length (从文件末尾倒数指定字节数)
            off_t suffix_length = std::stoll(end_str);
            if (suffix_length > file_size) {
                start = 0;
            } else {
                start = file_size - suffix_length;
            }
            end = file_size - 1;
        } else if (!start_str.empty() && end_str.empty()) {
            // 格式: start- (从指定位置到文件末尾)
            start = std::stoll(start_str);
            if (start >= file_size) {
                continue; // 起始位置超出文件大小
            }
            end = file_size - 1;
        } else if (!start_str.empty() && !end_str.empty()) {
            // 格式: start-end
            start = std::stoll(start_str);
            end = std::stoll(end_str);
            
            if (start > end || start >= file_size) {
                continue; // 无效范围
            }
            
            if (end >= file_size) {
                end = file_size - 1; // 调整到文件末尾
            }
        } else {
            continue; // 无效格式
        }
        
        ranges.emplace_back(start, end);
    }
    
    return ranges;
}

// 提取Range头部字符串
void HttpRequest::parse_range_header(const std::string& request_data)
{
    // 解析Range头部
    std::regex range_pattern(R"(Range:\s*(.+?)\r\n)", std::regex_constants::icase);
    std::smatch range_match;
    
    if (std::regex_search(request_data, range_match, range_pattern)) {
        this->range_header = range_match[1].str();
        // 注意：此时我们还不知道文件大小，因此暂时不解析范围
        // 在实际处理请求时，需要根据文件大小再解析
        this->is_range_request = true;
        LOG_DEBUG("Range request detected: %s", range_header.c_str());
    }
}

HttpRequest::HttpRequest(int32_t fd, const std::string& request_data) : client_fd(fd), is_range_request(false)
{
    this->filepath = HttpRequest::extract_path(request_data);
    LOG_INFO("HTTP request for: %s", this->filepath.c_str());
    
    this->parse_range_header(request_data);
}
