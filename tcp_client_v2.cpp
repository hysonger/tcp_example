// 这个程序基本上是全让通义灵码代工的，懒得写包装程序了
// 但是按两次回车发送的逻辑写成了实际为三次，指出来了它还是改不对，笨蛋AI

/*
PROMPT:
你是一名专业的C++网络协议工程师，现在使用TcpClient类编写一个命令行程序，输入的前两个参数分别为连接的服务器的IP地址和端口号。连接成功后，用户从命令行键入报文内容，连续两次回车视为单次报文输入结束。报文格式为：头两个字节为含头长的报文总长（网络序），紧接着为正文内容。注意包含应有的日志打印，异常输入校验和exception处理
*/

#include <iostream>
#include <string>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <arpa/inet.h>

#include "tcp_client.hpp"

/**
 * 构造报文
 * @param message 用户输入的消息内容
 * @return 构造好的完整报文
 */
std::string construct_message(const std::string& message) {
    // 报文总长度 = 消息长度 + 2字节长度字段
    uint16_t total_length = static_cast<uint16_t>(message.length() + 2);
    
    // 转换为网络字节序
    uint16_t network_length = htons(total_length);
    
    // 构造完整报文
    std::string packet;
    packet.reserve(total_length);
    packet.append(reinterpret_cast<const char*>(&network_length), sizeof(network_length));
    packet.append(message);
    
    return packet;
}

/**
 * 从标准输入读取用户输入，直到遇到连续两个换行符
 * @return 用户输入的消息内容
 */
std::string read_user_message() {
    std::string message;
    std::string line;
    
    LOG_INFO("Please enter message content (two consecutive newlines to end input):");
    
    while (true) {
        std::getline(std::cin, line);
        
        // 检查是否为连续的空行
        if (line.empty()) {
            break;
        }
        message += line + "\n";
    }
    
    // 删除最后添加的换行符（来自第二个空行）
    if (!message.empty() && message.back() == '\n') {
        message.pop_back();
    }
    
    return message;
}

int main(int argc, char* argv[]) {
    try {
        // 参数校验
        if (argc < 3) {
            std::cerr << "Usage: " << argv[0] << " <server_ip> <port>" << std::endl;
            return 1;
        }
        
        std::string server_ip = argv[1];
        uint16_t server_port = static_cast<uint16_t>(std::stoi(argv[2]));
        
        // 端口号范围校验
        if (server_port == 0) {
            LOG_ERR("Invalid port number: %hu", server_port);
            return 1;
        }
        
        LOG_INFO("Connecting to server %s:%hu...", server_ip.c_str(), server_port);
        
        // 创建TCP客户端并连接服务器
        TcpClient client(server_ip, server_port);
        
        LOG_INFO("Connection successful, ready to send messages");
        
        // 循环读取用户输入并发送报文
        while (true) {
            try {
                // 读取用户输入
                std::string message_content = read_user_message();
                
                // 检查是否要退出（直接比较原始输入）
                if (message_content == "//quit" || message_content == "//exit") {
                    LOG_INFO("Exiting program");
                    break;
                }
                
                // 构造报文
                std::string packet = construct_message(message_content);
                
                // 发送报文
                send_data_nonblock(client.get_fd(), packet.c_str(), static_cast<uint16_t>(packet.length()), 5);
                
                LOG_INFO("Message sent successfully, total length: %zu bytes", packet.length());
            } catch (const std::exception& e) {
                LOG_ERR("Error processing message: %s", e.what());
                continue;
            }
        }
    } catch (const std::exception& e) {
        LOG_ERR("Program exception: %s", e.what());
        return 1;
    } catch (...) {
        LOG_ERR("Unknown exception");
        return 1;
    }
    
    return 0;
}