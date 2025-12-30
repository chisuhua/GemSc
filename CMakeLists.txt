# CMakeLists.txt - 主项目
cmake_minimum_required(VERSION 3.16)  # SystemC 3.0.1 需要较新版本
project(gemsc CXX)

set(CMAKE_CXX_STANDARD 20)
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
set_target_properties(systemc PROPERTIES CXX_STANDARD 20)

include_directories(
    include
    include/core
    systemc-3.0.1/src
    external/json
)

file(GLOB_RECURSE SOURCE_FILES 
    "src/core/*.cc" 
    "src/utils/*.cc"
)

# 创建一个静态库用于核心功能
add_library(gemsc_core STATIC ${SOURCE_FILES})

# 链接 systemc 库（由 add_subdirectory 提供）
target_link_libraries(gemsc_core systemc)

# 创建一个专门的 sc_main 库
add_library(sc_main_obj OBJECT src/sc_main.cpp)
target_include_directories(sc_main_obj PRIVATE
    systemc-3.0.1/src
)

add_executable(sim 
    src/main.cpp
)

# 链接核心库，添加库目录路径
target_link_libraries(sim 
    gemsc_core
    systemc
)

# 显式添加依赖关系
add_dependencies(sim gemsc_core)

add_subdirectory(samples/simple1)

add_subdirectory(test)

# 可选：设置输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/build)
# 可选：启用 sanitizer（调试用）
# target_compile_options(sim PRIVATE -fsanitize=address -fno-omit-frame-pointer)
# target_link_options(sim PRIVATE -fsanitize=address)