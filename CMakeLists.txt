cmake_minimum_required(VERSION 3.3)
project(player)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

set(SOURCE_FILES player.cpp)
add_executable(player ${SOURCE_FILES})