file(GLOB SRCFILES "*.c")

add_subdirectory(api)
add_subdirectory(core)
add_subdirectory(netif)
add_library(lwip_sys OBJECT ${SRCFILES})
