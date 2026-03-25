cmake_minimum_required(VERSION 3.20)
project(spsc_ringbuffer LANGUAGES CXX)

add_executable(spsc_ringbuffer
  src/main.cpp
)

target_include_directories(spsc_ringbuffer PRIVATE include)
target_compile_features(spsc_ringbuffer PRIVATE cxx_std_20)

