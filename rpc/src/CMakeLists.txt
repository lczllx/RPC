add_library(lcz_rpc INTERFACE)

target_include_directories(lcz_rpc
  INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(lcz_rpc
  INTERFACE
    muduo_net
    jsoncpp_lib
    pthread
)
