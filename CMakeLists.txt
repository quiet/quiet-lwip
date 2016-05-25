cmake_minimum_required (VERSION 2.6)
project (Quiet-LWIP)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_definitions(-DQUIET_DEBUG)
    set(CMAKE_C_FLAGS ${CMAKE_C_FLAGS} "-g3 -O0")
    set(CMAKE_EXE_LINKER_FLAGS ${CMAKE_EXE_LINKER_FLAGS} "-Wl,-no_pie")
else()
    if("$CMAKE_C_COMPILER_ID}" STREQUAL "Clang")
      set(CMAKE_C_FLAGS ${CMAKE_C_FLAGS} "-Ofast -flto")
      set(CMAKE_EXE_LINKER_FLAGS ${CMAKE_EXE_LINKER_FLAGS} "-Ofast -flto")
    else()
      set(CMAKE_C_FLAGS ${CMAKE_C_FLAGS} "-O2")
    endif()
endif()

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c11 -Wpedantic -Werror -Wall")
set(SRCFILES src/driver.c)

include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/include/lwip)
include_directories(${CMAKE_SOURCE_DIR}/include/lwip/ipv4)
set(LIBRARY_OUTPUT_PATH ${PROJECT_BINARY_DIR}/lib)
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR}/bin)
add_subdirectory(src/lwip)

add_library(quiet_lwip ${SRCFILES} $<TARGET_OBJECTS:lwip_sys> $<TARGET_OBJECTS:lwip_core> $<TARGET_OBJECTS:lwip_api> $<TARGET_OBJECTS:lwip_netif> $<TARGET_OBJECTS:lwip_ipv4>)
target_link_libraries(quiet_lwip quiet)

add_custom_target(lwip-h ALL COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/include/lwip/ ${CMAKE_BINARY_DIR}/include/lwip)
add_custom_target(quiet-lwip-h ALL COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/include/quiet-lwip.h ${CMAKE_BINARY_DIR}/include/quiet-lwip.h)

add_executable(loopback EXCLUDE_FROM_ALL examples/loopback.c)
target_link_libraries(loopback quiet_lwip)
set(buildable_examples ${buildable_examples} loopback)

add_custom_target(examples DEPENDS ${buildable_examples})

