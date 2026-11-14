cmake_minimum_required(VERSION 3.27)
project(server CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Threads REQUIRED)
find_package(PostgreSQL REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(PQXX REQUIRED IMPORTED_TARGET libpqxx)

add_executable(server
    src/tokens.h
    src/main.cpp
    src/data_structures.h
    src/utilities.h
)

target_link_libraries(server PRIVATE PkgConfig::PQXX Threads::Threads PostgreSQL::PostgreSQL)

if(WIN32)
    target_link_libraries(server PRIVATE ws2_32)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(server PRIVATE -Wall -Wextra -Wpedantic)
endif()