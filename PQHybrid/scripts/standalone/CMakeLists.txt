cmake_minimum_required(VERSION 3.20)
project(PQCUDAStandalone LANGUAGES C CXX CUDA)
set(CMAKE_CUDA_SEPARABLE_COMPILATION ON)
set(CMAKE_CUDA_STANDARD 17)
get_filename_component(REPO "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
add_subdirectory("${REPO}/ML-DSA/cuDilithium" dilithium)
set(KYBER "${REPO}/ML-KEM/Kyber1024/Kyber_GPU_Batched")
add_executable(bench_kyber_original
    "${KYBER}/main.cu" "${KYBER}/rng.cu" "${KYBER}/cbd.cu"
    "${KYBER}/indcpa.cu" "${KYBER}/ntt.cu" "${KYBER}/poly.cu"
    "${KYBER}/polyvec.cu" "${KYBER}/reduce.cu" "${KYBER}/symmetric-fips202.cu")
target_include_directories(bench_kyber_original PRIVATE "${KYBER}")
target_link_libraries(bench_kyber_original PRIVATE pqcuda_fips202_cuda_thread)
