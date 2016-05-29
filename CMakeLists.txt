cmake_minimum_required(VERSION 3.3)
project(radio)

find_package(Boost)
IF (Boost_FOUND)
 include_directories(${Boost_INCLUDE_DIR})
endif()
set (Boost_USE_STATIC_LIBS OFF) # enable dynamic linking
set (Boost_USE_MULTITHREAD ON)  # enable multithreading
find_package (Boost COMPONENTS REQUIRED regex)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -Wall")

set(PLAYER_SOURCE_FILES player.cpp err.cpp err.h)
set(MASTER_SOURCE_FILES master.cpp err.cpp err.h TelnetSession.cpp TelnetSession.h SocketListener.cpp SocketListener.h PlayerSession.cpp PlayerSession.h)

add_executable(player ${PLAYER_SOURCE_FILES})
add_executable(master ${MASTER_SOURCE_FILES})

target_link_libraries (player ${Boost_LIBRARIES})
target_link_libraries (master ${Boost_LIBRARIES})