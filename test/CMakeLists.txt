enable_testing()

include(FetchContent)
FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.17.0
)
FetchContent_MakeAvailable(googletest)
include(GoogleTest)

add_executable(tiny_pointer_trees_tests
        test_aho_corasick.cpp
        test_art.cpp
        test_tinyptr.cpp
        test_trie.cpp
)

target_include_directories(tiny_pointer_trees_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(tiny_pointer_trees_tests PRIVATE
        tiny_pointer_trees
        art
        aho_corasick
        GTest::gtest_main
)

find_package(Threads REQUIRED)
target_link_libraries(tiny_pointer_trees_tests PRIVATE Threads::Threads)

gtest_discover_tests(tiny_pointer_trees_tests)
