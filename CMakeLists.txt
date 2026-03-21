cmake_minimum_required(VERSION 3.12)
project(SecureLicenseDUCDemo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(duccore
    src/common.cpp
    src/http.cpp
)

target_include_directories(duccore PUBLIC include)

add_executable(license_server src/license_server_main.cpp)
target_link_libraries(license_server PRIVATE duccore)

add_executable(license_client src/license_client_main.cpp)
target_link_libraries(license_client PRIVATE duccore)
