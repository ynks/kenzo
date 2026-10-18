cmake_minimum_required(VERSION 3.24)
project(kenzo VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

find_package(rtxui CONFIG REQUIRED)
find_package(asio CONFIG REQUIRED)
find_package(unofficial-vincentlaucsb-csv-parser CONFIG REQUIRED)

include(cmake/EmbedAssets.cmake)
set(UI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/assets/ui)
kenzo_embed_assets(${CMAKE_CURRENT_BINARY_DIR}/generated/assets_embedded.cpp
    MARKUP ${UI_DIR}/menu.html ${UI_DIR}/main.html ${UI_DIR}/overlays.html ${UI_DIR}/detail.html
    STYLE  ${UI_DIR}/theme.css ${UI_DIR}/base.css ${UI_DIR}/main.css ${UI_DIR}/overlays.css)

file(GLOB_RECURSE KENZO_SOURCES CONFIGURE_DEPENDS src/*.cpp)
list(REMOVE_ITEM KENZO_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp)

add_library(kenzo_core OBJECT ${KENZO_SOURCES} ${CMAKE_CURRENT_BINARY_DIR}/generated/assets_embedded.cpp)
target_include_directories(kenzo_core PUBLIC src)
target_link_libraries(kenzo_core PUBLIC rtxui::rtxui asio::asio unofficial::vincentlaucsb-csv-parser::csv)
target_compile_definitions(kenzo_core PUBLIC ASIO_NO_DEPRECATED ASIO_STANDALONE)

if(MSVC)
    target_compile_options(kenzo_core PUBLIC /W4 /WX /utf-8 /permissive- /Zc:__cplusplus /external:anglebrackets /external:W0 /wd4702)
    target_compile_definitions(kenzo_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS _WIN32_WINNT=0x0A00)
else()
    target_compile_options(kenzo_core PUBLIC -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(kenzo src/main.cpp)
target_link_libraries(kenzo PRIVATE kenzo_core)

add_executable(kenzo_probe EXCLUDE_FROM_ALL tools/probe.cpp)
target_link_libraries(kenzo_probe PRIVATE kenzo_core)

add_executable(kenzo_ui_checks EXCLUDE_FROM_ALL tools/ui_checks.cpp)
target_link_libraries(kenzo_ui_checks PRIVATE kenzo_core)

add_executable(kenzo_animation_checks EXCLUDE_FROM_ALL tools/animation_checks.cpp)
target_link_libraries(kenzo_animation_checks PRIVATE kenzo_core)

add_executable(kenzo_status_checks EXCLUDE_FROM_ALL tools/status_checks.cpp)
target_link_libraries(kenzo_status_checks PRIVATE kenzo_core)

add_executable(kenzo_idle_checks EXCLUDE_FROM_ALL tools/idle_checks.cpp)
target_link_libraries(kenzo_idle_checks PRIVATE kenzo_core)
