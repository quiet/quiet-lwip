include_directories(include)

add_executable(discovery_client EXCLUDE_FROM_ALL src/discovery_client.c)
target_link_libraries(discovery_client quiet_lwip)
set(buildable_examples ${buildable_examples} discovery_client)

add_executable(discovery_server EXCLUDE_FROM_ALL src/discovery_server.c)
target_link_libraries(discovery_server quiet_lwip)
set(buildable_examples ${buildable_examples} discovery_server)

add_executable(kv_client EXCLUDE_FROM_ALL src/kv_client.c)
target_link_libraries(kv_client quiet_lwip)
set(buildable_examples ${buildable_examples} kv_client)

add_executable(kv_server EXCLUDE_FROM_ALL src/kv_server.c)
target_link_libraries(kv_server quiet_lwip)
set(buildable_examples ${buildable_examples} kv_server)

add_library(relay STATIC EXCLUDE_FROM_ALL src/relay.c)

add_executable(proxy_client EXCLUDE_FROM_ALL src/proxy_client.c)
target_link_libraries(proxy_client quiet_lwip relay)
set(buildable_examples ${buildable_examples} proxy_client)

add_executable(proxy_server EXCLUDE_FROM_ALL src/proxy_server.c)
target_link_libraries(proxy_server quiet_lwip relay)
set(buildable_examples ${buildable_examples} proxy_server)

add_custom_target(examples DEPENDS ${buildable_examples})
