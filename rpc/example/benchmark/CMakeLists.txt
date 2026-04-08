# Protobuf 版（默认序列化）
add_executable(benchmark_server benchmark_server.cc)
target_link_libraries(benchmark_server PRIVATE lcz_rpc)
set_target_properties(benchmark_server PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_executable(benchmark_client benchmark_client.cc)
target_link_libraries(benchmark_client PRIVATE lcz_rpc)
set_target_properties(benchmark_client PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# JSON 版（原版副本，保留用于对比）
add_executable(benchmark_server_json benchmark_server_json.cc)
target_link_libraries(benchmark_server_json PRIVATE lcz_rpc)
set_target_properties(benchmark_server_json PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_executable(benchmark_client_json benchmark_client_json.cc)
target_link_libraries(benchmark_client_json PRIVATE lcz_rpc)
set_target_properties(benchmark_client_json PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)




