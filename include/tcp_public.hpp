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
    TcpRuntimeException(const std::string& message, const std::string& file_name, const uint32_t line_number) :
        std::runtime_error(
            "EXCEPTION: " + message + " [errno=" + std::to_string(errno) + "] at " +
            file_name + ":" + std::to_string(line_number) + "") {}

    TcpRuntimeException(TcpRuntimeException&& other, const std::string& message,
        const std::string& file_name, const uint32_t line_number) :
        std::runtime_error(std::string(other.what()) +
            "EXCEPTION: " + message + " [errno=" + std::to_string(errno) + "] at " +
            file_name + ":" + std::to_string(line_number) + "") {}
};

// === EXCEPTION END ===

void __format_log(FILE *stream, const std::string& message, ...);

// 使用宏的可变参数时，##使得可选参数可以被省略

#define LOG_INFO(message, ...) __format_log(stdout, ((message)), ##__VA_ARGS__);
#define LOG_ERR(message, ...) __format_log(stderr, ((message)), ##__VA_ARGS__);

#define __FILENAME__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)

std::string get_current_time();

inline bool is_ignorable_error()
{
    return (errno == EAGAIN) || (errno == EWOULDBLOCK) || (errno == EINTR);
}

#endif // TCP_PUBLIC_HPP