# CMakeLists.txt - 主项目
cmake_minimum_required(VERSION 3.16)  # SystemC 3.0.1 需要较新版本
project(gemsc CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 启用调试和 DPRINTF
add_compile_definitions(DEBUG_PRINT)
# 启用调试信息
set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_CXX_FLAGS_DEBUG "-g -DDEBUG_PRINT")

if (NOT TARGET systemc)
    add_subdirectory(systemc-3.0.1 EXCLUDE_FROM_ALL)
endif()

# 如果你想控制 build 类型
set_target_properties(systemc PROPERTIES CXX_STANDARD 17)

include_directories(
    include
    include/core
    systemc-3.0.1/src
    external/json
)

file(GLOB_RECURSE SOURCE_FILES 
    "src/core/*.cc" 
    "src/utils/*.cc" 
    "src/main.cpp" 
)

add_executable(sim ${SOURCE_FILES})

# 链接 systemc 库（由 add_subdirectory 提供）
target_link_libraries(sim systemc)

add_subdirectory(samples/simple1)

# 可选：设置输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/build)
# 可选：启用 sanitizer（调试用）
# target_compile_options(sim PRIVATE -fsanitize=address -fno-omit-frame-pointer)
# target_link_options(sim PRIVATE -fsanitize=address)
