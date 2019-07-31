## OnionTrace
## a program records and plays back Tor circuit building and stream assignment
project(oniontrace C)
cmake_minimum_required(VERSION 2.8 FATAL_ERROR)

## dependencies
set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/" ${CMAKE_MODULE_PATH})
find_package(GLIB REQUIRED)

include_directories(AFTER ${GLIB_INCLUDES})

## plug-ins need to disable fortification to ensure syscalls are intercepted
#add_cflags("-fPIC -fno-inline -fno-strict-aliasing -std=gnu11 -U_FORTIFY_SOURCE")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIC -std=gnu11 -fno-omit-frame-pointer -ggdb -O2")

## OnionTrace source files
set(sources
    src/oniontrace.c
    src/oniontrace-circuit.c
    src/oniontrace-config.c
    src/oniontrace-driver.c
    src/oniontrace-event-manager.c
    src/oniontrace-file.c
    src/oniontrace-logger.c
    src/oniontrace-peer.c
    src/oniontrace-player.c
    src/oniontrace-recorder.c
    src/oniontrace-timer.c
    src/oniontrace-torctl.c
)

## build the executable
add_executable(oniontrace ${sources})

## this ensures it is linked as a position-independent executable so that
## the system calls can be intercepted (so that it works in Shadow)
set_target_properties(oniontrace PROPERTIES 
    INSTALL_RPATH ${CMAKE_INSTALL_PREFIX}/lib 
    INSTALL_RPATH_USE_LINK_PATH TRUE 
    LINK_FLAGS "-pie -rdynamic -Wl,--no-as-needed"
)

## link in our dependencies and install
target_link_libraries(oniontrace ${GLIB_LIBRARIES})
install(TARGETS oniontrace DESTINATION bin)

message(STATUS "COMPILE_OPTIONS = ${CMAKE_C_FLAGS}")

