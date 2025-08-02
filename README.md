A TCP protocol server & client demostration, supporting multi I/O

Message format:
msg_len(2 bytes, max num 65535, network sequence, including msg_len itself) | msg_body

GOAL:
1. basic tcp_server.c with recv & print capability OK
2. basic tcp_client.c with send capability OK
3. rewrite tcp_server.c using epoll OK
4. rewrite tcp_server.c to cpp in class & exception style OK
5. rewrite tcp_client.c to cpp, extracting common part to tcp_public OK
6. adding client recv & server send capability OK
7. decopling message parse & deal part OK
