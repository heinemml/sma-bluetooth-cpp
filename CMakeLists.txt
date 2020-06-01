cmake_minimum_required(VERSION 3.13)
project(sma_bluetooth)

set(CMAKE_CXX_STANDARD 17)

include_directories(.)

find_package(LibXml2 REQUIRED)
find_package(Threads REQUIRED)

add_library(bluez INTERFACE)
target_link_libraries(bluez INTERFACE bluetooth Threads::Threads)

add_library(mysql INTERFACE)
target_link_libraries(mysql INTERFACE mysqlclient Threads::Threads z m dl)

add_executable(sma_bluetooth
        almanac.c
        repost.c
        sb_commands.c
        sma_mysql.c
        sma_mysql.h
        smatool.c
        )

target_link_libraries(sma_bluetooth mysqlclient bluez mysql curl LibXml2::LibXml2)
