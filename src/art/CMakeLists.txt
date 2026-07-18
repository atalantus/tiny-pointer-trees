add_library(art STATIC
        ARTSynchronized/OptimisticLockCoupling/Tree.cpp
        tiny_art/Tree.cpp
)

option(USE_N64 "Enable AVX-512 N64 nodes in tiny ART" OFF)
if (USE_N64)
    target_compile_definitions(art PUBLIC USE_N64)
endif()

target_include_directories(art PUBLIC
        ${CMAKE_SOURCE_DIR}/src
)

find_library(TbbLib tbb REQUIRED)
find_package(Threads REQUIRED)
target_link_libraries(art PUBLIC ${TbbLib} Threads::Threads)

add_executable(art_example example.cpp)

target_link_libraries(art_example PUBLIC art)