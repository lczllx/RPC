add_executable(benchmark_server benchmark_server.cc)
target_link_libraries(benchmark_server PRIVATE lcz_rpc)
set_target_properties(benchmark_server PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_executable(benchmark_client benchmark_client.cc)
target_link_libraries(benchmark_client PRIVATE lcz_rpc)
set_target_properties(benchmark_client PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)




