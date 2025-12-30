// 提供一个空的sc_main实现，以解决链接时的符号问题
extern "C" int sc_main(int argc, char* argv[]) {
    // 为systemc库提供一个空的sc_main实现
    // 这样测试可以正确链接到systemc库
    return 0;
}