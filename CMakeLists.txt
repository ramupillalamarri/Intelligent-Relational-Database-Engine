cmake_minimum_required(VERSION 3.10)
project(DBForge CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add source files
file(GLOB_RECURSE SOURCES "src/*.cpp")

add_executable(dbforge ${SOURCES})

# Include directories
target_include_directories(dbforge PRIVATE src)
