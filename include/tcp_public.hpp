#ifndef TCP_PUBLIC_HPP
#define TCP_PUBLIC_HPP

#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include <string>
#include <stdexcept>

// === EXCEPTION ===

class TcpRuntimeException : public std::runtime_error {
public:
    // 初始抛出问题的地方，使用该构造函数
    TcpRuntimeException(const std::string& message, const char *file_name, uint32_t line_number) :
        std::runtime_error(
            "EXCEPTION: " + message + " [errno=" + std::to_string(errno) + "] at: \n\t" +
            std::string(file_name) + ":" + std::to_string(line_number)) {}

    // 如果不处理异常，则使用该构造函数，追加调用栈信息
    TcpRuntimeException(TcpRuntimeException&& other, const char *file_name, uint32_t line_number) :
        std::runtime_error(std::string(other.what()) + "\n\t" +
            std::string(file_name) + ":" + std::to_string(line_number)) {}
};

// === EXCEPTION END ===

void __format_log(FILE *stream, const std::string& message, ...);

#define __FILENAME__ (((strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)))

// 使用宏的可变参数时，##使得可选参数可以被省略

#define LOG_INFO(message, ...) __format_log(stdout, ((message)), ##__VA_ARGS__)
#define LOG_ERR(message, ...) __format_log(stderr, ((message)), ##__VA_ARGS__)

// 快速重抛异常的宏
#define RETHROW(e) do {throw TcpRuntimeException(std::move(((e))), __FILENAME__, __LINE__); } while (0)

std::string get_current_time();

void recv_data_nonblock(int32_t socket_fd, char *buf, uint16_t recv_size, uint32_t max_retry_times);
void send_data_nonblock(int32_t socket_fd, const char *buf, uint16_t send_size, uint32_t max_retry_times);

#endif // TCP_PUBLIC_HPP