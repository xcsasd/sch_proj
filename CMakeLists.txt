cmake_minimum_required(VERSION 3.27)

project(server CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(PostgreSQL_ROOT "C:/msys64/ucrt64" CACHE PATH "Path to PostgreSQL")

include(FetchContent)
FetchContent_Declare(
    libpqxx
    GIT_REPOSITORY https://github.com/jtv/libpqxx.git
    GIT_TAG        8.0.2
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(libpqxx)

find_package(Threads REQUIRED)
find_package(PostgreSQL REQUIRED)

add_executable(server
    src/tokens.h
    src/main.cpp
    src/data_structures.h
    src/utilities.h
)

target_include_directories(server PRIVATE "C:/boost_1_92_0/boost_1_92_0")
target_link_libraries(server PRIVATE pqxx Threads::Threads PostgreSQL::PostgreSQL)
if(WIN32)
    target_link_libraries(server PRIVATE ws2_32)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(server PRIVATE -Wall -Wextra -Wpedantic)
endif()
