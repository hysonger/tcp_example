extern "C" {
#include <sys/socket.h>

#include <signal.h>
}
#include <ctime>
#include "tcp_public.hpp"

// 获取当前计算机本地时间并转换成字符串
// e.g. 2025-08-02 18:22:51
std::string get_current_time()
{
    time_t now = time(nullptr);
    char buf[64] = {0};
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&now));
    return std::string(buf);
}

void __format_log(FILE *stream, const std::string& message, ...) {
    va_list args;
    va_start(args, message);
    fprintf(stream, "===> [%s] ", get_current_time().c_str());
    vfprintf(stream, message.c_str(), args);
    va_end(args);
    fprintf(stream, "\n");
    fflush(stream);
}

inline bool is_ignorable_error()
{
    return (errno == EAGAIN) || (errno == EWOULDBLOCK) || (errno == EINTR);
}

void sigpipe_handler(int sig)
{
    LOG_INFO("SIG %d caught!", sig);
}

// 因为无论是客户端还是服务端，最终都需要收发数据，逻辑也大同小异
// 所以统一提取到这里

/**
 * @brief 非阻塞方式接收数据
 * 
 * 该函数通过非阻塞方式从指定socket接收数据，支持重试机制和部分发送处理
 * 
 * @param socket_fd 文件描述符，用于标识要发送接收数据的套接字
 * @param buf 指向要接收数据的缓冲区指针
 * @param send_size 要接收的数据大小
 * @param max_retry_times 最大重试次数
 * 
 * @throw TcpRuntimeException 当接收失败且错误不可忽略或达到最大重试次数时抛出异常
 */
void recv_data_nonblock(int32_t socket_fd, char *buf, uint16_t recv_size, uint32_t max_retry_times)
{
    for (uint32_t retry_times = 0; retry_times < max_retry_times; ) {
        ssize_t len = recv(socket_fd, buf, recv_size, MSG_DONTWAIT); // 在recv时，也可独立的指定非阻塞接收
        if (len < 0) {
            if (is_ignorable_error()) {
                retry_times++;
                continue;
            }
            throw TcpRuntimeException("Failed to recv data, error cannot be ignored", __FILENAME__, __LINE__);
        }

        if (len == 0) {
            retry_times++;
            continue;
        }

        // 只有发送长度小于剩余长度的时候，才做减法并继续循环
        // 否则，视为接收完毕，返回
        // 这样的写法是为了谨慎的预防无符号数回绕
        if ((len < UINT16_MAX) && (len < (ssize_t)recv_size)) {
            recv_size -= (uint16_t)len;
            buf += len;
        } else {
            return;
        }
    }

    throw TcpRuntimeException("Failed to recv data, Reached max retries", __FILENAME__, __LINE__);
}

/**
 * @brief 非阻塞方式发送数据
 * 
 * 该函数通过非阻塞方式向指定文件描述符发送数据，支持重试机制和部分发送处理
 * 
 * @param socket_fd 文件描述符，用于标识要发送数据的套接字
 * @param buf 指向要发送数据的缓冲区指针
 * @param send_size 要发送的数据大小
 * @param max_retry_times 最大重试次数
 * 
 * @throw TcpRuntimeException 当发送失败且错误不可忽略或达到最大重试次数时抛出异常
 */
void send_data_nonblock(int32_t socket_fd, const char *buf, uint16_t send_size, uint32_t max_retry_times)
{
    for (uint32_t retry_times = 0; retry_times < max_retry_times; ) {
        // 为SIGPIPE注册处理函数；
        // 当使用write()发送数据时，如果对方已经关闭了连接
        // 那么write()会返回-1，并且errno会设置成EPIPE
        // 这时需要处理这个错误，否则程序会退出并返回128 + SIGPIPE。
        // 例如，用Ctrl + C退出服务端进程，会在客户端触发这个问题
        // 但是如果截留该信号，也会使得下次send时的errno=EPIPE，上层一样需要处理抛出异常
        signal(SIGPIPE, sigpipe_handler);

        ssize_t len = send(socket_fd, buf, send_size, 0);
        if (len < 0) {
            if (is_ignorable_error()) {
                retry_times++;
                continue;
            }
            throw TcpRuntimeException("Failed to send data, error cannot be ignored", __FILENAME__, __LINE__);
        }

        if (len == 0) {
            retry_times++;
            continue;
        }

        // 只有发送长度小于剩余长度的时候，才做减法并继续循环
        // 否则，视为发送完毕，返回
        // 这样的写法是为了谨慎的预防无符号数回绕
        if ((len < UINT16_MAX) && (len < (ssize_t)send_size)) {
            send_size -= (uint16_t)len;
            buf += len;
        } else {
            return;
        }
    }

    throw TcpRuntimeException("Failed to send data, reached max retries", __FILENAME__, __LINE__);
}
