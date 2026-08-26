cmake_minimum_required(VERSION 3.20)

project(PQHybrid LANGUAGES C CXX CUDA)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CUDA_SEPARABLE_COMPILATION ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(OpenMP REQUIRED)

set(FRODO_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../ML-KEM/FrodoKEM/FrodoKEM_GPU")
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

add_library(frodo_gpu STATIC
    "${FRODO_DIR}/frodo976.cu"
    "${FRODO_DIR}/frodo_macrify_reference.cu"
    "${FRODO_DIR}/fips202.cu"
    "${FRODO_DIR}/random.cu"
    "${FRODO_DIR}/util.cu"
)

target_include_directories(frodo_gpu PRIVATE "${FRODO_DIR}")

target_compile_definitions(frodo_gpu PRIVATE
    shake128=frodo_shake128
    shake128_absorb=frodo_shake128_absorb
    shake128_squeezeblocks=frodo_shake128_squeezeblocks
    shake256=frodo_shake256
    shake256_absorb=frodo_shake256_absorb
    shake256_squeezeblocks=frodo_shake256_squeezeblocks
)

target_link_libraries(frodo_gpu PRIVATE curand)

set_target_properties(frodo_gpu PROPERTIES
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

add_library(pqcuda SHARED
    src/frodo_wrapper.cu
    src/dilithium_dispatch.cpp
    src/hybrid.cu
    $<TARGET_OBJECTS:dilithium_wrapper2>
    $<TARGET_OBJECTS:dilithium_wrapper3>
    $<TARGET_OBJECTS:dilithium_wrapper5>
)

target_include_directories(pqcuda
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE
        "${FRODO_DIR}"
        "${DILITHIUM_DIR}/include"
)

target_link_libraries(pqcuda PRIVATE
    frodo_gpu
    cuDilithium2
    cuDilithium3
    cuDilithium5
    fips202
    curand
    OpenMP::OpenMP_CXX
)

set_target_properties(pqcuda PROPERTIES
    CUDA_SEPARABLE_COMPILATION ON
    CUDA_RESOLVE_DEVICE_SYMBOLS ON
    POSITION_INDEPENDENT_CODE ON
)

add_executable(pqcuda_test src/main.cu)
target_include_directories(pqcuda_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(pqcuda_test PRIVATE pqcuda)
