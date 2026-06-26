# ═══════════════════════════════════════════════════════════════════════════════
#  ApexMatch — High-Concurrency Order Matching Engine
#  CMake Build Configuration
#  Author: Abhijeet Senapati
# ═══════════════════════════════════════════════════════════════════════════════

cmake_minimum_required(VERSION 3.16)

project(ApexMatch
    VERSION     1.0.0
    DESCRIPTION "High-Concurrency Order Matching Engine in C++17"
    LANGUAGES   CXX
)

# ── C++17 Standard ────────────────────────────────────────────────────────────

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Source & Header Files ─────────────────────────────────────────────────────

set(SOURCES
    src/OrderBook.cpp
    src/ThreadPool.cpp
    src/Server.cpp
    src/main.cpp
)

set(HEADERS
    include/Order.h
    include/OrderBook.h
    include/ThreadPool.h
    include/Server.h
)

# ── Executable Target ────────────────────────────────────────────────────────

add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

target_include_directories(${PROJECT_NAME}
    PRIVATE ${CMAKE_SOURCE_DIR}/include
)

# ── Threading Support (all platforms) ─────────────────────────────────────────

find_package(Threads REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE Threads::Threads)

# ── Platform-Specific Linking ─────────────────────────────────────────────────

if(WIN32)
    # Link Winsock2 for TCP socket support.
    target_link_libraries(${PROJECT_NAME} PRIVATE ws2_32)
endif()

# ── Compiler Warnings ────────────────────────────────────────────────────────

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE
        /W4             # Warning level 4
        /permissive-    # Strict conformance mode
        /utf-8          # UTF-8 source encoding (for Unicode box-drawing chars)
    )
else()
    target_compile_options(${PROJECT_NAME} PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wconversion
    )
endif()

# ── Build Type Defaults ──────────────────────────────────────────────────────

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

message(STATUS "")
message(STATUS "╔══════════════════════════════════════════════╗")
message(STATUS "║  ApexMatch v${PROJECT_VERSION}                           ║")
message(STATUS "║  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "║  Compiler:   ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "║  Platform:   ${CMAKE_SYSTEM_NAME}")
message(STATUS "╚══════════════════════════════════════════════╝")
message(STATUS "")
