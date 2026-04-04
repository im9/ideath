cmake_minimum_required(VERSION 3.22)

project(ideath_package_consumer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(ideath CONFIG REQUIRED)

add_executable(ideath_package_smoke main.cpp)
target_link_libraries(ideath_package_smoke PRIVATE ideath::ideath)
