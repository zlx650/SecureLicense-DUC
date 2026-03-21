cmake_minimum_required(VERSION 3.12)
project(SecureLicenseDUCDemo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(DEFINED ENV{CONDA_PREFIX})
    list(APPEND CMAKE_PREFIX_PATH "$ENV{CONDA_PREFIX}")
endif()

find_package(SQLite3 REQUIRED)

add_library(duccore
    src/config.cpp
    src/common.cpp
    src/http.cpp
    src/log.cpp
    src/storage.cpp
)

target_include_directories(duccore PUBLIC include)

add_executable(license_server src/license_server_main.cpp)
target_link_libraries(license_server PRIVATE duccore SQLite::SQLite3)

add_executable(license_client src/license_client_main.cpp)
target_link_libraries(license_client PRIVATE duccore)

add_executable(duccore_tests tests/test_duccore.cpp)
target_link_libraries(duccore_tests PRIVATE duccore SQLite::SQLite3)

enable_testing()
add_test(NAME duccore_tests COMMAND duccore_tests)
