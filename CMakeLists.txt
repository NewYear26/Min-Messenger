cmake_minimum_required(VERSION 3.16)
project(untitled LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(server main.cpp
        server.cpp)
add_executable(client client.cpp
        server.cpp
        secret.cpp
        secret.h)
