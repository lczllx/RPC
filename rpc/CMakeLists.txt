cmake_minimum_required(VERSION 3.16)

project(lcz_rpc LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(LCZ_RPC_BUILD_EXAMPLES "构建 example 目录中的示例程序" ON)

# 优先使用系统安装的 jsoncpp，如果找不到再使用 FetchContent
find_package(jsoncpp QUIET)
if(jsoncpp_FOUND)
    # 系统安装的 jsoncpp，创建别名以统一接口
    if(NOT TARGET jsoncpp_lib)
        add_library(jsoncpp_lib INTERFACE)
        target_link_libraries(jsoncpp_lib INTERFACE jsoncpp::jsoncpp)
    endif()
    message(STATUS "使用系统安装的 jsoncpp")
else()
    # 使用 FetchContent 下载 jsoncpp
    include(FetchContent)
    set(JSONCPP_WITH_TESTS OFF CACHE BOOL "" FORCE)
    set(JSONCPP_WITH_POST_BUILD_UNITTEST OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
      jsoncpp
      GIT_REPOSITORY https://github.com/open-source-parsers/jsoncpp.git
      GIT_TAG 1.9.5
    )
    FetchContent_MakeAvailable(jsoncpp)
    message(STATUS "使用 FetchContent 下载的 jsoncpp")
endif()

set(MUDUO_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(muduo)

add_subdirectory(src)

if(LCZ_RPC_BUILD_EXAMPLES)
  add_subdirectory(example)
endif()

