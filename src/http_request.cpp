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

HttpRequest::HttpRequest(int32_t fd, const std::string& request_data) : client_fd(fd) {
    this->filepath = HttpRequest::extract_path(request_data);
    LOG_INFO("HTTP request for: %s", this->filepath.c_str());
}