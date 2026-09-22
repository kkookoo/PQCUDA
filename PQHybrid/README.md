# PQCUDA: Linux / Windows 라이브러리 빌드

Kyber1024와 Dilithium-2/3/5의 C API를 공유·정적 라이브러리로 제공합니다.
공개 헤더는 `include/pqcuda.h`입니다.

| 플랫폼 | 공유 라이브러리 | 정적 라이브러리 |
| --- | --- | --- |
| Linux | `libpqcuda.so` | `libpqcuda.a` |
| Windows x64 / MSVC | `pqcuda.dll` + `pqcuda.lib` | `pqcuda_static.lib` |

Windows의 `pqcuda.lib`는 DLL 연결용 import library입니다.
정적 링크에는 `pqcuda_static.lib`를 사용합니다.

## 소스 배치

저장소 전체를 내려받고 다음 상대 경로를 유지합니다.

```text
PQCUDA/
  PQHybrid/
  ML-DSA/cuDilithium/
  ML-KEM/Kyber1024/Kyber_GPU_Batched/
```

## Windows 빌드

필요한 도구는 CMake 3.20 이상, Ninja, CUDA Toolkit,
해당 CUDA 버전이 지원하는 Visual Studio의 MSVC x64 C++ 빌드 도구와 Windows SDK입니다.
Visual Studio 설치 시 C++용 CMake 도구를 선택하면 CMake와 Ninja도 설치됩니다.
선택한 Visual Studio의 **x64 Native Tools Command Prompt**에서 실행합니다.
소스와 빌드 폴더는 `C:\work\PQCUDA`처럼 Windows 로컬 디스크에 둡니다.

현재 GTX 1070의 compute capability는 6.1입니다. 이 GPU용으로 빌드하려면
Windows CUDA 12.6과 그 버전이 지원하는 MSVC 193x 도구 집합(Visual Studio 2022)을 사용합니다.
CUDA 13.x에서는 Pascal(sm_61)용 오프라인 컴파일이 제거되었으므로
GTX 1070용 바이너리를 만들 수 없습니다.

```bat
cd C:\work\PQCUDA\PQHybrid
set "CUDACXX=C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.6\bin\nvcc.exe"
cmake -S . -B build-windows -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CUDA_ARCHITECTURES=61
cmake --build build-windows --parallel 4
ctest --test-dir build-windows --output-on-failure
cmake --install build-windows --prefix dist/windows
```

다른 GPU에서는 `61`을 대상 GPU에 맞는 값으로 바꿉니다.
여러 GPU를 지원하려면 해당 Toolkit에서 지원하는 값들을 세미콜론으로 지정합니다
(예: `"-DCMAKE_CUDA_ARCHITECTURES=75;86;89"`).
CUDA나 컴파일러를 바꿀 때는 새 빌드 폴더를 사용합니다.
Visual Studio 생성기를 사용하는 경우에는 빌드·설치에 `--config Release`,
CTest에 `-C Release`를 추가합니다.

설치 결과:

```text
dist/windows/
  bin/pqcuda.dll
  bin/pqcuda_test.exe
  bin/pqcuda_test_static.exe
  lib/pqcuda.lib
  lib/pqcuda_static.lib
  lib/kyber_gpu.lib
  lib/cuDilithium2.lib
  lib/cuDilithium3.lib
  lib/cuDilithium5.lib
  lib/pqcuda_fips202_cpu.lib
  lib/pqcuda_fips202_cuda_thread.lib
  lib/pqcuda_fips202_cuda_warp.lib
  lib/pqcuda_random.lib
  lib/cmake/PQCUDA/...
  include/pqcuda.h
```

실행할 때는 호환되는 NVIDIA 드라이버와 빌드에 사용한 CUDA의 런타임 DLL이 필요합니다.
개발 PC에서는 해당 CUDA의 `bin` 폴더를 `PATH`에 추가하고 실행합니다.
MSVC 런타임은 CMake 기본값인 Release `/MD`, Debug `/MDd`를 사용합니다.
정적 PQCUDA 라이브러리를 선택해도 CUDA·MSVC 런타임까지 모두 정적 링크되는 것은 아닙니다.

