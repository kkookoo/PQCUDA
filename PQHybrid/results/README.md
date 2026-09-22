# ML-KEM launch throughput

Measured on NVIDIA GeForce GTX 1070 (8 GiB), NVIDIA driver 560.94 / CUDA 12.6, 2026-09-10.

- `kyber_block_grid_32768.png` / `.pdf`: fixed batch 32768, block threads and corresponding grid blocks on the x-axis.
- `kyber_grid_sweep.png` / `.pdf`: grid size on the x-axis, a separate line per block size. Batch varies along each line. Underfilled configurations (batch < block) are omitted from this plot, but retained in CSV.
- `kyber_launch_throughput.csv`: all supported measured configurations; unsupported block sizes are excluded by the existing tuner.
- `kyber_batch_*.csv`: measurements for individual batches.

Each configuration uses 3 warmups and 7 measured runs with round-trip correctness checks. Throughput is batch / median cumulative GPU time for each kernel across the tuning pipeline, in operations per second. These measurements are not end-to-end keypair/encapsulation/decapsulation API throughput. Grid is ceil(batch / block); grid and block are not independently swept at a fixed batch. The GPU also drives the display, so background activity can affect results.

Reproduce from the repository root (GPU access required):

```bash
cmake --build PQHybrid/build-cuda126 --parallel 4
python3 PQHybrid/scripts/measure_kyber.py
# Requires matplotlib in your Python environment.
python3 PQHybrid/scripts/plot_kyber.py
```

## Dense API benchmark validation

`kyber_keypair_dense_benchmark.log` records the CLI keypair benchmark with 37 candidates:
1, 32, 128, 512, then steps of 1000 through 32000, and 32768.
The API result selects the shortest median batch completion time among candidates
at or above 95% of measured peak throughput. `Latency (ms/op)` is the amortized
per-item time. The saved configuration is `pqcuda_kyber_1024_op1.profile`.
`kyber_saved_profile_execution.log` records a separate CLI process loading that
profile and generating the recommended batch of keypairs.

The earlier kernel plots and CSV above are a separate measurement snapshot;
they do not represent this dense API sweep. Subsequent runs of the measurement script use SM-aligned steps near 1500 items;
these stored logs and profiles remain snapshots of the earlier 1000-step sweep.
