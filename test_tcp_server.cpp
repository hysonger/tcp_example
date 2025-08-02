

int main(const int argc, const char *argv[])
{
    int test_tcp_communication();
    int test_tcp_10client();

    test_tcp_communication();
    test_tcp_10client();

    return 0;
}