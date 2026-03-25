cmake_minimum_required(VERSION 3.20)
project(orderbook_implementation LANGUAGES CXX)

add_executable(orderbook_implementation
  src/OrderBook.cpp
  src/main.cpp
)

target_include_directories(orderbook_implementation PRIVATE include)
target_compile_features(orderbook_implementation PRIVATE cxx_std_20)

