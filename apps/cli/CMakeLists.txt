cmake_minimum_required(VERSION 3.20)
project(thermal_cli CXX)

add_executable(thermal_cli main.cpp)
target_link_libraries(thermal_cli PRIVATE thermal_core)
