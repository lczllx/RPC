cmake_minimum_required(VERSION 3.16)

project(lcz_rpc LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(LCZ_RPC_BUILD_EXAMPLES "构建 example 目录中的示例程序" ON)
option(LCZ_RPC_BUILD_TESTS "构建单元测试 (GoogleTest，首次需联网拉取)" OFF)

# 统一使用系统安装的 jsoncpp（头文件路径 <jsoncpp/json/json.h>）
# 先尝试 CMake 配置，若无则用 pkg-config（部分系统仅提供 pkg-config）
find_package(jsoncpp QUIET)
if(jsoncpp_FOUND)
    if(NOT TARGET jsoncpp_lib)
        add_library(jsoncpp_lib INTERFACE)
        target_link_libraries(jsoncpp_lib INTERFACE jsoncpp::jsoncpp)
    endif()
    message(STATUS "使用系统安装的 jsoncpp (CMake)")
else()
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(JsonCpp jsoncpp)
        if(NOT JsonCpp_FOUND)
            pkg_check_modules(JsonCpp libjsoncpp)
        endif()
    endif()
    if(JsonCpp_FOUND)
        add_library(jsoncpp_lib INTERFACE)
        target_include_directories(jsoncpp_lib INTERFACE ${JsonCpp_INCLUDE_DIRS})
        target_link_libraries(jsoncpp_lib INTERFACE ${JsonCpp_LINK_LIBRARIES})
        message(STATUS "使用系统安装的 jsoncpp (pkg-config)")
    else()
        # 最后尝试：直接找头文件和库（部分环境 pkg-config 包名不同）
        find_path(JSONCPP_INCLUDE_DIR jsoncpp/json/json.h PATHS /usr/include /usr/local/include)
        find_library(JSONCPP_LIBRARY jsoncpp PATHS /usr/lib /usr/local/lib /usr/lib/x86_64-linux-gnu)
        if(JSONCPP_INCLUDE_DIR AND JSONCPP_LIBRARY)
            add_library(jsoncpp_lib INTERFACE)
            target_include_directories(jsoncpp_lib INTERFACE ${JSONCPP_INCLUDE_DIR})
            target_link_libraries(jsoncpp_lib INTERFACE ${JSONCPP_LIBRARY})
            message(STATUS "使用系统安装的 jsoncpp (find_path/find_library)")
        else()
            message(FATAL_ERROR "未找到 jsoncpp。请安装: Ubuntu/Debian: sudo apt install libjsoncpp-dev; CentOS/RHEL: sudo yum install jsoncpp-devel")
        endif()
    endif()
endif()

# Protobuf（路径二：纯 Proto RPC）
find_package(Protobuf REQUIRED)
set(LCZ_PROTO_FILE "${CMAKE_CURRENT_SOURCE_DIR}/proto/rpc_envelope.proto")
set(LCZ_PROTOBUF_IMPORT_DIRS ${CMAKE_CURRENT_SOURCE_DIR})
protobuf_generate_cpp(LCZ_PROTO_SRCS LCZ_PROTO_HDRS ${LCZ_PROTO_FILE})
add_library(lcz_rpc_proto STATIC ${LCZ_PROTO_SRCS})
target_link_libraries(lcz_rpc_proto PUBLIC protobuf::libprotobuf)
target_include_directories(lcz_rpc_proto PUBLIC
  ${CMAKE_CURRENT_BINARY_DIR}
  ${Protobuf_INCLUDE_DIRS}
)

set(MUDUO_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(muduo)

add_subdirectory(src)

if(LCZ_RPC_BUILD_EXAMPLES)
  add_subdirectory(example)
endif()

if(LCZ_RPC_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

