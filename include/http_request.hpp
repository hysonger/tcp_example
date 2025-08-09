#ifndef HTTP_REQUEST_HPP
#define HTTP_REQUEST_HPP

#include <cstdint>
#include <string>
#include <filesystem>

enum HttpErrCode {
    HTTP_ERR_OK = 200,
    HTTP_ERR_BAD_REQUEST = 400,
    HTTP_ERR_FORBIDDEN = 403,
    HTTP_ERR_NOT_FOUND = 404,
    HTTP_ERR_INTERNAL_SERVER_ERROR = 500,
};

enum HttpRequestType {
    HTTP_REQUEST_GET,
    HTTP_REQUEST_HEAD,
    HTTP_REQUEST_POST,
    HTTP_REQUEST_PUT,
    HTTP_REQUEST_DELETE,
    HTTP_REQUEST_OPTIONS,
    HTTP_REQUEST_TRACE,
    HTTP_REQUEST_CONNECT,
    HTTP_REQUEST_PATCH,
};

// 表示一个Range范围的结构体
struct HttpRange {
    off_t start;  // 起始字节位置
    off_t end;    // 结束字节位置
    bool valid;   // Range是否有效
    
    HttpRange() : start(0), end(0), valid(false) {}
    HttpRange(off_t s, off_t e) : start(s), end(e), valid(true) {}
};

class HttpRequestException : public std::runtime_error {
protected:
    uint32_t err_code;

    std::string get_err_text() const;
public:
    HttpRequestException(const std::string& message, uint32_t err_code = 400) :
        std::runtime_error(message), err_code(err_code) {}
    
    std::string get_err_resp() const;
};

class HttpRequest {
protected:
    static std::string extract_path(const std::string &req);
    void parse_range_header(const std::string& request_data);
public:
    int32_t client_fd;

    HttpRequestType type; // 请求类型
    std::string filepath; // 文件路径，此时尚未规格化，需要消息处理逻辑进行进一步处理

    bool is_range_request; // 是否为Range请求
    std::string range_header; // Range请求数据

    std::vector<HttpRange> parse_ranges(off_t file_size);
    HttpRequest(int32_t fd, const std::string& request_data);
};

#endif // HTTP_REQUEST_HPP