file(GLOB SRCFILES "*.c")

add_library(lwip_ipv4 OBJECT ${SRCFILES})
