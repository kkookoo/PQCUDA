
// @Author: Arpan Jati
// Adapted from NewHope Reference Codebase and Parallelized using CUDA
// Updated : August 2019

#ifndef INDCPA_H
#define INDCPA_H

#include "main.h"

void HandleError(cudaError_t err, const char* file, int line);

void indcpa_set_launch_config(int grid_size, int block_size);

enum indcpa_kernel_id {
    INDCPA_KERNEL_SHA3_512 = 0,
    INDCPA_KERNEL_GEN_MATRIX,
    INDCPA_KERNEL_POLY_GETNOISE,
    INDCPA_KERNEL_POLYVEC_NTT,
    INDCPA_KERNEL_POLYVEC_POINTWISE_ACC,
    INDCPA_KERNEL_POLY_FROMMONT,
    INDCPA_KERNEL_POLYVEC_ADD,
    INDCPA_KERNEL_POLYVEC_REDUCE,
    INDCPA_KERNEL_PACK_SK,
    INDCPA_KERNEL_PACK_PK,
    INDCPA_KERNEL_UNPACK_PK,
    INDCPA_KERNEL_POLY_FROMMSG,
    INDCPA_KERNEL_POLYVEC_INVNTT,
    INDCPA_KERNEL_POLY_INVNTT,
    INDCPA_KERNEL_POLY_ADD,
    INDCPA_KERNEL_POLY_REDUCE,
    INDCPA_KERNEL_PACK_CIPHERTEXT,
    INDCPA_KERNEL_UNPACK_CIPHERTEXT,
    INDCPA_KERNEL_UNPACK_SK,
    INDCPA_KERNEL_POLY_SUB,
    INDCPA_KERNEL_POLY_TOMSG,
    INDCPA_KERNEL_COUNT
};

int indcpa_tuning_begin(int candidate_block_size);
void indcpa_tuning_end(void);
float indcpa_tuning_average_ms(int kernel_id);
int indcpa_set_kernel_block_size(int kernel_id, int block_size);
int indcpa_get_kernel_block_size(int kernel_id);
const char *indcpa_get_kernel_name(int kernel_id);

#define HANDLE_ERROR( err ) (HandleError( err, __FILE__, __LINE__ ))


void indcpa_keypair(int COUNT, poly_set4* ps, unsigned char *pk,
                    unsigned char *sk, unsigned char* rng_buf, cudaStream_t stream);

void indcpa_enc(int COUNT, poly_set4* ps, unsigned char *c,
                unsigned char *m,
                unsigned char *pk,
                unsigned char *coins, cudaStream_t stream);

void indcpa_dec(int COUNT, poly_set4* ps, unsigned char *m,
                unsigned char *c,
                unsigned char *sk, cudaStream_t stream);

void print_data(const char* text, unsigned char* data, int length);

#endif
