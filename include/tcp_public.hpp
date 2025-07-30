#ifndef TCP_PUBLIC_HPP
#define TCP_PUBLIC_HPP

#include <cstdint>
#include <cstdarg>
#include <cstdio>

#include <string>
#include <stdexcept>

// === EXCEPTION ===

class TcpRuntimeException : public std::runtime_error {
public:
    TcpRuntimeException(const std::string& message, const std::string& file_name, const uint32_t line_number) :
        std::runtime_error("["  + file_name + ":" + std::to_string(line_number) + "] EXCEPTION: " + message +
        " [errno=" + std::to_string(errno) + "]") {}
};

// === EXCEPTION END ===

void __format_log(FILE *stream, const std::string& message, ...);

// 使用宏的可变参数时，##使得可选参数可以被省略

#define LOG_INFO(message, ...) __format_log(stdout, ((message)), ##__VA_ARGS__);
#define LOG_ERR(message, ...) __format_log(stderr, ((message)), ##__VA_ARGS__);

std::string get_current_time();

inline bool is_ignorable_error()
{
    return (errno == EAGAIN) || (errno == EWOULDBLOCK) || (errno == EINTR);
}

#endif // TCP_PUBLIC_HPP