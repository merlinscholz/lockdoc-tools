cmake_minimum_required(VERSION 3.07)
project(bb2lines)

set(CMAKE_CXX_STANDARD 20)

set(SRCS
        gcov-io.cpp
        gcov-io.h
        binaryread.cpp
        binaryread.h
)

include(FindBfd.cmake)

# find_package(Libbfd)
if(LIBBFD_FOUND)
    add_definitions(-DENABLE_LIBBFD)
    include_directories(${LIBBFD_INCLUDE_DIRS})
else()
    # set(USE_LIBBFD OFF CACHE BOOL "Use libbfd for crash reporting and leak check" FORCE)
    message(WARNING "Libbfd not found, disable integrated stack trace.")
endif()

add_library(bb2lines_lib ${SRCS})

link_libraries(stdc++fs)

add_executable(bb2lines main.cpp)

target_link_libraries(bb2lines PRIVATE bb2lines_lib ${LIBBFD_LIBRARIES})