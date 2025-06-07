cmake_minimum_required(VERSION 3.31)
project(MemoryAdapter C)

set(CMAKE_C_STANDARD 11)

add_executable(MemoryAdapter main.c mem_adapter.c)
