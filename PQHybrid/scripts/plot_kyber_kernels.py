"""Plot six representative Kyber kernels from saved GPU timing measurements."""
import argparse
import csv
from pathlib import Path

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

KERNELS = (
    ('sha3_512_n', 'SHA3-512'),
    ('gen_matrix_n', 'Matrix generation'),
    ('poly_getnoise', 'Noise sampling'),
    ('polyvec_ntt_n', 'Forward NTT'),
    ('polyvec_pointwise_acc_n', 'Pointwise multiply / accumulate'),
    ('polyvec_invntt_n', 'Inverse NTT'),
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--results', type=Path,
                        default=Path(__file__).resolve().parents[1] / 'results')
    args = parser.parse_args()
    with (args.results / 'kyber_launch_throughput.csv').open() as f:
        rows = list(csv.DictReader(f))
    for row in rows:
        for key in ('batch', 'block', 'grid'):
            row[key] = int(row[key])
        row['throughput_ops_s'] = float(row['throughput_ops_s'])
    for kernel, _ in KERNELS:
        if not any(r['kernel'] == kernel for r in rows):
            raise ValueError(f'Missing measurements for {kernel}')
    blocks = sorted({r['block'] for r in rows})
    maximum = max(r['batch'] for r in rows)
    colors = {b: plt.get_cmap('tab10')(i) for i, b in enumerate(blocks)}
    plt.rcParams.update({'font.size': 10, 'axes.spines.top': False,
                         'axes.spines.right': False})
    for fixed in (False, True):
        fig, axes = plt.subplots(2, 3, figsize=(16, 9))
        for ax, (kernel, title) in zip(axes.flat, KERNELS):
            data = [r for r in rows if r['kernel'] == kernel]
            if fixed:
                points = sorted((r for r in data if r['batch'] == maximum),
                                key=lambda r: r['block'])
                ax.plot(range(len(points)),
                        [r['throughput_ops_s'] / 1e6 for r in points], '-o', color='#176b9a')
                ax.set_xticks(range(len(points)),
                              [f"{r['block']}\n{r['grid']}" for r in points], fontsize=8)
                ax.set_xlabel('Block threads (top) / Grid blocks (bottom)')
            else:
                for block in blocks:
                    points = sorted((r for r in data if r['block'] == block
                                     and r['batch'] >= block), key=lambda r: r['grid'])
                    if points:
                        ax.plot([r['grid'] for r in points],
                                [r['throughput_ops_s'] / 1e6 for r in points],
                                '-o', markersize=3, color=colors[block])
                ax.set_xscale('log', base=2)
                ax.set_xlabel('Grid size (blocks); batch increases along each line')
            ax.set_title(f'{title}\n{kernel}', fontsize=11)
            ax.set_ylabel('Throughput (million batch items/s)')
            ax.set_ylim(bottom=0)
            ax.grid(alpha=.22)
        fig.suptitle('Kyber1024 | selected kernel throughput'
                     + (f' | batch = {maximum:,}' if fixed else ''), fontsize=18, y=.99)
        fig.text(.5, .945,
                 'Saved GPU timings: batch / cumulative time of each kernel across the CPA pipeline; excludes host API overhead',
                 ha='center', fontsize=10)
        if not fixed:
            handles = [plt.Line2D([], [], color=colors[b], marker='o', label=str(b))
                       for b in blocks]
            fig.legend(handles=handles, title='Block size (threads)', loc='upper center',
                       bbox_to_anchor=(.5, .93), ncol=len(blocks), frameon=False)
        fig.tight_layout(rect=(0, 0, 1, .93 if fixed else .855), h_pad=2)
        name = (f'kyber_selected_kernels_block_grid_{maximum}' if fixed
                else 'kyber_selected_kernels_grid_sweep')
        for extension in ('png', 'pdf'):
            path = args.results / f'{name}.{extension}'
            fig.savefig(path, dpi=160)
            print(path)
        plt.close(fig)


if __name__ == '__main__':
    main()
