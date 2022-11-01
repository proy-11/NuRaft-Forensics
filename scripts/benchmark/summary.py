import os
import json
import argparse
import numpy as np
import matplotlib.pyplot as plt

from collections import defaultdict
from os.path import join, isdir

BATCH_FREQ = 5
TIME_SCALE = 20
PLOT_MARGIN = 1.5

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('-p', '--path', required=True)
    args = parser.parse_args()

    res = defaultdict(list)

    for dirname in os.listdir(args.path):
        if not isdir(join(args.path, dirname)):
            continue
        try:
            bs, i = map(int, dirname.split('-'))
            with open(join(args.path, dirname, 'server_1.json'), 'r') as f:
                obj = json.load(f)

            res[bs].append(obj['log_height_committed'] - 5)
        except:
            continue

    keys = np.array(sorted(list(res.keys())))
    means, stds = [], []
    for key in keys:
        means.append(np.mean(res[key]))
        stds.append(np.std(res[key]))

    means, stds = np.array(means), np.array(stds)
    keys *= BATCH_FREQ * TIME_SCALE

    fig, ax = plt.subplots(1, 1, figsize=(4, 4))
    ax.set_xlabel('Input size')
    ax.set_ylabel('# Committed')
    ax.set_title('20 sec, 5 batches/sec, qlen = 1000')
    ax.grid(True)
    ax.set_xlim(keys.min() / PLOT_MARGIN, keys.max() * PLOT_MARGIN)
    ax.set_ylim(keys.min() / PLOT_MARGIN, keys.max() * PLOT_MARGIN)
    ax.set_xscale('log')
    ax.set_yscale('log')
    ax.plot(keys, means, color='k')
    ax.fill_between(keys, means-3*stds, means+3*stds, alpha=.2, color='k')

    fig.tight_layout()
    fig.savefig('summary.jpg')
    plt.close()
