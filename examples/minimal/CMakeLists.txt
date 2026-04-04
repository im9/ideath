cmake_minimum_required(VERSION 3.22)
project(ideath_minimal_example LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

# Option A: consume ideath via add_subdirectory (source dependency)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../.. ideath_build)

# Option B: consume ideath via find_package (after install)
# find_package(ideath CONFIG REQUIRED)

add_executable(minimal main.cpp)
target_link_libraries(minimal PRIVATE ideath::ideath)
