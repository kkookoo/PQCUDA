# Kyber Key Exchange 비교

측정 파일: `kyber_key_exchange.csv`

라이브러리에서 `KeyGen → Encaps → Decaps`를 하나의 key exchange로 묶어
동기식으로 측정했다. 각 batch마다 라이브러리의 커널별 자동 튜닝을 먼저 실행하고,
선택된 block 설정을 그대로 적용했다. 따라서 커널마다 block 크기와 grid가 다르며,
공통 block 설정을 강제하지 않았다. 각 batch는 3회 워밍업 후 7회 측정 중앙값을
사용했다. API 내부의 CPU 해시·난수·메모리 할당·GPU 실행을 모두 포함하며,
라이브러리 호출은 한 stream에서 순차 실행했다.

원본 표의 Kyber 막대는 `COUNT*2 / elapsed_time`으로 계산된다. 기존 프로그램이
두 stream에서 CPA keypair·encapsulation·decapsulation을 실행하기 때문에,
표의 수치와 아래 라이브러리 수치는 측정 범위가 완전히 같지 않다. 원본 표의
Kyber 값은 C=9.27k, AVX2=32.3k, G940=30.6k, G1060=175k, P6000=299k,
V100=473k key exchanges/s이다.

GTX 1070에서 라이브러리 측정 결과:

| Batch | Grid | Block | Total time (ms) | Key exchanges/s |
|---:|---:|---:|---:|---:|
| 1,024 | 커널별 | 커널별 | 142.393 | 7.19k |
| 4,096 | 커널별 | 커널별 | 207.940 | 19.70k |
| 16,384 | 커널별 | 커널별 | 487.302 | 33.62k |
| 32,768 | 커널별 | 커널별 | 921.895 | **35.54k** |

따라서 현재 조건에서 자동 튜닝 라이브러리의 최고 측정값은 **35.54k key exchanges/s**이다.
이는 표의 G1060 175k와 직접 비교하면 약 19.5%지만, 표가 CPA 파이프라인을
두 stream으로 측정한 반면 라이브러리는 완전한 KEM과 API 비용을 포함한다.
동일한 알고리즘·stream 수·사전 할당·측정 범위로 맞춘 비교가 필요하다.
