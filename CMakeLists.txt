cmake_minimum_required(VERSION 3.10)
project(EstateMap)

set(CMAKE_CXX_STANDARD 11)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GDAL REQUIRED gdal)

add_executable(EstateMap src/main.cpp
        src/zabawa.cpp)

target_include_directories(EstateMap PRIVATE ${GDAL_INCLUDE_DIRS})
target_link_libraries(EstateMap ${GDAL_LIBRARIES})