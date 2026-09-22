# KAT provenance and scope

## Kyber1024 round2

`kyber1024_round2.bin` contains four deterministic reference records, each:
`pk[1568] || sk[3168] || ct[1568] || ss[32] || rejected_ct_ss[32]`.
The rejected ciphertext is the valid ciphertext with bit 0 of byte 0 flipped.

Reference: https://github.com/pq-crystals/kyber/tree/a2d1fdb22e14e49f47f896c7952cac37126da25f/ref
(branch round2, KYBER_K=4). This is a reference-derived KAT, not a NIST .rsp
file or a FIPS 203 / ML-KEM conformance claim. SHA3/SHAKE and KEM arithmetic
in the generator come from the independent reference, not this repository's
common implementation.

Entropy byte i for vector v and request r is `(v*53 + r*71 + i) mod 256`.
Reference requests: r=0 key seed, r=1 rejection secret z, r=2 encapsulation
entropy, each 32 bytes. The test maps these semantic inputs to the wrapper's
allocation/call order (z, key seeds with padding, encapsulation entropy).
Fixed entropy is linked only into the test executable; production RNG code
and production library linkage are unchanged.

SHA256 of the committed binary:
`89cbaa516c6e08ff4614f09d38c0f7b586500ec502d8a2ab0662a08bd90364ad`

Regeneration (reference checkout must match the commit above):

```bash
cc -O2 -DKYBER_K=4 -I /tmp/pqcuda-kyber-round2-reference/ref \
  PQHybrid/tests/kat/generate_kyber_round2.c \
  /tmp/pqcuda-kyber-round2-reference/ref/{kem,indcpa,polyvec,poly,ntt,cbd,reduce,verify,fips202,symmetric-fips202}.c \
  -o /tmp/pqcuda-generate-kyber-kat
/tmp/pqcuda-generate-kyber-kat > PQHybrid/tests/kat/kyber1024_round2.bin
```

Run with `ctest --test-dir PQHybrid/build-cuda126 -R kyber_kat --output-on-failure`.
The test checks exact pk/sk/ct/ss, valid decapsulation and rejection output
for batches 1, 4 and 33 (repeating the four reference records), with 32-thread
blocks to include a partial final block. A missing GPU is reported as skipped, not passed.

## Dilithium 2/3/5

The existing `ML-DSA/cuDilithium/tests/test_vectors.cu` generates 10,000
vectors per mode; its CMake tests compare the complete generated m/pk/sk/sig
file hash against the pre-existing expected hashes. These expectations were
not changed for this work. Build and run:

```bash
cmake --build PQHybrid/build-standalone-cuda126 \
  --target test_cuDilithium2 test_cuDilithium3 test_cuDilithium5 -j 4
ctest --test-dir PQHybrid/build-standalone-cuda126/dilithium \
  -R '^test_cuDilithium' --output-on-failure
```

These tests cover the legacy cuDilithium direct API using common CPU/warp
SHAKE; they do not establish FIPS 204 conformance or test all runtime variants.
Dilithium keypair still derives test seeds from a counter with SHAKE128 in
`src/keypair.cu`; it does not consume the common OS RNG. PQHybrid wrapper
roundtrips are tested separately by `common_crypto_roundtrip`.

## Shared primitives and OS randomness

`fips202_cpu` and `fips202_cuda` compare SHA3/SHAKE against stored independent
hashlib-derived vectors (CPU, thread and warp paths). `platform` checks the
common OS RNG's basic API behavior, zero-length/invalid-pointer handling and
nonconstant outputs. This is not a deterministic KAT or statistical quality
certification of the OS RNG.
