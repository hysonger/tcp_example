extern "C" {
#include <fcntl.h>

#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <netinet/in.h>

#include <signal.h>
}
#include <ctime>
#include "tcp_public.hpp"

constexpr static uint32_t MAX_RETRY_TIMES = 200;
constexpr static uint32_t IO_WAIT_TIMEOUT = 10000; // usec

constexpr static uint32_t EPOLL_RETRY_TIMES = 5;
constexpr static uint32_t EPOLL_WAIT_TIMEOUT = 1000; // msec

// 获取当前计算机本地时间并转换成字符串
// e.g. 2025-08-02 18:22:51
std::string get_current_time()
{
    time_t now = time(nullptr);
    char buf[64] = {0};
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&now));
    return std::string(buf);
}

void __format_log(FILE *stream, const std::string& message, const char *file_name, uint32_t line_number, ...) {
    va_list args;
    va_start(args, line_number);
    fprintf(stream, "===> [%s][%s:%u] ", get_current_time().c_str(), file_name, line_number);
    vfprintf(stream, message.c_str(), args);
    va_end(args);
    fprintf(stream, "\n");
    fflush(stream);
}

// 非阻塞I/O中，可以忽略的三种错误码
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
void recv_data_nonblock(int32_t socket_fd, char *buf, uint16_t recv_size)
{
    for (uint32_t retry_times = 0; retry_times < MAX_RETRY_TIMES; ) {
        ssize_t len = recv(socket_fd, buf, recv_size, MSG_DONTWAIT); // 在recv时，也可独立地指定非阻塞接收
        if (len < 0) {
            if (is_ignorable_error()) {
                //retry_times++;
                usleep(IO_WAIT_TIMEOUT);
                continue;
            }
            throw TcpRuntimeException("recv error: error cannot be ignored", __FILENAME__, __LINE__);
        }

        if (len == 0) {
            throw TcpRuntimeException("recv error: peer closed", __FILENAME__, __LINE__);
        }

        retry_times = 0;

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
void send_data_nonblock(int32_t socket_fd, const char *buf, uint16_t send_size)
{
    for (uint32_t retry_times = 0; retry_times < MAX_RETRY_TIMES; ) {
        // 为SIGPIPE注册处理函数；
        // 当使用write()发送数据时，如果对方已经关闭了连接
        // 那么write()会返回-1，并且errno会设置成EPIPE
        // 这时需要处理这个错误，否则程序会退出并返回128 + SIGPIPE。
        // 例如，用Ctrl + C退出服务端进程，会在客户端触发这个问题
        // 但是如果截留该信号，也会使得下次send时的errno=EPIPE，上层一样需要处理抛出异常
        signal(SIGPIPE, sigpipe_handler);

        ssize_t len = send(socket_fd, buf, send_size, MSG_DONTWAIT);
        if (len < 0) {
            if (is_ignorable_error()) {
                retry_times++;
                usleep(IO_WAIT_TIMEOUT);
                LOG_INFO("retry times %u", retry_times);
                continue;
            }
            LOG_ERR("send error: %s", strerror(errno));
            throw TcpRuntimeException("send error: error cannot be ignored", __FILENAME__, __LINE__);
        }

        if (len == 0) {
            LOG_ERR("send error: peer closed");
            throw TcpRuntimeException("send error: peer closed", __FILENAME__, __LINE__);
        }

        retry_times = 0;

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
    LOG_ERR("send_data_nonblock: send failed");
    throw TcpRuntimeException("Failed to send data, reached max retries", __FILENAME__, __LINE__);
}

// 利用sendfile向socket发送文件；有更精细需求的不适用本函数
void sendfile_nonblock(int32_t socket_fd, const std::string& file_path, off_t offset, off_t length)
{
    int file_fd = open(file_path.c_str(), O_RDONLY);
    if (file_fd < 0) {
        throw TcpRuntimeException("Open file failed", __FILENAME__, __LINE__);
    }

    off_t remaining = length;
    
    uint32_t retry_times = 0;
    
    while (remaining > 0 && retry_times < MAX_RETRY_TIMES) {
        // 使用 sendfile 发送数据
        ssize_t sent = sendfile(socket_fd, file_fd, &offset, remaining);
        if (sent < 0) {
            // 检查是否为可忽略的错误
            if (is_ignorable_error()) {
                //retry_times++;
                usleep(IO_WAIT_TIMEOUT);
                continue;
            }
            
            close(file_fd);
            LOG_ERR("Send file failed, errno=%d", errno);
            throw TcpRuntimeException("Send file failed", __FILENAME__, __LINE__);
        }
        
        if (sent == 0) {
            // 没有更多数据可以发送，可能连接已关闭
            LOG_ERR("Send file failed, peer closed");
            break;
        }
        
        // 重置重试次数
        retry_times = 0;
        
        // 更新剩余字节数和偏移量
        remaining -= sent;
        //offset += sent;
        LOG_DEBUG("Sent %d bytes, remaining %d bytes, offset %d", sent, remaining, offset);
    }
    
    close(file_fd);
    // 检查是否所有数据都已发送
    if (remaining > 0) {
        LOG_ERR("Send file failed, incomplete transfer");
        throw TcpRuntimeException("Send file failed, incomplete transfer", __FILENAME__, __LINE__);
    }
}

// 该函数主要为试验性质，通过epoll触发EPOLLOUT进而触发发送数据
// 属于一种邪修，说不定特殊场景下会有用……
void send_data_epoll(int32_t socket_fd, const char *buf, uint16_t send_size)
{
    int epfd = epoll_create1(0);
    struct epoll_event ev, events[1];

    // 将 socket 添加到 epoll
    ev.events = EPOLLOUT;
    ev.data.fd = socket_fd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, socket_fd, &ev);

    for (uint32_t retry_times = 0; retry_times < EPOLL_RETRY_TIMES; ) {
        epoll_wait(epfd, events, sizeof(events) / sizeof(events[0]), EPOLL_WAIT_TIMEOUT);
        if (events[0].data.fd != socket_fd || !(events[0].events & EPOLLOUT)) {
            continue;
        }

        // 为SIGPIPE注册处理函数；
        // 当使用write()发送数据时，如果对方已经关闭了连接
        // 那么write()会返回-1，并且errno会设置成EPIPE
        // 这时需要处理这个错误，否则程序会退出并返回128 + SIGPIPE。
        // 例如，用Ctrl + C退出服务端进程，会在客户端触发这个问题
        // 但是如果截留该信号，也会使得下次send时的errno=EPIPE，上层一样需要处理抛出异常
        signal(SIGPIPE, sigpipe_handler);

        ssize_t len = send(socket_fd, buf, send_size, MSG_DONTWAIT);
        if (len < 0) {
            if (is_ignorable_error()) {
                continue;
            }
            close(epfd);
            throw TcpRuntimeException("send error", __FILENAME__, __LINE__);
        }

        if (len == 0) {
            close(epfd);
            throw TcpRuntimeException("send error: peer closed", __FILENAME__, __LINE__);
        }

        retry_times = 0;

        // 只有发送长度小于剩余长度的时候，才做减法并继续循环
        // 否则，视为发送完毕，返回
        // 这样的写法是为了谨慎的预防无符号数回绕
        if ((len < UINT16_MAX) && (len < (ssize_t)send_size)) {
            send_size -= (uint16_t)len;
            buf += len;
        } else {
            close(epfd);
            return;
        }
    }

    close(epfd);
    throw TcpRuntimeException("Failed to send data, reached max retries", __FILENAME__, __LINE__);
}