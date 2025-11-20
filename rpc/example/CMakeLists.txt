add_executable(message_test message_test.cc)
target_link_libraries(message_test PRIVATE lcz_rpc)

add_executable(despacher_test despacher_test.cc)
target_link_libraries(despacher_test PRIVATE lcz_rpc)

add_subdirectory(test)

