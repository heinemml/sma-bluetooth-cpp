cmake_minimum_required(VERSION 3.13)
project(sma_bluetooth)

set(CMAKE_CXX_STANDARD 17)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pedantic -Wno-psabi")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

include_directories(.)

find_package(LibXml2 REQUIRED)
find_package(Threads REQUIRED)

add_library(bluez INTERFACE)
target_link_libraries(bluez INTERFACE bluetooth Threads::Threads)

add_library(mysql INTERFACE)
target_link_libraries(mysql INTERFACE mysqlclient Threads::Threads z m dl)

find_package(fmt)

set(SOURCES
        almanac.cpp
        bt_connection.cpp
        repost.cpp
        sb_commands.cpp
        sma_mysql.cpp
        smatool.cpp
        )

add_executable(smatoolpp ${SOURCES})

target_link_libraries(smatoolpp mysqlclient bluez mysql curl LibXml2::LibXml2 fmt::fmt)

configure_file(sma.in.new ${CMAKE_CURRENT_BINARY_DIR}/sma.in COPYONLY)
configure_file(smatool.xml ${CMAKE_CURRENT_BINARY_DIR}/smatool.xml COPYONLY)