A simple TCP & HTTP protocol server & client demostration, supporting multi I/O with epoll

The core implementation is in TcpServer & TcpClient classes.
Most of I/O operations are in nonblock mode. (But it's so hard to write! I dont think it's a good idea to write all I/O operations in nonblock mode. It depends on the actual situation.)
Any application can inherit from TcpServer and rewrite certain methods to implement its own protocol.
HttpServer inherits from TcpServer for example. It reads files from html/ and deliver them to client with sendfile().

TcpServer default Message format:
msg_len(2 bytes, max num 65535, network sequence, including msg_len itself) | msg_body

GOAL:
1. basic tcp_server.c with recv & print capability OK
2. basic tcp_client.c with send capability OK
3. rewrite tcp_server.c using epoll OK
4. rewrite tcp_server.c to cpp in class & exception style OK
5. rewrite tcp_client.c to cpp, extracting common part to tcp_public OK
6. adding client recv & server send capability OK
7. decopling message parse & deal part OK
