cmake_minimum_required(VERSION 3.20)
project(cpp_practices LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(projects/spsc_ringbuffer)
add_subdirectory(projects/workstealing_pool)
add_subdirectory(projects/orderbook_implementation)

