# sample/CMakeLists.txt
cmake_minimum_required(VERSION 3.16)
project(sample1)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 启用调试和 DPRINTF
add_compile_definitions(DEBUG_PRINT)

# 包含主项目的头文件目录
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/systemc-3.0.1/src
    ${CMAKE_SOURCE_DIR}/external/json
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# 如果主项目已经构建了 systemc，链接它
if (TARGET systemc)
    set(SYSTEMC_LIB systemc)
else()
    # 否则假设 systemc 已经安装或在系统路径中
    find_library(SYSTEMC_LIB systemc REQUIRED)
endif()

# 定义共享库的公共源文件
set(COMMON_SOURCES
    ${CMAKE_SOURCE_DIR}/src/core/module_factory.cc
)

# cpu_cluster 库
set(CPU_CLUSTER_SOURCES
    modules/cpu_cluster.cc
    ${COMMON_SOURCES}
)

add_library(libcpu_cluster SHARED ${CPU_CLUSTER_SOURCES})
target_link_libraries(libcpu_cluster ${SYSTEMC_LIB})
set_target_properties(libcpu_cluster PROPERTIES PREFIX "")

# mem_cluster 库
set(MEM_CLUSTER_SOURCES
    modules/mem_cluster.cc
    ${COMMON_SOURCES}
)

add_library(libmem_cluster SHARED ${MEM_CLUSTER_SOURCES})
target_link_libraries(libmem_cluster ${SYSTEMC_LIB})
set_target_properties(libmem_cluster PROPERTIES PREFIX "")

# 确保生成的库放在正确的位置
set_target_properties(libcpu_cluster libmem_cluster PROPERTIES 
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/build)
