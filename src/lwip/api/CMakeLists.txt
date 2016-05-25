file(GLOB SRCFILES "*.c")

add_library(lwip_api OBJECT ${SRCFILES})
