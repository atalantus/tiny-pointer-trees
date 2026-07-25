cmake_minimum_required(VERSION 3.26)

project(tiny-pointer-trees VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_options(
        -fPIC
        -fasynchronous-unwind-tables
)

if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options(-Wno-stringop-overflow)
endif()

set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DDEBUG -O0 -g")

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(
            -O3
            -march=native
            -fomit-frame-pointer
            -ffast-math
    )

    # Enable LTO / IPO safely
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ipo_supported)
    if(ipo_supported)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
    endif()
endif()

enable_testing()

include(FetchContent)
FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.17.0
)
FetchContent_MakeAvailable(googletest)
include(GoogleTest)

add_library(tiny_pointer_trees STATIC
        src/trie/tiny_trie.cpp
        src/trie/tiny_trie_2.cpp
)

target_include_directories(tiny_pointer_trees PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_include_directories(tiny_pointer_trees PRIVATE
        $<TARGET_PROPERTY:GTest::gtest,INTERFACE_INCLUDE_DIRECTORIES>
)

add_subdirectory(src/aho_corasick)
add_subdirectory(src/art)

add_subdirectory(test)
