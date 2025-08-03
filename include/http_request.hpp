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
public:
    int32_t client_fd;
    std::string filepath; // 文件路径，此时尚未规格化，需要消息处理逻辑进行进一步处理

    HttpRequest(int32_t fd, const std::string& request_data);
};

#endif // HTTP_REQUEST_HPP