# Keep the standalone cuDilithium target name while sharing the implementation.
if(NOT TARGET pqcuda_fips202_cpu)
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../../../common"
                     "${CMAKE_CURRENT_BINARY_DIR}/pqcuda_common")
endif()
add_library(fips202 INTERFACE)
target_link_libraries(fips202 INTERFACE pqcuda_fips202_cpu pqcuda_fips202_cuda_warp)
