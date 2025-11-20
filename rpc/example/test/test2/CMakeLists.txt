add_executable(test2_server test_server.cc)
target_link_libraries(test2_server PRIVATE lcz_rpc)

add_executable(test2_client test_client.cc)
target_link_libraries(test2_client PRIVATE lcz_rpc)

