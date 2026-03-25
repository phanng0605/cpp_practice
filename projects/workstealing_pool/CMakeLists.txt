cmake_minimum_required(VERSION 3.20)
project(workstealing_pool LANGUAGES CXX)

add_executable(workstealing_pool
  src/main.cpp
)

target_include_directories(workstealing_pool PRIVATE include)
target_compile_features(workstealing_pool PRIVATE cxx_std_20)

