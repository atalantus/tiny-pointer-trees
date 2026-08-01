# Offline check that a DerefTable hash / reduction variant keeps bucket loads
# below ENTRIES_PER_BIN_COUNT at the target load factor. Depends only on
# headers, so it needs no linking beyond the source include path.
add_executable(hash_distribution_sim hash_distribution_sim.cpp)

target_include_directories(hash_distribution_sim PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)
