cmake_minimum_required(VERSION 3.20)

project(PQHybrid LANGUAGES C CXX CUDA)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CUDA_SEPARABLE_COMPILATION ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(CUDAToolkit REQUIRED)
find_package(OpenMP REQUIRED COMPONENTS CXX)
include(GNUInstallDirs)
include(CTest)

if(WIN32)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
if(MSVC)
    # The CLI contains Korean text; use UTF-8 for both source and literals.
    add_compile_options(
        "$<$<COMPILE_LANGUAGE:C,CXX>:/utf-8>"
        "$<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=/utf-8>"
    )
    if(CMAKE_CUDA_COMPILER_VERSION VERSION_GREATER_EQUAL 13.3)
        # CUDA 13.3's CCCL headers require MSVC's conforming preprocessor.
        add_compile_options(
            "$<$<COMPILE_LANGUAGE:CXX>:/Zc:preprocessor>"
            "$<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=/Zc:preprocessor>"
        )
    endif()
endif()

set(KYBER_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../ML-KEM/Kyber1024/Kyber_GPU_Batched")
set(DILITHIUM_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../ML-DSA/cuDilithium")

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../common"
                 "${CMAKE_CURRENT_BINARY_DIR}/common")

include_directories("${DILITHIUM_DIR}/include")

# These dependencies are embedded in the DLL and linked with the static API.
set(BUILD_SHARED_LIBS OFF)
add_subdirectory(
    "${DILITHIUM_DIR}/src"
    "${CMAKE_CURRENT_BINARY_DIR}/cuDilithium"
)

# cuDilithium builds each parameter set from the same sources and therefore
# gives all three archives identical symbols.  Give every externally linked
# CUDA function a mode-specific name so the parameter sets can coexist in one
# final library.
set(DILITHIUM_GLOBAL_SYMBOLS
    crypto_sign_keypair crypto_sign_signature crypto_sign_verify
    crypto_sign_set_kernel_variant crypto_sign_get_kernel_variant
    crypto_sign_kernel_variant_count crypto_sign_tuning_stage_name
    crypto_sign_kernel_variant_name crypto_sign_tuning_begin
    crypto_sign_tuning_end
    gpu_keypair gpu_verify ntt_inner invntt_inner c_zetas
    polyvec_matrix_expand_kernel polyvec_matrix_expand_opt_kernel
    unpack_fuse_ntt_kernel unpack_fuse_ntt_radix2_kernel
    unpack_fuse_ntt_radix2_opt_kernel compute_y_kernel
    compute_y_opt_kernel compute_w_32t_kernel compute_w_128t_kernel
    compute_cp_kernel compute_cp_opt_kernel rej_loop_32t_kernel
    rej_loop_128t_kernel sig_copy_kernel
)

foreach(mode IN ITEMS 2 3 5)
    foreach(symbol IN LISTS DILITHIUM_GLOBAL_SYMBOLS)
        target_compile_definitions(cuDilithium${mode} PRIVATE
            "${symbol}=dilithium${mode}_${symbol}")
    endforeach()
endforeach()

add_library(kyber_gpu STATIC
    "${KYBER_DIR}/cbd.cu"
    "${KYBER_DIR}/indcpa.cu"
    "${KYBER_DIR}/ntt.cu"
    "${KYBER_DIR}/poly.cu"
    "${KYBER_DIR}/polyvec.cu"
    "${KYBER_DIR}/reduce.cu"
    "${KYBER_DIR}/symmetric-fips202.cu"
)

target_include_directories(kyber_gpu PRIVATE "${KYBER_DIR}")
target_link_libraries(kyber_gpu PUBLIC pqcuda_fips202_cuda_thread)

set_target_properties(kyber_gpu PROPERTIES
    CUDA_SEPARABLE_COMPILATION ON
    POSITION_INDEPENDENT_CODE ON
)

add_library(dilithium_wrapper2 OBJECT src/dilithium_wrapper.cu)
add_library(dilithium_wrapper3 OBJECT src/dilithium_wrapper.cu)
add_library(dilithium_wrapper5 OBJECT src/dilithium_wrapper.cu)

foreach(mode IN ITEMS 2 3 5)
    target_compile_definitions(dilithium_wrapper${mode} PRIVATE
        DILITHIUM_MODE=${mode}
        "PQC_DILITHIUM_ENTRY=pqcuda_dilithium${mode}_sign_verify"
        "PQC_DILITHIUM_KEYPAIR_ENTRY=pqcuda_dilithium${mode}_keypair"
        "PQC_DILITHIUM_SIGN_ENTRY=pqcuda_dilithium${mode}_sign"
        "PQC_DILITHIUM_VERIFY_ENTRY=pqcuda_dilithium${mode}_verify"
        "PQC_DILITHIUM_KEYPAIR_BATCH_ENTRY=pqcuda_dilithium${mode}_keypair_batch"
        "PQC_DILITHIUM_SIGN_BATCH_ENTRY=pqcuda_dilithium${mode}_sign_batch"
        "PQC_DILITHIUM_VERIFY_BATCH_ENTRY=pqcuda_dilithium${mode}_verify_batch"
        "PQC_DILITHIUM_TUNE_ENTRY=pqcuda_dilithium${mode}_tune_sign_kernels"
        "PQC_DILITHIUM_TUNED_STAGE_COUNT_ENTRY=pqcuda_dilithium${mode}_tuned_stage_count"
        "PQC_DILITHIUM_TUNED_STAGE_NAME_ENTRY=pqcuda_dilithium${mode}_tuned_stage_name"
        "PQC_DILITHIUM_TUNED_VARIANT_NAME_ENTRY=pqcuda_dilithium${mode}_tuned_variant_name"
        "crypto_sign_keypair=dilithium${mode}_crypto_sign_keypair"
        "crypto_sign_signature=dilithium${mode}_crypto_sign_signature"
        "crypto_sign_verify=dilithium${mode}_crypto_sign_verify"
        "crypto_sign_set_kernel_variant=dilithium${mode}_crypto_sign_set_kernel_variant"
        "crypto_sign_get_kernel_variant=dilithium${mode}_crypto_sign_get_kernel_variant"
        "crypto_sign_kernel_variant_count=dilithium${mode}_crypto_sign_kernel_variant_count"
        "crypto_sign_tuning_stage_name=dilithium${mode}_crypto_sign_tuning_stage_name"
        "crypto_sign_kernel_variant_name=dilithium${mode}_crypto_sign_kernel_variant_name"
        "crypto_sign_tuning_begin=dilithium${mode}_crypto_sign_tuning_begin"
        "crypto_sign_tuning_end=dilithium${mode}_crypto_sign_tuning_end"
    )
    target_include_directories(dilithium_wrapper${mode} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${DILITHIUM_DIR}/include"
    )
    set_target_properties(dilithium_wrapper${mode} PROPERTIES
        CUDA_SEPARABLE_COMPILATION ON
        POSITION_INDEPENDENT_CODE ON
    )
endforeach()

set(PQCUDA_SOURCES
    src/kyber_wrapper.cu
    src/dilithium_dispatch.cpp
    $<TARGET_OBJECTS:dilithium_wrapper2>
    $<TARGET_OBJECTS:dilithium_wrapper3>
    $<TARGET_OBJECTS:dilithium_wrapper5>
)

add_library(pqcuda SHARED ${PQCUDA_SOURCES})
add_library(pqcuda_static STATIC ${PQCUDA_SOURCES})
target_compile_definitions(pqcuda PRIVATE PQCUDA_BUILD_SHARED)
target_compile_definitions(pqcuda_static PUBLIC PQCUDA_STATIC)
if(WIN32)
    # pqcuda.lib is the DLL import library; keep the static archive distinct.
    set_target_properties(pqcuda_static PROPERTIES OUTPUT_NAME pqcuda_static)
else()
    set_target_properties(pqcuda_static PROPERTIES OUTPUT_NAME pqcuda)
endif()

foreach(target IN ITEMS pqcuda pqcuda_static)
    target_include_directories(${target}
        PUBLIC
            "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
            "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
        PRIVATE
            "${KYBER_DIR}"
            "${DILITHIUM_DIR}/include"
    )

    target_link_libraries(${target} PRIVATE
        kyber_gpu
        cuDilithium2
        cuDilithium3
        cuDilithium5
        pqcuda_fips202_cpu
        pqcuda_fips202_cuda_warp
        pqcuda_random
        CUDA::curand
        OpenMP::OpenMP_CXX
    )
    if(WIN32)
        target_link_libraries(${target} PRIVATE bcrypt)
    endif()

    set_target_properties(${target} PROPERTIES
        CUDA_SEPARABLE_COMPILATION ON
        POSITION_INDEPENDENT_CODE ON
    )
endforeach()

set_target_properties(pqcuda PROPERTIES CUDA_RESOLVE_DEVICE_SYMBOLS ON)
set_target_properties(pqcuda_static PROPERTIES CUDA_RESOLVE_DEVICE_SYMBOLS OFF)

add_executable(pqcuda_test src/main.c src/benchmark_gpu.cu)
target_include_directories(pqcuda_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(pqcuda_test PRIVATE pqcuda)

add_executable(pqcuda_test_static src/main.c src/benchmark_gpu.cu)
target_include_directories(pqcuda_test_static PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(pqcuda_test_static PRIVATE pqcuda_static)

set_target_properties(pqcuda_test pqcuda_test_static PROPERTIES
    LINKER_LANGUAGE CXX
)

if(BUILD_TESTING)
    # Fixed entropy is isolated to this executable; production RNG is unchanged.
    add_executable(pqcuda_kyber_kat_test tests/kyber_kat_test.cpp src/kyber_wrapper.cu src/benchmark_gpu.cu)
    target_include_directories(pqcuda_kyber_kat_test PRIVATE include src "${KYBER_DIR}")
    target_link_libraries(pqcuda_kyber_kat_test PRIVATE kyber_gpu pqcuda_fips202_cpu CUDA::cudart)
    set_target_properties(pqcuda_kyber_kat_test PROPERTIES CUDA_SEPARABLE_COMPILATION ON CUDA_RESOLVE_DEVICE_SYMBOLS ON)
    add_test(NAME kyber_kat COMMAND pqcuda_kyber_kat_test "${CMAKE_CURRENT_SOURCE_DIR}/tests/kat/kyber1024_round2.bin")
    set_tests_properties(kyber_kat PROPERTIES SKIP_RETURN_CODE 77)
    add_executable(pqcuda_common_crypto_roundtrip_test tests/common_crypto_roundtrip_test.cpp)
    target_link_libraries(pqcuda_common_crypto_roundtrip_test PRIVATE pqcuda CUDA::cudart)
    add_test(NAME common_crypto_roundtrip COMMAND pqcuda_common_crypto_roundtrip_test)
    set_tests_properties(common_crypto_roundtrip PROPERTIES SKIP_RETURN_CODE 77)
    add_executable(pqcuda_fips202_cpu_test tests/fips202_cpu_test.c)
    target_link_libraries(pqcuda_fips202_cpu_test PRIVATE pqcuda_fips202_cpu)
    add_test(NAME fips202_cpu COMMAND pqcuda_fips202_cpu_test)
    add_executable(pqcuda_fips202_cuda_test tests/fips202_cuda_test.cu)
    target_link_libraries(pqcuda_fips202_cuda_test PRIVATE
        pqcuda_fips202_cuda_thread pqcuda_fips202_cuda_warp)
    add_test(NAME fips202_cuda COMMAND pqcuda_fips202_cuda_test)
    set_tests_properties(fips202_cuda PROPERTIES SKIP_RETURN_CODE 77)
    add_executable(pqcuda_dilithium_launch_test tests/dilithium_launch_test.cpp)
    target_link_libraries(pqcuda_dilithium_launch_test PRIVATE pqcuda_static)
    set_target_properties(pqcuda_dilithium_launch_test PROPERTIES CUDA_RESOLVE_DEVICE_SYMBOLS ON)
    add_test(NAME dilithium_launch COMMAND pqcuda_dilithium_launch_test)
    add_executable(pqcuda_benchmark_policy_test tests/benchmark_policy_test.c)
    target_include_directories(pqcuda_benchmark_policy_test PRIVATE src)
    add_test(NAME benchmark_policy COMMAND pqcuda_benchmark_policy_test)
    add_executable(pqcuda_platform_test tests/platform_test.c)
    target_link_libraries(pqcuda_platform_test PRIVATE pqcuda_random)
    target_include_directories(pqcuda_platform_test PRIVATE src)
    if(WIN32)
        target_link_libraries(pqcuda_platform_test PRIVATE bcrypt)
    endif()
    add_test(NAME platform COMMAND pqcuda_platform_test)
endif()

install(TARGETS pqcuda pqcuda_static kyber_gpu
        cuDilithium2 cuDilithium3 cuDilithium5 fips202
        pqcuda_fips202_cpu pqcuda_fips202_cuda_thread pqcuda_fips202_cuda_warp pqcuda_random
    EXPORT PQCUDATargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(TARGETS pqcuda_test pqcuda_test_static
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../common/"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/pqcuda_common"
    FILES_MATCHING PATTERN "*.h" PATTERN "*.cuh")
install(FILES include/pqcuda.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT PQCUDATargets NAMESPACE PQCUDA::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/PQCUDA)
include(CMakePackageConfigHelpers)
configure_package_config_file(cmake/PQCUDAConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/PQCUDAConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/PQCUDA)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/PQCUDAConfig.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/PQCUDA)
