add_library(lcz_rpc INTERFACE)

# 生成头在上级 build 目录（rpc/build/proto/），由 lcz_rpc_proto 提供
target_include_directories(lcz_rpc
  INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../muduo>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/..>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(lcz_rpc
  INTERFACE
    muduo_net
    muduo_base
    jsoncpp_lib
    lcz_rpc_proto
    pthread
)
