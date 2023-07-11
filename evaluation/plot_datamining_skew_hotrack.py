import os

import plotutils
import literals
import collect_data as cd

DATA_PATH = f"../data/datamining_skew/64racks_8hosts_10s/"
IMG_PATH = f"../img/datamining_skew/64racks_8hosts_10s/"


def aggregate_skewed():
    TOPOLOGIES = [
        "ddb-2s-6da-mixed-co10000000-rto15ms-cwnd30-day9900us-night100us-q50-hp1mb-segr-1500",
        "ddb-2s-6da-mixed-co10000000-rto15ms-cwnd30-day9900us-night100us-q50-hp1000000-1500",
        "opera-1500-global",
        "expander-mixed-rto15ms-q50-hp1mb-1500"
    ]

    for topo in TOPOLOGIES:
        print(topo)
        topo_dir = os.path.join(DATA_PATH, topo)
        if not os.path.exists(topo_dir):
            print(f"{topo_dir} does not exist.")
            continue

        topo_img_dir = os.path.join(IMG_PATH, topo)
        os.makedirs(topo_img_dir, exist_ok=True)

        cd.aggregate_all_load_data(topo_dir)


def plot_skewed():
    CONFIGS_TO_COMPARE = [
        (os.path.join(DATA_PATH, "ddb-2s-6da-mixed-co10000000-rto15ms-cwnd30-day9900us-night100us-q50-hp1000000-1500"),
         (2, 6), literals.DUO, literals.COLOR_DUO, plotutils.MARKER[3]),
        (os.path.join(DATA_PATH, "expander-mixed-rto15ms-q50-hp1mb-1500"), (8, 0), literals.EXPANDER, literals.COLOR_EXP,
         plotutils.MARKER[1]),
        (os.path.join(DATA_PATH, "opera-1500-global"), (8, 0), literals.OPERA, literals.COLOR_OPERA, plotutils.MARKER[0]),
     ]

    if True:
        cd.line_total_transmitted_topos_skewed(
            folders_and_links=CONFIGS_TO_COMPARE,
            num_racks=64, hpr=8,
            img_suffix=f"_datamining_skew",
            img_path=IMG_PATH,
            end_time_ms=10_000,
            ylim=(0,3),
            path_to_traffic="../traffic_gen/datamining_skew/"
        )

    if True:
        cd.bar_1dahop_and_imh_topos_skew(
            folders_and_links=CONFIGS_TO_COMPARE[:1],
            img_suffix=f"_datamining_skew",
            img_path=IMG_PATH,
            hpr=8,
            num_racks=64
        )


if __name__ == "__main__":
    aggregate_skewed()
    plot_skewed()
