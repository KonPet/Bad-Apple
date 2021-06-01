cmake_minimum_required(VERSION 3.17)
project(BadAppleEncode)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS_RELEASE -O3)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_FLAGS_RELEASE -O3)
set(CMAKE_CXX_FLAGS "-Wall -Wextra")

add_executable(BadAppleEncode src/main.cpp src/lzss.c)