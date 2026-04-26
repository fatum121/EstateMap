cmake_minimum_required(VERSION 3.10)

project(EstateMap)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)

pkg_check_modules(CURL REQUIRED libcurl)
pkg_check_modules(GDAL REQUIRED gdal)

add_definitions(-DPROJECT_ROOT="${CMAKE_SOURCE_DIR}")

# ─────────────────────────────────────────────
# Target 1: Downloader (main.cpp)
# ─────────────────────────────────────────────
add_executable(EstateMap src/main.cpp)

target_include_directories(EstateMap PRIVATE
        ${GDAL_INCLUDE_DIRS}
        ${CURL_INCLUDE_DIRS}
)

target_link_libraries(EstateMap PRIVATE
        ${GDAL_LIBRARIES}
        ${CURL_LIBRARIES}
        Threads::Threads
)

target_compile_options(EstateMap PRIVATE -Wall -Wextra)

# ─────────────────────────────────────────────
# Target 2: Analysis (analysis.cpp)
# ─────────────────────────────────────────────
# Tylko STL — bez GDAL i curl
add_executable(Analysis src/analysis.cpp)

target_compile_options(Analysis PRIVATE -Wall -Wextra)

# Upewnij się, że katalog output istnieje
add_custom_command(TARGET Analysis POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/output
        COMMENT "Tworzenie katalogu output/"
)