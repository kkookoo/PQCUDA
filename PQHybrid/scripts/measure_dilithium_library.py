"""Measure optimized Dilithium Gen/Sign/Verify throughput through PQCUDA."""
import argparse
import csv
import ctypes
from pathlib import Path
from statistics import median
from time import perf_counter_ns


MODES = (2, 3, 5)
PK_BYTES = {2: 1312, 3: 1952, 5: 2592}
SK_BYTES = {2: 2528, 3: 4000, 5: 4864}
SIG_BYTES = {2: 2420, 3: 3293, 5: 4595}


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--library', type=Path,
                        default=root / 'build-cuda126/libpqcuda.so')
    parser.add_argument('--output', type=Path,
                        default=root / 'results/dilithium_library_throughput.csv')
    parser.add_argument('--batch', type=int, default=10000)
    args = parser.parse_args()

    lib = ctypes.CDLL(str(args.library.resolve()))
    mode_t = ctypes.c_int
    size = ctypes.c_size_t
    ptr = ctypes.POINTER(ctypes.c_uint8)
    lib.pqcuda_dilithium_max_batch_size.restype = size
    lib.pqcuda_dilithium_tune_sign_kernels.argtypes = [mode_t, size]
    lib.pqcuda_dilithium_keypair_batch.argtypes = [mode_t, ptr, size, ptr, size, size]
    lib.pqcuda_dilithium_sign_batch.argtypes = [mode_t, ptr, size, ctypes.POINTER(size),
                                                 ptr, size, ptr, size, size]
    lib.pqcuda_dilithium_verify_batch.argtypes = [mode_t, ptr, size, ptr, size, ptr, size, size]
    if args.batch < 1 or args.batch > lib.pqcuda_dilithium_max_batch_size():
        parser.error('batch exceeds the library maximum')

    args.output.parent.mkdir(parents=True, exist_ok=True)
    fields = ['mode', 'operation', 'batch', 'message_bytes', 'warmups', 'samples',
              'median_total_ms', 'throughput_ops_s', 'timing_scope', 'sign_tuned']
    with args.output.open('w', newline='') as output:
        writer = csv.DictWriter(output, fieldnames=fields)
        writer.writeheader()
        for mode in MODES:
            batch = args.batch
            pk = (ctypes.c_uint8 * (PK_BYTES[mode] * batch))()
            sk = (ctypes.c_uint8 * (SK_BYTES[mode] * batch))()
            sig = (ctypes.c_uint8 * (SIG_BYTES[mode] * batch))()
            msg = (ctypes.c_uint8 * (32 * batch))()
            sig_len = size()

            def keypair():
                result = lib.pqcuda_dilithium_keypair_batch(
                    mode, pk, len(pk), sk, len(sk), batch)
                if result:
                    raise RuntimeError(f'Dilithium-{mode} keypair failed')

            def sign():
                result = lib.pqcuda_dilithium_sign_batch(
                    mode, sig, len(sig), ctypes.byref(sig_len), msg, 32,
                    sk, len(sk), batch)
                if result:
                    raise RuntimeError(f'Dilithium-{mode} sign failed')

            def verify():
                result = lib.pqcuda_dilithium_verify_batch(
                    mode, sig, sig_len, msg, 32, pk, len(pk), batch)
                if result:
                    raise RuntimeError(f'Dilithium-{mode} verify failed')

            keypair()
            if lib.pqcuda_dilithium_tune_sign_kernels(mode, batch):
                raise RuntimeError(f'Dilithium-{mode} sign tuning failed')
            sign()
            verify()
            operations = [('Gen', keypair), ('Sign', sign), ('Verify', verify)]
            for operation, call in operations:
                for _ in range(3):
                    call()
                timings = []
                for _ in range(10):
                    start = perf_counter_ns()
                    call()
                    timings.append((perf_counter_ns() - start) / 1e6)
                total_ms = median(timings)
                writer.writerow({
                    'mode': mode, 'operation': operation, 'batch': batch,
                    'message_bytes': 32, 'warmups': 3, 'samples': 10,
                    'median_total_ms': total_ms,
                    'throughput_ops_s': batch * 1000.0 / total_ms,
                    'timing_scope': 'full synchronous PQCUDA batch API',
                    'sign_tuned': 'yes',
                })
                output.flush()
                print(f'Dilithium-{mode} {operation}: {batch * 1000.0 / total_ms:.2f} ops/s '
                      f'({total_ms:.3f} ms)', flush=True)
    print(f'Saved {args.output}')


if __name__ == '__main__':
    main()
