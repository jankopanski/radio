cmake_minimum_required(VERSION 3.3)
project(player)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -lboost_regex")

set(SOURCE_FILES player.cpp err.c err.h)
add_executable(player ${SOURCE_FILES})