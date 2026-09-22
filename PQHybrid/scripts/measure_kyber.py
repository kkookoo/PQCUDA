"""Measure full Kyber KEM APIs (CPU work, allocations and transfers included)."""
import argparse
import csv
import ctypes
from pathlib import Path
from statistics import median
from time import perf_counter_ns


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--library', type=Path, default=root / 'build-cuda126/libpqcuda.so')
    parser.add_argument('--output', type=Path, default=root / 'results')
    parser.add_argument('--batches', type=int, nargs='+')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    lib = ctypes.CDLL(str(args.library.resolve()))
    size = ctypes.c_size_t
    ptr = ctypes.POINTER(ctypes.c_uint8)
    lib.pqcuda_kyber1024_max_batch_size.restype = size
    lib.pqcuda_kyber1024_tuned_kernel_count.restype = size
    lib.pqcuda_kyber1024_apply_launch_profile.argtypes = [ctypes.POINTER(size), size]
    signatures = {'keypair': [ptr, ptr, size], 'encapsulate': [ptr, ptr, ptr, size],
                  'decapsulate': [ptr, ptr, ptr, size]}
    for name, signature in signatures.items():
        getattr(lib, f'pqcuda_kyber1024_{name}_batch').argtypes = signature
    maximum = lib.pqcuda_kyber1024_max_batch_size()
    batches = sorted(set(args.batches or
                         [n for n in (1, 32, 128, 512, 1024, 2048, 4096, 8192, 16384)
                          if n < maximum] + [maximum]))
    if any(n < 1 or n > maximum for n in batches):
        parser.error(f'batches must be between 1 and {maximum}')
    count = lib.pqcuda_kyber1024_tuned_kernel_count()
    blocks = []
    for block in (16, 32, 64, 128, 192, 256, 512, 1024):
        # A single grid/block label is valid only when every kernel supports it.
        if lib.pqcuda_kyber1024_apply_launch_profile((size * count)(*([block] * count)), count) == 0:
            blocks.append(block)
        else:
            print(f'Skipping block={block}: not supported by all kernels', flush=True)
    if not blocks:
        raise RuntimeError('No supported launch configuration; check CUDA access')

    def checked(name, *values):
        if getattr(lib, f'pqcuda_kyber1024_{name}_batch')(*values):
            raise RuntimeError(f'{name} failed at batch={batch}, block={block}')

    path = args.output / 'kyber_operation_throughput.csv'
    with path.open('w', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=['operation', 'batch', 'block', 'grid',
                                'median_total_ms', 'throughput_ops_s', 'warmups', 'samples', 'timing_scope'])
        writer.writeheader()
        for batch in batches:
            buf = lambda n: (ctypes.c_uint8 * (n * batch))()
            pk, sk, ct, ss, recovered = buf(1568), buf(3168), buf(1568), buf(32), buf(32)
            for block in blocks:
                profile = (size * count)(*([block] * count))
                if lib.pqcuda_kyber1024_apply_launch_profile(profile, count):
                    raise RuntimeError('Cannot apply launch profile')
                # Prepare valid inputs outside all measured regions.
                checked('keypair', pk, sk, batch)
                checked('encapsulate', ct, ss, pk, batch)
                for operation, name, values in (
                    ('KeyGen', 'keypair', (pk, sk, batch)),
                    ('Encaps', 'encapsulate', (ct, ss, pk, batch)),
                    ('Decaps', 'decapsulate', (recovered, ct, sk, batch)),
                ):
                    timings = []
                    for trial in range(13):
                        start = perf_counter_ns()
                        checked(name, *values)  # APIs synchronize before returning.
                        elapsed = (perf_counter_ns() - start) / 1e6
                        if trial >= 3:
                            timings.append(elapsed)
                    elapsed = median(timings)
                    # Refresh ciphertext after KeyGen; verify every measured configuration.
                    if operation == 'KeyGen':
                        checked('encapsulate', ct, ss, pk, batch)
                    checked('decapsulate', recovered, ct, sk, batch)
                    if bytes(ss) != bytes(recovered):
                        raise RuntimeError('Shared-secret mismatch')
                    writer.writerow(dict(operation=operation, batch=batch, block=block,
                        grid=(batch + block - 1) // block, median_total_ms=elapsed,
                        throughput_ops_s=batch * 1000 / elapsed, warmups=3, samples=10,
                        timing_scope='full synchronous host API'))
                    f.flush()
                print(f'Measured batch={batch}, block={block}', flush=True)
    print(f'Saved {path}', flush=True)


if __name__ == '__main__':
    main()
