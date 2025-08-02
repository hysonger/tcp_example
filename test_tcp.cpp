
int test_tcp_communication();
int test_tcp_10client();
int test_parallel_communication();

int main(const int argc, const char *argv[])
{
    test_tcp_communication();
    test_tcp_10client();
    test_parallel_communication();

    return 0;
}