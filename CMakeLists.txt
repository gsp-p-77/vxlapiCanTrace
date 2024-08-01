cmake_minimum_required(VERSION 3.12)
project(vxlapiCanTrace VERSION 1.0.0)

add_executable(vxlapiCanTrace vxlapiCanTrace.cpp)
target_link_libraries(vxlapiCanTrace ../vxlapi64)
