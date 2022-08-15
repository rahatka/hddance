#!/usr/bin/python3

import argparse
import matplotlib.pyplot as plt
import os
from tsmoothie.smoother import LowessSmoother
from numpy import average, array

samples = 0
hdds = {}

plt.style.use('dark_background')


def produce_graphs(input, output, smooth):
    if not os.path.exists(output):
        os.mkdir(output)
    for filename in os.listdir(input):
        with open(os.path.join(input, filename), 'r') as f:
            lines = f.read().splitlines()
            model = lines[0]
            block = (int(lines[1]))
            if model not in hdds:
                hdds[model] = {}
            hdds[model][block] = []
            xy = hdds[model][block]
            for line in lines[2:]:
                pos = line.split(":")
                access_time = float(pos[1]) * 1000
                if access_time > 0:
                    xy.append((float(pos[0]) * 100, float(pos[1]) * 1000))
            xy.sort(key=lambda x: x[0])
            samples = len(xy)

    for model, blocks in hdds.items():
        print(f"processing {model}")
        plt.title(f"{model}, {samples} samples")
        plt.xlabel("head position, %")
        plt.ylabel("access time, ms")
        stats = ""
        for block in sorted(blocks):
            coord = blocks[block]
            if block <= 512:
                col = "y"
            elif 512 < block <= 4096:
                col = "r"
            elif 4096 < block <= 65536:
                col = "g"
            else:
                col = "b"
            xs = array([x[0] for x in coord])
            ys = array([x[1] for x in coord])

            if smooth:
                smoother = LowessSmoother(smooth_fraction=0.05)
                smoother.smooth(ys)
                low, up = smoother.get_intervals('prediction_interval')
                plt.plot(xs, smoother.smooth_data[0], linewidth=1, alpha=0.5, color=col)
                plt.fill_between(xs, low[0], up[0], alpha=0.2, color=col)
            plt.scatter(xs, ys, color=col, marker=',', lw=0, s=0.2, label=f"{(block / 1024):g}")
            plt.legend(title='block size, KiB', title_fontsize="xx-small", markerscale=10, fontsize="xx-small", framealpha=0.5)
            av = average(ys)
            kib = f"{(block / 1024):g} KiB"
            ms = f"{round(av, 2)} ms"
            iops = f"{round(1000 / av, 2)} IOPS"
            mibs = f"{round((block * (1000 / av)) / 1048576, 2)} MiB/s"
            stats += f"{kib:10} {ms:10} {iops:16} {mibs}\n"
        for scale in (50, 100, 250):
            filename = f"{output}/{model}_{scale}_ms.png"
            # if os.path.exists(filename):
            #     continue
            plt.figtext(0.33, 0.92, stats, horizontalalignment='left', fontsize=5, fontdict={'family': 'monospace'})
            plt.figtext(0.97, 0.02, "hddance v1.0", horizontalalignment='right', fontsize=4)
            plt.axis([0, 100, 0, scale])
            plt.savefig(filename, dpi=600)
        plt.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument('-i', '--input', required=False, help="input directory with test result files", default=".")
    parser.add_argument('-o', '--output', required=False, help="output directory for images", default=".")
    parser.add_argument('-s', '--smooth', action='store_true', help="generate smooth lines and prediction intervals")
    args = parser.parse_args()
    produce_graphs(args.input, args.output, args.smooth)