```bat
set "PATH=C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.6\bin;%PATH%"
dist\windows\bin\pqcuda_test.exe
```

## 다른 CMake 프로젝트에서 사용

설치 폴더 전체를 유지하고 `CMAKE_PREFIX_PATH`로 지정합니다.
정적 라이브러리는 CUDA device link와 내부 라이브러리가 필요하므로
다음과 같이 CMake의 제공 타깃으로 연결합니다.

```cmake
cmake_minimum_required(VERSION 3.20)
project(MyApp LANGUAGES C CXX CUDA)
find_package(PQCUDA CONFIG REQUIRED)
add_executable(my_app main.c)
target_link_libraries(my_app PRIVATE PQCUDA::pqcuda)
# 정적 링크를 선택하려면 위 타깃을 PQCUDA::pqcuda_static으로 변경합니다.
set_target_properties(my_app PROPERTIES
    LINKER_LANGUAGE CXX
    CUDA_RESOLVE_DEVICE_SYMBOLS ON)
```

```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CUDA_ARCHITECTURES=61 -DCMAKE_PREFIX_PATH=C:/work/PQCUDA/PQHybrid/dist/windows
cmake --build build
```

DLL 사용 시 `pqcuda.dll`을 실행 파일 옆에 놓거나 DLL이 있는 폴더를 `PATH`에 추가합니다.
CMake 타깃은 정적 링크용 `PQCUDA_STATIC` 정의와 의존 라이브러리를 자동으로 전달합니다.
직접 MSVC 프로젝트에 연결하는 경우 정적 링크에는 `PQCUDA_STATIC`을 정의하고,
위 내부 `.lib`들과 CUDA device link, CUDA 및 `bcrypt.lib` 등의 링크 의존성도 구성해야 합니다.
`PQCUDA_BUILD_SHARED`는 DLL 자체를 빌드할 때만 사용하는 정의입니다.

## Linux 빌드

```sh
cmake -S . -B build-cuda126 -DCMAKE_CUDA_COMPILER=/usr/local/cuda-12.6/bin/nvcc -DCMAKE_CUDA_ARCHITECTURES=61 -DCMAKE_BUILD_TYPE=Release
cmake --build build-cuda126 --parallel 4
ctest --test-dir build-cuda126 --output-on-failure
./build-cuda126/pqcuda_test
```

`ctest`는 GPU 없이 긴 줄 입력, EOF/CRLF 처리, OS 난수 생성, 단조 시계,
벤치마크 추천 기준과 Dilithium 함수별 launch 정보·인자 검증을 검사합니다.
실제 GPU 키 생성·캡슐화·서명 동작은 대상 GPU에서 별도로 확인해야 합니다.

## 검증한 환경

- Linux: GCC 13.3 / CUDA 12.6 / sm_61. 공유·정적 빌드와 설치 패키지의 C 링크,
  플랫폼 테스트를 통과했습니다. GTX 1070에서 Kyber1024 및 Dilithium-2/3/5 연산도 확인했습니다.
- Windows: MSVC 19.51 / CUDA 13.3 / sm_75. DLL·정적 LIB 빌드, 공개 API 29개 export,
  설치 패키지의 공유·정적 C 링크, CLI 메뉴, 플랫폼 테스트를 통과했습니다.
  이 sm_75 검증용 바이너리는 GTX 1070에서 실행할 수 없으며 Windows GPU 연산은 검증하지 않았습니다.

