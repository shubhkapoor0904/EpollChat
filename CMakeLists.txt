cmake_minimum_required(VERSION 3.14)
project(EpollChatServer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-O3 -DNDEBUG -flto -Wall -Wextra -Wpedantic -pthread)
    add_link_options(-flto)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -pthread)
endif()

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# Core Server Executable
set(SERVER_SOURCES
    src/Logger.cpp
    src/Protocol.cpp
    src/ThreadPool.cpp
    src/ClientConnection.cpp
    src/Server.cpp
    src/main.cpp
)

add_executable(chat_server ${SERVER_SOURCES})
target_link_libraries(chat_server PRIVATE pthread)

# Minimal CLI Client Executable
add_executable(cli_client client/cli_client.cpp src/Protocol.cpp)
target_link_libraries(cli_client PRIVATE pthread)

# Unit Testing setup using Catch2 (FetchContent)
include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.4.0
)
FetchContent_MakeAvailable(Catch2)

add_executable(unit_tests tests/test_protocol.cpp src/Protocol.cpp)
target_link_libraries(unit_tests PRIVATE Catch2::Catch2WithMain)

enable_testing()
add_test(NAME ProtocolTests COMMAND unit_tests)
