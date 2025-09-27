cmake_minimum_required(VERSION 3.14)
project(GemScTests)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找 gtest
find_package(GTest REQUIRED)

# 添加可执行文件
add_executable(packet_pool_tests 
    src/test_packet_pool.cc
    # 添加您的所有源文件...
    src/module_factory.cc
)

# 包含路径
target_include_directories(packet_pool_tests PRIVATE include ext)

# 链接 gtest
target_link_libraries(packet_pool_tests GTest::GTest GTest::Main)


add_executable(tlm_exts_test test_tlm_exts.cpp)
target_link_libraries(tlm_exts_test GTest::gtest GTest::gtest_main pthread systemc)


