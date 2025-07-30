#include <ctime>
#include "tcp_public.hpp"

// 获取当前计算机本地时间并转换成字符串
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
    fprintf(stream, "===> [%s]", get_current_time().c_str());
    vfprintf(stream, message.c_str(), args);
    va_end(args);
    fprintf(stream, "\n");
}
