cmake_minimum_required(VERSION 3.16)
project(ChessEngine LANGUAGES CXX)

# Only affects single-config generators (Makefiles, plain Ninja); multi-config
# generators (Visual Studio, Ninja Multi-Config) ignore this and pick the
# config via --config Debug/Release at build time instead.
if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "RelWithDebInfo" "MinSizeRel")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (MSVC)
    add_compile_options(/W4 /permissive-)

    # Optimize Release builds (/O2 /DNDEBUG are CMake defaults already;
    # /GL + /LTCG add whole-program/cross-TU inlining, matching GCC LTO)
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /GL")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /LTCG")
    set(CMAKE_STATIC_LINKER_FLAGS_RELEASE "${CMAKE_STATIC_LINKER_FLAGS_RELEASE} /LTCG")
else()
    # Warnings
    add_compile_options(-Wall -Wextra -Wpedantic)

    # Optimize Release builds
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -DNDEBUG")
endif()

# 1) Engine core library
add_library(engine_core
    src/engine/board.cpp
    src/engine/movegen.cpp
    src/engine/bitboards.cpp
    src/engine/hashing.cpp
    src/engine/eval.cpp
    src/engine/engine.cpp
    src/engine/uci.cpp
    src/engine/threads.cpp
    src/engine/search.cpp
    src/engine/moveselector.cpp
    src/engine/transpostable.cpp
)

target_include_directories(engine_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# 2) UCI engine executable
add_executable(uci_engine
    apps/uci_main.cpp
)

target_link_libraries(uci_engine
    PRIVATE engine_core
)

# 3) Tools / CLI executable (perft, bench, verify...)
add_executable(engine_tools
    apps/tools_main.cpp
    tools/perft.cpp
    tools/verification.cpp
    tools/unit_tests.cpp
    tools/magicGen.cpp
)

target_include_directories(engine_tools
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tools
)

target_link_libraries(engine_tools
    PRIVATE engine_core
)
