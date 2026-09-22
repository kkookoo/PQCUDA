/* Compile against unmodified pq-crystals/kyber round2 ref sources, KYBER_K=4.
 * Deterministic entropy is for this KAT generator only. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "api.h"
#include "params.h"
static unsigned vector_id, call;
void randombytes(unsigned char *out, size_t size) {
    if (size != 32 || call >= 3) abort();
    for (size_t i=0; i<size; ++i) out[i]=(unsigned char)(vector_id*53 + call*71 + i);
    ++call;
}
int main(void) {
    unsigned char pk[KYBER_PUBLICKEYBYTES], sk[KYBER_SECRETKEYBYTES];
    unsigned char ct[KYBER_CIPHERTEXTBYTES], ss[32], recovered[32];
    for (vector_id=0; vector_id<4; ++vector_id) {
        call=0;
        crypto_kem_keypair(pk,sk);
        crypto_kem_enc(ct,ss,pk);
        crypto_kem_dec(recovered,ct,sk);
        for (int i=0;i<32;++i) if(ss[i]!=recovered[i]) return 1;
        fwrite(pk,1,sizeof(pk),stdout); fwrite(sk,1,sizeof(sk),stdout);
        fwrite(ct,1,sizeof(ct),stdout); fwrite(ss,1,32,stdout);
        ct[0]^=1;
        crypto_kem_dec(recovered,ct,sk);
        fwrite(recovered,1,32,stdout);
    }
    return ferror(stdout) ? 1 : 0;
}
