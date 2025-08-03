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

void __format_log(FILE *stream, const std::string& message, const char *file_name, uint32_t line_number, ...);

#define __FILENAME__ (((strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)))

// TIPS: 使用宏的可变参数时，##使得可选参数可以被省略

// 打印日志，不受调试宏影响，输出到标准输出；注意该函数为C风格，除首个参数外，输入字符串请以.c_str()传入
#define LOG_INFO(message, ...) __format_log(stdout, ((message)), __FILENAME__, __LINE__, ##__VA_ARGS__)

// 错误日志，不受调试宏影响，输出到标准错误；注意该函数为C风格，除首个参数外，输入字符串请以.c_str()传入
#define LOG_ERR(message, ...) __format_log(stderr, ((message)), __FILENAME__, __LINE__, ##__VA_ARGS__)

#ifndef NDEBUG
// 调试日志，受调试宏影响，调试模式下输出到标准输出；注意该函数为C风格，除首个参数外，输入字符串请以.c_str()传入
#define LOG_DEBUG(message, ...) __format_log(stdout, ((message)), __FILENAME__, __LINE__, ##__VA_ARGS__)
#else
#define LOG_DEBUG(message, ...) (void)0
#endif

// 快速封装重抛异常的宏
#define RETHROW(e) do {throw TcpRuntimeException(std::move(((e))), __FILENAME__, __LINE__); } while (0)

std::string get_current_time();

void recv_data_nonblock(int32_t socket_fd, char *buf, uint16_t recv_size);
void send_data_nonblock(int32_t socket_fd, const char *buf, uint16_t send_size);

std::string recv_with_eof(int32_t socket_fd, size_t max_size, const std::string& eof_str);

void sendfile_nonblock(int32_t socket_fd, const std::string& file_path, off_t offset, off_t length);

void send_data_epoll(int32_t socket_fd, const char *buf, uint16_t send_size);

#endif // TCP_PUBLIC_HPP