import matplotlib.pyplot as plt
import matplotlib
from typing import Tuple


HATCHES = ['/', '\\', '|', '-', '+', 'x', 'o', 'O', '.', '*']
COLW = 3.45


def create_figure(ncols: float, aspect_ratio=0.618, fontsize: int = 10) -> Tuple[plt.Figure, plt.Axes]:
    matplotlib.rcParams.update({
        "text.usetex": True,
        'font.size': fontsize,
        'font.family': 'serif',
        'axes.linewidth': 0.5
    })
    ax = plt.subplot()
    fig = plt.gcf()
    fig.set_figwidth(ncols * COLW)
    fig.set_figheight(ncols * COLW * aspect_ratio)
    return fig, ax


MARKER = ["^", "*", ">", "x", "+", "o", "v",  "<"]
COLORS = ['#1b9e77', '#d95f02', '#7570b3', '#e7298a', '#66a61e', '#e6ab02']

NUM_COLORS = len(COLORS)
NUM_MARKER = len(MARKER)
