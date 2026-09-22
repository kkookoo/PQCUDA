#include "pqcuda.h"
#include <cuda_runtime.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>
static size_t kat_batch, kat_base, entropy_call;
// Test executable only: production pqcuda_random is not linked into this target.
extern "C" int pqcuda_random_bytes(uint8_t *out, size_t size) {
    const size_t expected = kat_batch * (entropy_call == 1 ? 64 : 32);
    if (entropy_call > 2 || size != expected) return 0;
    std::memset(out,0,size);
    const size_t role = entropy_call == 0 ? 1 : entropy_call == 1 ? 0 : 2;
    for (size_t n=0;n<kat_batch;++n)
        for (size_t i=0;i<32;++i)
            out[n*32+i]=static_cast<uint8_t>(((kat_base+n)%4)*53 + role*71+i);
    ++entropy_call;
    return 1;
}
int main(int argc, char **argv) {
    int devices=0;
    if(cudaGetDeviceCount(&devices)!=cudaSuccess || !devices) return 77;
    if(argc!=2) return 1;
    std::ifstream file(argv[1],std::ios::binary);
    constexpr size_t record=1568+3168+1568+32+32;
    std::vector<uint8_t> expected(record*4);
    if(!file.read(reinterpret_cast<char*>(expected.data()),expected.size()) || file.peek()!=EOF) return 1;
    std::vector<size_t> blocks(pqcuda_kyber1024_tuned_kernel_count(),32);
    if(pqcuda_kyber1024_apply_launch_profile(blocks.data(),blocks.size())) return 1;
    int failures=0;
    for(size_t batch : {size_t(1),size_t(4),size_t(33)}) {
        kat_batch=batch; kat_base=0; entropy_call=0;
        std::vector<uint8_t> pk(1568*batch),sk(3168*batch),ct(1568*batch),ss(32*batch),dec(ss.size());
        auto check=[&](const char*label,const std::vector<uint8_t>&data,size_t bytes,size_t offset){
            for(size_t n=0;n<batch;++n) {
                if(std::memcmp(data.data()+n*bytes,expected.data()+(n%4)*record+offset,bytes)) {
                    std::fprintf(stderr,"FAIL batch=%zu item=%zu %s\n",batch,n,label); ++failures;
                }
            }
        };
        if(pqcuda_kyber1024_keypair_batch(pk.data(),sk.data(),batch)) return 1;
        check("pk",pk,1568,0); check("sk",sk,3168,1568);
        if(pqcuda_kyber1024_encapsulate_batch(ct.data(),ss.data(),pk.data(),batch)) return 1;
        check("ct",ct,1568,1568+3168); check("ss",ss,32,1568+3168+1568);
        if(pqcuda_kyber1024_decapsulate_batch(dec.data(),ct.data(),sk.data(),batch)) return 1;
        check("decaps",dec,32,1568+3168+1568);
        for(size_t n=0;n<batch;++n) ct[n*1568]^=1;
        if(pqcuda_kyber1024_decapsulate_batch(dec.data(),ct.data(),sk.data(),batch)) return 1;
        check("rejected ciphertext secret",dec,32,record-32);
        if(entropy_call!=3) return 1;
    }
    if(failures) return 1;
    std::puts("PASS: Kyber1024 round2 reference KAT, batches 1/4/33, pk/sk/ct/ss/decaps/rejection");
    return 0;
}
