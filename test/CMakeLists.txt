
enable_testing()

set(CATCH_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# 定义创建单个 Catch2 测试的函数
function(add_catch_test TEST_NAME)
    # 参数：测试名称，其余为该测试的源文件（不含 catch_amalgamated.cpp）
    set(SOURCES ${ARGN})

    # 自动包含 catch_amalgamated.cpp
    list(APPEND SOURCES ${CATCH_INCLUDE_DIR}/catch_amalgamated.cpp)

    # 创建可执行文件
    add_executable(${TEST_NAME} ${SOURCES})

    # 包含目录
    target_include_directories(${TEST_NAME} PRIVATE
        ${CATCH_INCLUDE_DIR}
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_SOURCE_DIR}/include/core
        ${PROJECT_SOURCE_DIR}/include/core/ext
        ${PROJECT_SOURCE_DIR}/external/json
    )

    # 链接库（如果存在）
    if(TARGET systemc)
        target_link_libraries(${TEST_NAME} systemc)
    endif()

    # 注册为 CTest 测试
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endfunction()

# 收集所有测试源文件
file(GLOB TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/test_*.cc")

# 为每个测试源文件创建一个独立的测试
foreach(TEST_SRC ${TEST_SOURCES})
    # 提取测试名，如 test_xxx.cc -> test_xxx
    get_filename_component(TEST_NAME ${TEST_SRC} NAME_WE)
    
    # 跳过不需要单独构建的文件
    if(NOT ${TEST_NAME} STREQUAL "catch_amalgamated")
        add_catch_test(${TEST_NAME} ${TEST_SRC})
    endif()
endforeach()


# # 添加可执行文件
# add_executable(packet_pool_tests 
#     src/test_packet_pool.cc
#     # 添加您的所有源文件...
#     src/module_factory.cc
# )

# # 包含路径
# target_include_directories(packet_pool_tests PRIVATE include ext)

# # 链接 gtest
# target_link_libraries(packet_pool_tests GTest::GTest GTest::Main)


# add_executable(tlm_exts_test test_tlm_exts.cpp)
# target_link_libraries(tlm_exts_test GTest::gtest GTest::gtest_main pthread systemc)


