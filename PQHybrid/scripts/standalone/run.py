"""Run current standalone benchmark entry points without the PQHybrid wrapper."""
import csv
from pathlib import Path
import re
import subprocess

root = Path(__file__).resolve().parents[2]
build = root / 'build-standalone-cuda126'
output = root / 'results/standalone'
output.mkdir(exist_ok=True)
rows = []
for name, executable, input_text in [
    ('kyber', build / 'bench_kyber_original', '0\n0\n'),
    *((f'dilithium{mode}', build / f'dilithium/bin/bench_cuDilithium{mode}', None)
      for mode in (2, 3, 5)),
]:
    print(f'Running {name}', flush=True)
    result = subprocess.run([str(executable)], input=input_text, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            cwd=output, timeout=600)
    (output / f'{name}.log').write_text(result.stdout)
    if result.returncode or re.search(r'CUDA Runtime Error|Verification failed|ERROR\s*:|invalid configuration|out of memory', result.stdout, re.I):
        raise RuntimeError(f'{name} failed; see {output / (name + ".log")}')
    if name == 'kyber':
        matches = re.findall(r'COUNT=(\d+) \| Time Elapsed: ([\d.]+) ms. K/s: (\d+)', result.stdout)
        if not matches or result.stdout.count('MESSAGE VERIFICATION COMPLETE') != 2 * len(matches):
            raise RuntimeError('Missing Kyber correctness checks')
        for batch, ms, _ in matches:
            rows.append(dict(algorithm=name, operation='CPA keypair+enc+dec', batch=int(batch),
                             streams=2, trials=1, total_ms=float(ms),
                             throughput_ops_s=int(batch)*1000/float(ms)))
    else:
        for line in result.stdout.splitlines():
            fields = line.split(',')
            if len(fields) == 5 and fields[0].strip() in (
                'verify stream', 'sign stream', 'keypair stream',
                'verify batch', 'sign_batch', 'keypair batch'):
                operation, trials, minimum, med, stddev = fields
                rows.append(dict(algorithm=name, operation=operation.strip(), batch=10000,
                                 streams=10 if 'stream' in operation else 1,
                                 trials=int(trials), total_ms=float(med)/1000,
                                 throughput_ops_s=10000*1e6/float(med)))
        if len([r for r in rows if r['algorithm'] == name]) != 6:
            raise RuntimeError(f'Missing {name} measurements')
    print(f'Finished {name}', flush=True)
with (output / 'measurements.csv').open('w', newline='') as f:
    writer = csv.DictWriter(f, fieldnames=list(rows[0]))
    writer.writeheader()
    writer.writerows(rows)
print(f'Saved {output / "measurements.csv"}', flush=True)