참고: [CUDA 12.6 Windows 지원 도구](https://docs.nvidia.com/cuda/archive/12.6.2/cuda-installation-guide-microsoft-windows/index.html),
[CUDA 13.0 릴리스 노트](https://docs.nvidia.com/cuda/archive/13.0.1/cuda-toolkit-release-notes/index.html),
[Windows 난수 API](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptgenrandom),
[Windows 고해상도 시계](https://learn.microsoft.com/en-us/windows/win32/sysinfo/acquiring-high-resolution-time-stamps).

## Kyber Benchmark의 자동 launch 탐색

`2. Benchmark` → `1. ML-KEM` → keypair/encap/decap을 선택하면 batch 입력 없이
현재 GPU에서 자동 탐색합니다. GPU 이름, SM 수, 최대 block thread 수를 표시합니다.
처리 개수 후보는 작은 latency 확인용 `1, 32, 128, 512`와 SM 수에 정렬된 batch들,
API 최대값 32768입니다. ML-KEM의 기본 단위는 `SM 수 × 32`이며,
이 단위의 배수 중 1500에 가장 가까운 값을 간격으로 사용합니다.
기본 단위가 2000을 넘는 GPU에서는 SM당 항목 수를 32→16→8→…→1로 줄입니다.
따라서 일반적인 GPU의 간격은 1000~2000이며 SM 수의 배수를 유지합니다.
예를 들어 SM 15개이면 `1440, 2880, …, 31680, 32768`을 테스트합니다.
작은 후보와 마지막 최대값은 정렬 예외입니다. 모든 block 후보에서 동시에 균등한
SM 배치를 보장하지는 않으며 실제 처리량은 기존 커널 튜닝으로 비교합니다.
각 처리 개수에서 block 후보 `16, 32, 64, 128, 192, 256, 512, 1024`를
GPU 및 커널의 지원 한도 안에서 측정합니다. grid는 `ceil(처리 개수 / block)`입니다.

커널별 튜닝 후 선택한 API 작업을 3회 워밍업하고 10회 측정합니다.
**탐색에서 측정한 최대 처리량의 95% 이상인 후보 중 전체 완료 시간의 중앙값이
가장 짧은 설정**을 추천합니다. 전체 시간에는 API 내부 메모리 할당, 전송,
CPU 처리와 GPU 실행이 포함되며, 튜닝과 입력 데이터 준비·파일 저장은 제외됩니다.
`Latency (ms/op)`는 전체 시간을 처리 개수로 나눈 값이며, 추천 시 사용하는 전체 지연시간과 구분합니다.

결과에는 후보별 측정값과 추천 처리 개수, 커널별 1차원 grid/block을 표시합니다.
프로필은 CPA 파이프라인 전체 커널을 나열하며 각 작업은 필요한 커널만 실행합니다.
추천은 탐색한 후보 범위에서의 결과입니다. 추천 batch와 커널별 block을 현재 디렉터리의
`pqcuda_kyber_1024_opN.profile`에 작업별로 저장합니다(N: 1=keypair, 2=encap, 3=decap).
마지막으로 측정한 후보가 아니라 추천 후보의 설정을 복원하여 저장합니다.

## Dilithium Benchmark의 자동 launch 탐색

`2. Benchmark` → `2. ML-DSA` → 모드(2/3/5) → keypair/sign/verify를 선택합니다.
batch 입력 없이 SM 수 기반 후보를 자동 탐색합니다. Dilithium은 한 block이 최대 4개
항목을 처리하는 구조에 맞춰 기본 단위를 `SM 수 × 4`로 사용하며, 나머지 간격 선택
방식은 Kyber와 같습니다.
sign/verify의 측정 메시지는 배치 전체에 동일한 6바이트 `PQCUDA`를 사용합니다.

- `keypair`·`verify`: `block=(32,1)`, `grid=처리 개수`로 실행합니다.
  서명 함수 튜닝은 수행하지 않습니다.
- `sign`: 처리 개수 후보마다 7개 단계의 지원 함수 변형을 측정하고 선택합니다.
  함수가 지원하는 `(32,1)`, `(32,4)`, `(128,1)` block 구조를 유지합니다.
  선택된 함수명과 실제 grid/block을 후보별로 기록하여 추천 후보의 설정을 출력합니다.
- SHAKE·행렬 생성·unpack/NTT의 grid는 처리 개수에 따라 결정됩니다.
  서명 재시도 단계는 내부 실행 용량 2,048을 사용하므로 단일 항목 함수는 grid=2,048,
  4개 항목을 묶는 함수는 grid=512입니다. 재시도 횟수는 서명 승인 여부에 따라 달라집니다.
  `sig_copy_kernel`은 `block=(96,1)`, `grid=copy_count`로 실행되며 copy_count는 실행 중 결정됩니다.

Kyber와 동일하게 3회 워밍업·10회 측정 후, 측정한 최대 처리량의 95% 이상에서
전체 완료 시간 중앙값이 가장 짧은 후보를 추천합니다. 시간 측정 범위도 동일하며,
keypair·sign 결과의 서명 검증은 측정 구간 밖에서 수행합니다.
전체 함수 조합을 모두 탐색하는 방식은 아니며 단계별 함수 튜닝 후 API 시간을 비교합니다.
추천 batch와 sign 단계별 variant는 `pqcuda_dilithium_MODE_opN.profile`에 저장합니다.
keypair/verify는 고정 launch 구조이므로 추천 batch만 저장합니다.

## Run algorithm의 배치 실행과 결과 파일

`1. Run algorithm`에서 알고리즘·모드·작업을 선택한 다음 실행 설정을 선택합니다.

- `1. Batch 직접 입력`: batch size(1~32,768)를 직접 입력합니다.
- `2. 저장된 benchmark 최적 설정 사용`: 같은 작업의 `.profile`에서 추천 batch와 커널 설정을
  불러와 재튜닝 없이 실행합니다. 프로그램을 종료했다가 다시 실행해도 사용할 수 있습니다.
Kyber1024의 keypair/encaps/decaps와 Dilithium-2/3/5의 keypair/sign/verify가 배치 API로 실행됩니다.
benchmark가 성공적으로 완료될 때마다 해당 알고리즘·모드·작업의 프로필을
이번에 측정한 최적 batch와 커널 설정으로 덮어씁니다. 저장이 완료되면
`Updated optimal profile`과 batch 값을 출력합니다.
프로필은 현재 디렉터리에서 찾습니다. GPU 또는 커널 구현을 변경했다면 benchmark를 다시 실행하세요.
설정이 없거나 유효하지 않으면 오류를 표시하며 다른 설정으로 임의 실행하지 않습니다.

출력은 기존 `.hex` 파일 이름으로 프로그램을 실행한 현재 디렉터리에 **배치 항목 하나당 한 줄**로 저장합니다.
각 파일의 같은 줄 번호가 같은 배치 항목에 대응합니다. `@파일명` 입력으로 배치 전체를
다시 읽을 수 있으며, 입력 파일의 전체 데이터 크기는 선택한 batch와 일치해야 합니다.
`@파일명`의 상대 경로도 현재 디렉터리를 기준으로 찾습니다.
기존 단건 파일은 batch=1에서 계속 사용할 수 있습니다.

- Kyber 파일은 실행 디렉터리에 저장합니다: `kyber1024_public_key.hex`,
  `kyber1024_private_key.hex`, `kyber1024_ciphertext.hex`,
  `kyber1024_shared_key_encap.hex`, `kyber1024_shared_key_decap.hex`.
- cuDilithium 파일도 실행 디렉터리에 저장합니다:
  `cudilithium{2,3,5}_public_key.hex`, `cudilithium{2,3,5}_private_key.hex`,
  `cudilithium{2,3,5}_signature.hex`.
- cuDilithium은 메시지 한 개를 입력받아 배치 전체에 공통으로 사용합니다.
  검증 시에도 같은 메시지와, 같은 순서로 저장된 공개키·서명 파일을 입력합니다.
  검증 결과는 배치 전체가 유효한지 화면에 표시합니다.

예를 들어 batch=3으로 keypair를 실행하면 공개키 파일과 개인키 파일에 각각 3줄이 저장됩니다.
Kyber encaps에서 batch=3과 `@kyber1024_public_key.hex`를 입력하고,
decaps에서는 batch=3과 `@kyber1024_ciphertext.hex`, `@kyber1024_private_key.hex`를 입력합니다.


## 공통 암호 라이브러리

공통 SHA3/SHAKE 구현과 OS 난수 생성은 저장소 루트의 `common/`에서 관리합니다.
Kyber wrapper의 CPU 해시는 Dilithium 디렉터리 대신 공통 CPU 라이브러리를 참조합니다.

| CMake 타깃 | 구현 | 사용하는 경로 |
|---|---|---|
| `pqcuda_fips202_cpu` | CPU SHA3-256/512, SHAKE128/256 | Kyber wrapper의 CPU 해시, CPU FIPS202 사용자 |
| `pqcuda_fips202_cuda_thread` | 스레드당 Keccak 상태 | Kyber GPU 커널 |
| `pqcuda_fips202_cuda_warp` | warp가 분담하는 Keccak 상태 | Dilithium GPU 커널 |
| `pqcuda_random` | OS 난수 생성 | 기존 `pqcuda_random_bytes` 호출부 |

GPU 구현은 기존 스레드·warp 실행 구조를 유지합니다. 알고리즘별 NTT, 샘플링,
난수 소비 방식은 변경하지 않습니다. 기존 FIPS202 파일 경로는 공통 구현을 참조하는
호환 파일이며, 실제 구현은 `common/`에만 있습니다. cuDilithium 단독 CMake 빌드도
같은 공통 소스를 사용합니다. 기존 `fips202` CMake 타깃은 CPU·warp 라이브러리를
연결하는 호환 INTERFACE 타깃입니다.

설치 시 공통 정적 라이브러리와 헤더도 함께 설치되며, CMake에서
`PQCUDA::pqcuda_fips202_cpu` 등으로 직접 사용할 수 있습니다.
CPU 헤더는 C/C++ 공용이고 `#include <fips202_cpu/fips202.h>`로 참조합니다.

검증에는 독립적인 Python hashlib 결과를 기준으로 한 CPU/GPU 해시 테스트,
SHAKE 분할 absorb/squeeze, Kyber KEM 왕복, Dilithium 2/3/5 서명·검증과 변조 입력
검사를 포함합니다. CUDA 테스트는 GPU가 없는 환경에서 skipped로 표시됩니다.

## Kyber 연산 단위 성능 그래프

```bash
python3 PQHybrid/scripts/measure_kyber.py
python3 PQHybrid/scripts/plot_kyber.py
```

Python 환경에 `matplotlib`을 설치한 뒤 저장소 루트에서 실행합니다. 측정은 `build-cuda126/libpqcuda.so`를 사용하며,
다른 빌드는 `--library`로 지정합니다. `--batches 32 128 512`로 배치를 제한할 수 있습니다.
결과는 `results/kyber_operation_throughput.csv`와 PNG/PDF로 저장됩니다.
`kyber_grid_sweep`는 KeyGen, Encaps, Decaps별로 블록 크기를 고정한 상태에서
배치와 그리드를 늘린 결과입니다. `kyber_block_grid_<최대배치>`와
`kyber_overview`는 최대 배치에서 블록/그리드 조합을 비교합니다.

각 점은 동기식 KEM 배치 API의 전체 완료 시간을 3회 워밍업 후 10회 측정한
중앙값이며, 처리량은 `배치 수 / 전체 완료 시간`입니다. CPU 해시·난수,
GPU 커널, 메모리 할당과 전송을 포함합니다. 입력 준비와 공유 비밀 일치 검사는
측정 구간 밖에서 수행합니다. Decaps의 재암호화도 API 시간에 포함됩니다.
그리드 축은 배치 증가를 동반하므로 동일 작업량에서 그리드만 바꾸는 실험은 아닙니다.
하나의 그리드 크기로 비교할 수 있도록 내부 모든 커널에 동일한 블록 크기를
적용하며, 모든 커널이 지원하지 않는 후보는 제외합니다.
기존 `kyber_launch_throughput.csv`의 커널별 누적 측정치는 이 그래프에 사용하지 않습니다.

## 공통 구현 KAT

Kyber1024 round2 참조 KAT는 `ctest --test-dir PQHybrid/build-cuda126 -R kyber_kat --output-on-failure`로 실행합니다.
고정 난수는 테스트 실행 파일에만 링크하며 실제 라이브러리 RNG는 변경하지 않습니다.
Dilithium 2/3/5 기존 10,000개 벡터 검사와 참조 벡터 재생성 방법은
[tests/kat/README.md](tests/kat/README.md), 실행 결과와 발견한 오류는
[results/kat/README.md](results/kat/README.md)에 기록했습니다.
