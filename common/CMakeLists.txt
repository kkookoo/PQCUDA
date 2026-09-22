include(GNUInstallDirs)

# Shared primitives; algorithm-specific arithmetic stays in its own library.
add_library(pqcuda_fips202_cpu STATIC fips202_cpu/fips202.c)
add_library(pqcuda_fips202_cuda_thread STATIC fips202_cuda/thread/fips202.cu)
add_library(pqcuda_fips202_cuda_warp STATIC fips202_cuda/warp/fips202.cu)
add_library(pqcuda_random STATIC randombytes/randombytes.c)
foreach(target IN ITEMS pqcuda_fips202_cpu pqcuda_fips202_cuda_thread
                        pqcuda_fips202_cuda_warp pqcuda_random)
    target_include_directories(${target} PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/pqcuda_common>")
    set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
endforeach()
foreach(target IN ITEMS pqcuda_fips202_cuda_thread pqcuda_fips202_cuda_warp)
    set_target_properties(${target} PROPERTIES
        CUDA_SEPARABLE_COMPILATION ON CUDA_STANDARD 17)
endforeach()
if(WIN32)
    target_link_libraries(pqcuda_random PRIVATE bcrypt)
endif()
