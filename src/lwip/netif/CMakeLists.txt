file(GLOB SRCFILES "*.c")

add_library(lwip_netif OBJECT ${SRCFILES})
