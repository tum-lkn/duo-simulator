import os

import plotutils
import literals

import collect_data as cd

def plot():
    DATA_PATH = f"../data/permutation/64racks_8hosts_10s/"
    IMG_PATH = f"../img/permutation/64racks_8hosts_10s/"
    os.makedirs(IMG_PATH, exist_ok=True)

    TOPOLOGIES = [
        "opera-1500",
        "ddb-2s-6da-mixed-co0-rto15ms-cwnd30-day9900us-night100us-q50-hp1mb-1500",
        "ddb-2s-6da-mixed-co0-rto15ms-cwnd30-day9900us-night100us-q50-hp1mb-segr-1500",
        "expander-mixed-rto15ms-q50-hp1mb-1500"
    ]
    for topo in TOPOLOGIES:
        topo_path = os.path.join(DATA_PATH, topo)
        topo_img_dir = os.path.join(IMG_PATH, topo)
        os.makedirs(topo_img_dir, exist_ok=True)

        cd.aggregate_all_load_data(topo_path)

    CONFIGS_TO_COMPARE = [
        (os.path.join(DATA_PATH, "ddb-2s-6da-mixed-co0-rto15ms-cwnd30-day9900us-night100us-q50-hp1mb-1500"),
         (2, 6), literals.DUO, literals.COLOR_DUO, plotutils.MARKER[3]),
        (os.path.join(DATA_PATH, "expander-tcp-rto15ms-q50"),
         (8, 0), literals.EXPANDER, literals.COLOR_EXP, plotutils.MARKER[1]),
        (os.path.join(DATA_PATH, "opera-1500"),
         (8, 0), literals.OPERA, literals.COLOR_OPERA, plotutils.MARKER[2]),
        (os.path.join(DATA_PATH, "ddb-2s-6da-mixed-co0-rto15ms-cwnd30-day9900us-night100us-q50-hp1mb-segr-1500"),
         (2, 6), literals.SEGR, literals.COLOR_SEGR, plotutils.MARKER[4]),
    ]

    if True:
        cd.line_total_transmitted_topos(
            folders_and_links=CONFIGS_TO_COMPARE,
            img_suffix=f"_permutation",
            img_path=IMG_PATH,
            end_time_ms=10000,
            path_to_traffic="../traffic_gen/permutation/64racks_8hosts_10s/"
        )


if __name__ == "__main__":
    plot()