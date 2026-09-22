# 현재 소스의 독립 실행 벤치마크

PQHybrid C API wrapper를 거치지 않고 기존 `main.cu` 및
`bench_dilithium.cu` 진입점을 실행한 결과입니다. 이 디렉터리를 생성하는 과정에서
알고리즘 및 기존 벤치마크 소스를 변경하지 않았습니다. 단, 해당 소스에는 이 작업
이전부터 수정 사항이 있으므로 미수정 upstream 버전의 결과로 해석하면 안 됩니다.

빌드: CUDA 12.6, Release, CUDA architecture 61.

```bash
cmake -S PQHybrid/scripts/standalone -B PQHybrid/build-standalone-cuda126 \
  -DCMAKE_CUDA_COMPILER=/usr/local/cuda-12.6/bin/nvcc \
  -DCMAKE_CUDA_ARCHITECTURES=61 -DCMAKE_BUILD_TYPE=Release
cmake --build PQHybrid/build-standalone-cuda126 \
  --target bench_kyber_original bench_cuDilithium2 bench_cuDilithium3 bench_cuDilithium5 -j 4
python3 PQHybrid/scripts/standalone/run.py
```

## 측정 범위

| 항목 | Kyber 기존 진입점 | Dilithium 기존 진입점 | 기존 PQHybrid API 측정 |
|---|---|---|---|
| 작업 | CPA keypair + encrypt + decrypt 합계 | KeyGen / Sign / Verify 각각 | 전체 KEM / 서명 API |
| 할당 | 측정 전에 수행 | 측정 전에 메모리 풀 할당 | API 내부 할당 포함 |
| 전송 | 포함 | 포함 | 포함 |
| 스트림 | 2 | 단일 및 10개 | wrapper의 실행 구조 |
| 반복 | 배치당 1회 | 각 10회, 중앙값 | 워밍업 3회 + 측정 7회, 중앙값 |
| 배치 | 스트림당 4~16,384, 합계 8~32,768 | 총 10,000 | 측정 CSV 참조 |
| 메시지 | CPA 입력 | 32바이트 | 기존 Dilithium CLI는 6바이트 |

Kyber는 GPU_G1060 프로필(0), serverMode=0을 선택했습니다. 출력 COUNT는
두 스트림의 합계이며 CSV 처리량은 이 COUNT를 실행 시간으로 나눈 값입니다.
완전한 KEM의 CPU 해시, 공유 비밀 도출, Decaps 재암호화 등을 측정하는
PQHybrid KeyGen/Encaps/Decaps 수치와 직접 나누어 속도 향상률을 계산할 수 없습니다.
Kyber 기존 RNG도 디버그용 고정 출력을 생성합니다.

Dilithium은 자동 커널 튜닝 없이 현재 소스 기본 variant를 사용합니다.
CSV total_ms는 기존 타이머가 출력한 마이크로초 중앙값을 ms로 변환했습니다.
원본 로그는 각 알고리즘의 `.log`, 요약은 `measurements.csv`에 있습니다.
CUDA 오류와 서명 검증 실패 메시지가 있는 실행은 요약 생성에서 거부합니다.

## 공정한 후속 비교

1. 같은 모드, 배치, 메시지 길이, 스트림 수, launch 설정을 사용합니다.
2. 양쪽 모두 3회 워밍업 및 7회 측정 중앙값으로 맞춥니다.
3. 메모리 사전 할당 여부와 전송 포함 여부를 맞춥니다.
4. Kyber는 CPA끼리 또는 완전한 KEM끼리 비교합니다.
5. wrapper 비용 비교는 같은 커널 설정으로, 튜닝 효과 비교는 기본/튜닝 설정으로
   나누어 측정합니다. 이 결과만으로 라이브러리 자체의 오버헤드를 단정하지 않습니다.
