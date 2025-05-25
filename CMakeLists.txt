cmake_minimum_required(VERSION 3.10)
project(SimpleTest LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

add_executable(MySimpleApp main.cpp)

message(STATUS "Simple CMakeLists.txt configured.")