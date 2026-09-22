"""Measure complete Kyber key exchanges through the PQCUDA library."""
import argparse
import csv
import ctypes
from pathlib import Path
from statistics import median
from time import perf_counter_ns


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--library', type=Path,
                        default=root / 'build-cuda126/libpqcuda.so')
    parser.add_argument('--output', type=Path,
                        default=root / 'results/kyber_key_exchange.csv')
    parser.add_argument('--batches', type=int, nargs='+',
                        default=[1024, 4096, 16384, 32768])
    args = parser.parse_args()

    lib = ctypes.CDLL(str(args.library.resolve()))
    size = ctypes.c_size_t
    byte_ptr = ctypes.POINTER(ctypes.c_uint8)
    lib.pqcuda_kyber1024_max_batch_size.restype = size
    lib.pqcuda_kyber1024_tuned_kernel_count.restype = size
    lib.pqcuda_kyber1024_tune_launch_profile.argtypes = [size]
    lib.pqcuda_kyber1024_tuned_kernel_name.argtypes = [size]
    lib.pqcuda_kyber1024_tuned_kernel_name.restype = ctypes.c_char_p
    lib.pqcuda_kyber1024_tuned_kernel_threads.argtypes = [size]
    lib.pqcuda_kyber1024_tuned_kernel_threads.restype = size
    lib.pqcuda_kyber1024_apply_launch_profile.argtypes = [ctypes.POINTER(size), size]
    lib.pqcuda_kyber1024_keypair_batch.argtypes = [byte_ptr, byte_ptr, size]
    lib.pqcuda_kyber1024_encapsulate_batch.argtypes = [byte_ptr, byte_ptr, byte_ptr, size]
    lib.pqcuda_kyber1024_decapsulate_batch.argtypes = [byte_ptr, byte_ptr, byte_ptr, size]

    maximum = lib.pqcuda_kyber1024_max_batch_size()
    if any(batch < 1 or batch > maximum for batch in args.batches):
        parser.error(f'batch must be between 1 and {maximum}')
    count = lib.pqcuda_kyber1024_tuned_kernel_count()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    fields = ['batch', 'grid', 'block', 'warmups', 'samples',
              'median_total_ms', 'key_exchanges_per_second',
              'timing_scope', 'streams', 'tuned_kernel_blocks']
    with args.output.open('w', newline='') as output:
        writer = csv.DictWriter(output, fieldnames=fields)
        writer.writeheader()
        for batch in args.batches:
            if lib.pqcuda_kyber1024_tune_launch_profile(batch):
                raise RuntimeError(f'launch tuning failed for batch={batch}')
            tuned = [lib.pqcuda_kyber1024_tuned_kernel_threads(i) for i in range(count)]
            profile_text = ';'.join(
                f'{lib.pqcuda_kyber1024_tuned_kernel_name(i).decode()}={tuned[i]}'
                for i in range(count))
            pk = (ctypes.c_uint8 * (1568 * batch))()
            sk = (ctypes.c_uint8 * (3168 * batch))()
            ct = (ctypes.c_uint8 * (1568 * batch))()
            ss = (ctypes.c_uint8 * (32 * batch))()
            recovered = (ctypes.c_uint8 * (32 * batch))()

            def call(name, *values):
                if getattr(lib, name)(*values):
                    raise RuntimeError(f'{name} failed for batch={batch}')

            # Prepare valid inputs and warm up the complete sequence.
            call('pqcuda_kyber1024_keypair_batch', pk, sk, batch)
            call('pqcuda_kyber1024_encapsulate_batch', ct, ss, pk, batch)
            call('pqcuda_kyber1024_decapsulate_batch', recovered, ct, sk, batch)
            if bytes(ss) != bytes(recovered):
                raise RuntimeError('shared-secret mismatch during preparation')
            for _ in range(3):
                call('pqcuda_kyber1024_keypair_batch', pk, sk, batch)
                call('pqcuda_kyber1024_encapsulate_batch', ct, ss, pk, batch)
                call('pqcuda_kyber1024_decapsulate_batch', recovered, ct, sk, batch)

            timings = []
            for _ in range(10):
                start = perf_counter_ns()
                call('pqcuda_kyber1024_keypair_batch', pk, sk, batch)
                call('pqcuda_kyber1024_encapsulate_batch', ct, ss, pk, batch)
                call('pqcuda_kyber1024_decapsulate_batch', recovered, ct, sk, batch)
                timings.append((perf_counter_ns() - start) / 1e6)
                if bytes(ss) != bytes(recovered):
                    raise RuntimeError('shared-secret mismatch')
            total_ms = median(timings)
            writer.writerow({
                'batch': batch,
                'grid': 'per-kernel',
                'block': 'per-kernel',
                'warmups': 3,
                'samples': 10,
                'median_total_ms': total_ms,
                'key_exchanges_per_second': batch * 1000.0 / total_ms,
                'timing_scope': 'full synchronous KeyGen+Encaps+Decaps API',
                'streams': 1,
                'tuned_kernel_blocks': profile_text,
            })
            output.flush()
            print(f'batch={batch} tuned_blocks={profile_text} '
                  f'total_ms={total_ms:.3f} '
                  f'key_exchange/s={batch * 1000.0 / total_ms:.2f}', flush=True)
    print(f'Saved {args.output}')


if __name__ == '__main__':
    main()
