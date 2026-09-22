# 공통 구현 통합 후 KAT 검증 결과

2026-09-11, NVIDIA GeForce GTX 1070, CUDA 12.6, architecture 61.

| 검사 | 결과 | 범위 |
|---|---|---|
| Kyber1024 round2 참조 KAT | PASS | 독립 참조 벡터 4개를 배치 1/4/33으로 검사, block=32 |
| Dilithium-2 | PASS | 기존 기대 SHA256과 10,000개 m/pk/sk/sig 벡터 일치 |
| Dilithium-3 | PASS | 동일 |
| Dilithium-5 | PASS | 동일 |
| common CPU/GPU SHA3/SHAKE | PASS | 저장된 독립 벡터와 CPU/thread/warp 결과 비교 |
| 공통 OS 난수 API | PASS | 기본 API 동작 검사, 결정적 KAT 아님 |
| PQHybrid 전체 CTest | 7/7 PASS | 최종 배치 33 추가 전 전체 테스트; 추가 후 kyber_kat 별도 PASS |

- `before_fix.log`: 처음 발견한 Kyber KAT 실패, 왕복 테스트는 통과했음.
- `after_fix.log`: 수정 후 전체 CTest 결과.
- `kyber_final.log`: 배치 33까지 확장한 최종 Kyber KAT.
- `dilithium.log`: Dilithium 2/3/5 기존 기준 해시 테스트.
- 기준 벡터 출처 및 재현 방법: `../../tests/kat/README.md`.

## 발견 및 수정한 Kyber 오류

1. Encaps/Decaps의 공유 비밀 KDF에 `G(m || H(pk))`의 첫 32바이트를
   보존하지 않고 메시지 m을 넣고 있었음. 양쪽에 동일한 오류가 있어 왕복 검증만으로는
   발견되지 않았음. prekey를 별도 보존하여 참조 구현과 일치시킴.
2. 키 생성 SHA3-512가 같은 버퍼에 32바이트 입력을 64바이트 출력으로 확장하여
   배치 항목 간 메모리 중첩이 있었음. 후속 커널은 시드를 32바이트 간격으로 읽지만
   기존 출력은 64바이트 간격이었음. `keypair_seed_n`에서 common의 SHA3-512를
   호출하고, 입력과 분리된 `ps->seed`에 공개/노이즈 시드 배열을 각각 연속 저장함.
   커널 이름 조회 및 최대 블록 크기 조회도 새 커널을 가리키도록 갱신함.

공통 해시 구현 자체는 이번 작업에서 수정하지 않았음. 이 오류들이 common 통합에서
발생했다고 단정할 수 없음. 기존 성능 CSV/PNG 및 프로필은 수정 전 결과이며,
수정된 코드의 성능을 나타내려면 재측정이 필요함. 기존 결과 파일은 보존함.

## 적용 범위의 한계

Kyber는 공식 pq-crystals round2 참조 소스로 생성한 결정적 벡터이며 NIST .rsp를
그대로 실행한 것은 아님. Dilithium도 기존 cuDilithium 벡터 검증이며,
최종 FIPS 203/204 적합성 인증이나 모든 커널 variant 검증을 의미하지 않음.

Kyber KAT의 고정 난수는 테스트 실행 파일에만 링크됨. 실제 라이브러리의 common
OS 난수 구현은 그대로 유지됨. Dilithium 키 생성은 아직 카운터 기반 SHAKE128
테스트 시드를 사용하며 common OS RNG를 호출하지 않음. 따라서 Dilithium 결과를
'공통 OS 난수까지 통합된 전체 KAT 통과'라고 표현하면 안 됨.
