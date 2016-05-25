file(GLOB SRCFILES "*.c")

add_subdirectory(ipv4)
add_library(lwip_core OBJECT ${SRCFILES})
