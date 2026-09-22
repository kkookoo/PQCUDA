"""Plot full KeyGen, Encaps and Decaps throughput. Requires matplotlib."""
import argparse
import csv
from pathlib import Path
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--results', type=Path, default=Path(__file__).resolve().parents[1] / 'results')
    args = parser.parse_args()
    root = args.results
    with (root / 'kyber_operation_throughput.csv').open() as f:
        rows = list(csv.DictReader(f))
    if not rows:
        raise ValueError('No operation measurements found')
    for row in rows:
        for key in ('batch', 'block', 'grid'):
            row[key] = int(row[key])
        row['throughput_ops_s'] = float(row['throughput_ops_s'])
    blocks = sorted({r['block'] for r in rows})
    maximum = max(r['batch'] for r in rows)
    colors = dict(zip(blocks, plt.get_cmap('tab10').colors))
    plt.rcParams.update({'font.size': 10, 'axes.spines.top': False, 'axes.spines.right': False})
    for fixed in (False, True):
        fig, axes = plt.subplots(1, 3, figsize=(16, 5))
        for ax, operation in zip(axes, ('KeyGen', 'Encaps', 'Decaps')):
            data = [r for r in rows if r['operation'] == operation]
            if fixed:
                data = sorted((r for r in data if r['batch'] == maximum), key=lambda r: r['block'])
                ax.plot(range(len(data)), [r['throughput_ops_s'] / 1000 for r in data], '-o')
                ax.set_xticks(range(len(data)), [f"{r['block']}\n{r['grid']}" for r in data], fontsize=8)
                ax.set_xlabel('Block threads (top) / Grid blocks (bottom)')
            else:
                for block in blocks:
                    points = sorted((r for r in data if r['block'] == block and r['batch'] >= block), key=lambda r: r['grid'])
                    if points:
                        ax.plot([r['grid'] for r in points], [r['throughput_ops_s'] / 1000 for r in points], '-o',
                                markersize=3, color=colors[block], label=str(block))
                ax.set_xscale('log', base=2)
                ax.set_xlabel('Grid size (blocks); batch increases along each line')
            ax.set_title(operation, fontweight='bold')
            ax.set_ylabel('Throughput (thousand operations/s)')
            ax.set_ylim(bottom=0)
            ax.grid(alpha=.22)
        fig.suptitle(f'Kyber1024 | full operation throughput' + (f' | batch = {maximum:,}' if fixed else ''), fontsize=17)
        fig.text(.5, .91, 'Full host API time: CPU work + GPU kernels + allocation/transfers | 3 warmups, median of 10 samples', ha='center')
        if not fixed:
            handles = [plt.Line2D([], [], color=colors[b], marker='o', label=str(b)) for b in blocks]
            fig.legend(handles=handles, title='Threads per block (same for all kernels)', loc='upper center', bbox_to_anchor=(.5, .89), ncol=len(blocks), frameon=False)
        fig.tight_layout(rect=(0, 0, 1, .89 if fixed else .77))
        names = [f'kyber_block_grid_{maximum}', 'kyber_overview'] if fixed else ['kyber_grid_sweep']
        for name in names:
            for extension in ('png', 'pdf'):
                fig.savefig(root / f'{name}.{extension}', dpi=160)
        plt.close(fig)
    print('Plots saved to', root)


if __name__ == '__main__':
    main()
