cmake_minimum_required(VERSION 3.20)

project(PQHybrid LANGUAGES C CXX CUDA)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CUDA_SEPARABLE_COMPILATION ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(OpenMP REQUIRED)

set(KYBER_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../ML-KEM/Kyber1024/Kyber_GPU_Batched")
set(DILITHIUM_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../ML-DSA/cuDilithium")

include_directories("${DILITHIUM_DIR}/include")

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
    "${KYBER_DIR}/fips202.cu"
    "${KYBER_DIR}/indcpa.cu"
    "${KYBER_DIR}/ntt.cu"
    "${KYBER_DIR}/poly.cu"
    "${KYBER_DIR}/polyvec.cu"
    "${KYBER_DIR}/reduce.cu"
    "${KYBER_DIR}/symmetric-fips202.cu"
)

target_include_directories(kyber_gpu PRIVATE "${KYBER_DIR}")
target_compile_definitions(kyber_gpu PRIVATE
    KeccakF_RoundConstants=kyber_KeccakF_RoundConstants
)

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
        "crypto_sign_keypair=dilithium${mode}_crypto_sign_keypair"
        "crypto_sign_signature=dilithium${mode}_crypto_sign_signature"
        "crypto_sign_verify=dilithium${mode}_crypto_sign_verify"
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
    src/hybrid.cu
    $<TARGET_OBJECTS:dilithium_wrapper2>
    $<TARGET_OBJECTS:dilithium_wrapper3>
    $<TARGET_OBJECTS:dilithium_wrapper5>
)

add_library(pqcuda SHARED ${PQCUDA_SOURCES})
add_library(pqcuda_static STATIC ${PQCUDA_SOURCES})
set_target_properties(pqcuda_static PROPERTIES OUTPUT_NAME pqcuda)

foreach(target IN ITEMS pqcuda pqcuda_static)
    target_include_directories(${target}
        PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
        PRIVATE
            "${KYBER_DIR}"
            "${DILITHIUM_DIR}/include"
    )

    target_link_libraries(${target} PRIVATE
        kyber_gpu
        cuDilithium2
        cuDilithium3
        cuDilithium5
        fips202
        curand
        OpenMP::OpenMP_CXX
    )

    set_target_properties(${target} PROPERTIES
        CUDA_SEPARABLE_COMPILATION ON
        POSITION_INDEPENDENT_CODE ON
    )
endforeach()

set_target_properties(pqcuda PROPERTIES CUDA_RESOLVE_DEVICE_SYMBOLS ON)
set_target_properties(pqcuda_static PROPERTIES CUDA_RESOLVE_DEVICE_SYMBOLS OFF)

add_executable(pqcuda_test src/main.cpp)
target_include_directories(pqcuda_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(pqcuda_test PRIVATE pqcuda)

add_executable(pqcuda_test_static src/main.cpp)
target_include_directories(pqcuda_test_static PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(pqcuda_test_static PRIVATE pqcuda_static)
