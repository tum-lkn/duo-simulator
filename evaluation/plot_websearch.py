import os

import numpy as np

import plotutils
import literals

import collect_data as cd


DATA_PATH = f"../data/websearch/64racks_8hosts_10s/"
IMG_PATH = f"../img/websearch/64racks_8hosts_10s/"


def plot():
    os.makedirs(IMG_PATH, exist_ok=True)
    IMG_SUFFIX = "_websearch"

    TOPOLOGIES = [
        "ddb-2s-6da-mixed-co10000000-rto15ms-cwnd30-day9900us-night100us-q50-hp1mb-segr-1500",
        "ddb-2s-6da-mixed-co10000000-rto15ms-cwnd30-day9900us-night100us-q50-hp1mb-1500",
        "opera-1500",
        "expander-mixed-rto15ms-q50-hp1mb-1500"
    ]
    for topo in TOPOLOGIES:
        topo_path = os.path.join(DATA_PATH, topo)
        if not os.path.exists(topo_path):
            print(f"No results for {topo}")
            continue

        topo_img_dir = os.path.join(IMG_PATH, topo)
        os.makedirs(topo_img_dir, exist_ok=True)
        cd.aggregate_all_load_data(topo_path, ignore_fct=True)

    CONFIGS_TO_COMPARE = [
        (os.path.join(DATA_PATH, "ddb-2s-6da-mixed-co10000000-rto15ms-cwnd30-day9900us-night100us-q50-hp1mb-1500"),
         (2, 6), literals.DUO, literals.COLOR_DUO, plotutils.MARKER[3]),
        (os.path.join(DATA_PATH, "expander-mixed-rto15ms-q50-hp1mb-1500"),
         (8, 0), literals.EXPANDER, literals.COLOR_EXP, plotutils.MARKER[1]),
        (os.path.join(DATA_PATH, "opera-1500"),
         (8, 0), literals.OPERA, literals.COLOR_OPERA, plotutils.MARKER[2]),
        (os.path.join(DATA_PATH, "ddb-2s-6da-mixed-co10000000-rto15ms-cwnd30-day9900us-night100us-q50-hp1mb-segr-1500"),
         (2, 6), literals.SEGR, literals.COLOR_SEGR, plotutils.MARKER[4]),
    ]

    if True:
        cd.line_total_transmitted_topos(
            folders_and_links=CONFIGS_TO_COMPARE,
            img_suffix=IMG_SUFFIX,
            img_path=IMG_PATH,
            end_time_ms=3500,
            ylim=(0, 1.6),
            yticks=np.arange(0, 1.51, 0.3),
            path_to_traffic="../traffic_gen/websearch_64racks_8hosts_10s/"
        )


if __name__ == "__main__":
    plot()
