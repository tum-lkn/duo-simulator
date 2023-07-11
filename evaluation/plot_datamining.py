import os

import pandas as pd
import numpy as np
import matplotlib.pyplot as plt

import plotutils
import literals

import collect_data as cd

# %%
DATA_PATH = f"../data/datamining/64racks_8hosts_10s/"
IMG_PATH = f"../img/datamining/64racks_8hosts_10s/"


def aggregate_num_tors(num_tors: int):
    TOPOLOGIES = [
        "opera-1500-global",
        "ddb-2s-6da-mixed-co10000000-rto15ms-cwnd30-day9900us-night100us-q50-hp0-1500",
        "ddb-2s-6da-mixed-co10000000-rto15ms-cwnd30-day9900us-night100us-q50-hp1mb-1500",
        "ddb-2s-6da-mixed-co10000000-rto15ms-cwnd30-day9900us-night100us-q50-hp100kb-1500",
        "ddb-2s-6da-mixed-co10000000-rto15ms-cwnd30-day9900us-night100us-q50-hp15mb-1500",
        "ddb-2s-6da-mixed-co10000000-rto15ms-cwnd30-day9900us-night100us-q50-hp1mb-segr-1500",
        "expander-mixed-rto15ms-q50-hp1mb-1500",

        # Run with diffseqno recorded. Takes a very long time... run only the one necessary run.
        # requires to recompile the opera source with rlb.cpp lines 165-210 commented in
        "opera-1500-diffseqno",
    ]
    for topo in TOPOLOGIES:
        print(topo)
        topo_dir = os.path.join(f"../data/datamining/{num_tors}racks_8hosts_10s/", topo)
        if not os.path.exists(topo_dir):
            print(f"{topo_dir} does not exist.")
            continue

        topo_img_dir = os.path.join(f"../img/datamining/{num_tors}racks_8hosts_10s/", topo)
        os.makedirs(topo_img_dir, exist_ok=True)
        cd.aggregate_all_load_data(topo_dir)


def plot():
    CONFIGS_TO_COMPARE = [
        (os.path.join(DATA_PATH, "ddb-2s-6da-mixed-co10000000-rto15ms-cwnd30-day9900us-night100us-q50-hp1mb-1500"),
         (2, 6), literals.DUO, literals.COLOR_DUO, plotutils.MARKER[3]),
        (os.path.join(DATA_PATH,
                      "ddb-2s-6da-mixed-co10000000-rto15ms-cwnd30-day9900us-night100us-q50-hp1mb-segr-1500-fixed"),
         (2, 6), literals.SEGR, literals.COLOR_SEGR, plotutils.MARKER[4]),
        (os.path.join(DATA_PATH, "expander-mixed-rto15ms-q50-hp1mb-1500"),
         (8, 0), literals.EXPANDER, literals.COLOR_EXP, plotutils.MARKER[1]),
        (os.path.join(DATA_PATH, "opera-1500-global"),
         (8, 0), literals.OPERA, literals.COLOR_OPERA, plotutils.MARKER[2]),
    ]

    if True:
        # FIG 5
        cd.line_total_transmitted_topos(
            folders_and_links=CONFIGS_TO_COMPARE,
            img_suffix=f"_datamining",
            img_path=IMG_PATH,
            end_time_ms=10_000,
            path_to_traffic="../traffic_gen/64racks_8hosts_10s/"
        )

    if True:
        # FIG 5 Legend
        cd.plot_legend(
            folders_and_links=CONFIGS_TO_COMPARE,
            img_suffix=f"_permutation",
            img_path=IMG_PATH
        )

    if True:
        # FIG 6
        for load in [40]:
            cd.line_fct_by_flowsize_topos(
                folders_and_links=CONFIGS_TO_COMPARE,
                img_suffix=f"_datamining",
                img_path=IMG_PATH,
                load_perc=load,
                quantile=0.99
            )

    if True:
        # FIG 7
        cd.cdf_seqno_diff_topos(
            folders_and_links=[
                (os.path.join(DATA_PATH,
                              "ddb-2s-6da-mixed-co10000000-rto15ms-cwnd30-day9900us-night100us-q50-hp1mb-1500-seqno"),
                 (2, 6), literals.DUO, literals.COLOR_DUO, plotutils.MARKER[3]),
                (os.path.join(DATA_PATH,
                              "ddb-2s-6da-mixed-co10000000-rto15ms-cwnd30-day9900us-night100us-q50-hp1mb-segr-1500-fixed"),
                 (2, 6), literals.SEGR, literals.COLOR_SEGR, plotutils.MARKER[4]),
                (os.path.join(DATA_PATH, "expander-mixed-rto15ms-q50-hp1mb-1500"), (8, 0), literals.EXPANDER,
                 literals.COLOR_EXP,
                 plotutils.MARKER[1]),
                (os.path.join(DATA_PATH, "opera-1500-diffseqno"), (8, 0), literals.OPERA, literals.COLOR_OPERA,
                 plotutils.MARKER[2]),

            ],
            load=10,
            img_path="../img/datamining/64racks_8hosts_10s/",
            img_suffix="_datamining"
        )

    if True:
        # FIG 8
        cd.line_avg_path_length_topos(
            folders_and_links=CONFIGS_TO_COMPARE,
            img_suffix=f"_datamining",
            img_path=IMG_PATH
        )

    if True:
        # FIG 10
        cd.bar_1dahop_and_imh_topos(
            # No opera
            folders_and_links=CONFIGS_TO_COMPARE[:1],
            img_suffix=f"_datamining",
            img_path=IMG_PATH
        )

    if True:
        # FIG 9
        # These values are printed during the avg. path length plot (Fig. 8). Update if needed
        AVG_PL_VALUES = {
            literals.DUO: {
                10: 1.210322,
                20: 1.213244,
                40: 1.468929,
                60: 1.581084,
                80: 1.634901
            },
            literals.SEGR: {
                20: 1.303213,
                40: 1.460809,
                60: 1.456561,
                80: 1.428524,
            },
            literals.EXPANDER: {
                20: 2.183560,
                40: 2.057751,
                60: 1.984408,
                80: 1.932681
            },
            literals.OPERA: {
                20: 1.989669,
                40: 2.000851,
                60: 2.005789,
                80: -1
            }
        }

        img_suffix = f"_datamining_20_60"
        img_path = IMG_PATH
        num_tors = 64

        plotutils.create_figure(ncols=0.66, fontsize=9, aspect_ratio=0.75)
        offset = 0
        for folder, (num_static, num_da), label, color, marker in CONFIGS_TO_COMPARE:
            if not os.path.exists(folder):
                continue
            bar_values = list()
            bar_true = list()

            for load, linestyle in [(20, '-'), (60, '--')]:
                value = 0
                print(f"Load {load}")
                fnames = filter(lambda x: f"{load}perc" in x, os.listdir(folder))
                for fname in fnames:
                    times, per_tor_util, bytes_rx, hdr_bytes_rx, dropped, bytes_rtx, \
                        _, _, hop_count, _, _ = cd.read_per_tor_utilization(os.path.join(folder, fname))

                    num_uplinks = num_da + num_static
                    data = per_tor_util
                    reshaped_array = np.zeros(shape=(num_tors * num_uplinks, len(data)))
                    for i, arr in enumerate(data):
                        if label == literals.OPERA:
                            # Opera has first uplinks, then downlinks
                            reshaped_array[:, i] = (arr[:, num_uplinks:].flatten())
                        else:
                            reshaped_array[:, i] = (arr[:, :num_uplinks].flatten())

                    avg_util_rel = np.mean(8 * reshaped_array / 1e10, axis=0)[500:]
                    value = np.mean(avg_util_rel)
                    print(label, np.mean(avg_util_rel), np.std(avg_util_rel), np.max(avg_util_rel), avg_util_rel[-1])
                bar_values.append(value)
                bar_true.append(value / AVG_PL_VALUES[label][load])

            print(label, bar_values, bar_true)
            positions = np.array(
                range(offset, len(CONFIGS_TO_COMPARE) * len(bar_values) + offset, 1 + len(CONFIGS_TO_COMPARE)))
            plt.bar(x=positions, width=0.4,
                    height=bar_values, color=color, label=label, hatch=plotutils.HATCHES[offset], edgecolor='k',
                    linewidth=0.5)

            plt.bar(x=positions + 0.4, width=0.4,
                    height=bar_true, color='k', hatch='*', edgecolor='k', linewidth=0)

            offset += 1
        plt.ylabel("Avg. Link Utilization")
        plt.xlabel("Offered Load [\%]")
        plt.xticks(
            np.arange(len(CONFIGS_TO_COMPARE) / 2 - 0.3, len(CONFIGS_TO_COMPARE) * 2 + 1, 1 + len(CONFIGS_TO_COMPARE)),
            [20, 60])
        plt.ylim(0, 1)
        plt.grid()
        plt.legend(ncol=4, fontsize=9, columnspacing=0.3, handlelength=0.8, handletextpad=0.5,
                   bbox_to_anchor=(-0.2, .93),
                   loc="lower left", frameon=False)
        plt.subplots_adjust(left=0.22, bottom=0.23, top=0.87, right=0.99)
        plt.savefig(f"{img_path}/compare_avg_link_utilization_{img_suffix}.pdf")
        plt.close()

    if True:
        # FIG 12
        cd.line_fct_by_flowsize_topos(
            folders_and_links=[
                (os.path.join(DATA_PATH,
                              "ddb-2s-6da-mixed-co10000000-rto15ms-cwnd30-day9900us-night100us-q50-hp0-1500"),
                 (2, 6), "0 B", plotutils.COLORS[0], plotutils.MARKER[3]),
                (os.path.join(DATA_PATH,
                              "ddb-2s-6da-mixed-co10000000-rto15ms-cwnd30-day9900us-night100us-q50-hp15mb-1500"),
                 (2, 6), "15 MB", plotutils.COLORS[3], plotutils.MARKER[2]),
                (os.path.join(DATA_PATH,
                              "ddb-2s-6da-mixed-co10000000-rto15ms-cwnd30-day9900us-night100us-q50-hp1mb-1500"),
                 (2, 6), "1 MB", plotutils.COLORS[2], plotutils.MARKER[1]),
                (
                    os.path.join(DATA_PATH,
                                 "ddb-2s-6da-mixed-co10000000-rto15ms-cwnd30-day9900us-night100us-q50-hp100kb-1500"),
                    (2, 6), "100 KB", plotutils.COLORS[1], plotutils.MARKER[0]),
            ],
            img_suffix=f"_datamining_threshold",
            img_path=IMG_PATH,
            load_perc=40,
            quantile=0.99
        )

    if True:
        # FIG 13
        cd.line_runtime_topos(
            folders_and_links=CONFIGS_TO_COMPARE,
            img_suffix=f"_datamining",
            img_path=IMG_PATH
        )


def aggregate_plot_comparison_config_duty_cycle():
    # FIG 11
    start_time_ms = 0
    end_time_ms = 10_000


    load = 40
    force = False
    color_idx = 0

    plotutils.create_figure(ncols=1, fontsize=10)
    norm_value = 2.346665
    for period, configs in [
        ("1ms", [(500, 500), (100, 900), (20, 980), (10, 990)]),
        ("10ms", [(5000, 5000), (1000, 9000), (200, 9800), (100, 9900)]),
        ("50ms", [(25000, 25000), (5000, 45000), (1000, 49000), (500, 49500)])
    ]:

        bar_values = list()
        for night, day in configs:
            folder = os.path.join(DATA_PATH,
                                  f"ddb-2s-6da-mixed-co10000000-rto15ms-cwnd30-day{day}us-night{night}us-q50-hp1mb-1500")
            if not os.path.exists(folder):
                bar_values.append(0)
                continue
            fname = os.path.join(folder, "aggregated_total_received.h5")
            if not os.path.exists(fname) or force:
                cd.aggregate_all_load_data(folder, ignore_fct=True)
            total_bytes_df = pd.read_hdf(fname)

            index = (total_bytes_df.time >= start_time_ms) & (total_bytes_df.time < end_time_ms)
            grouped_df = total_bytes_df[index].groupby("load_percent").max()
            print(grouped_df.time)

            bar_values.append(grouped_df.total_bytes.loc[load] / 1e12 / norm_value)

        if len(bar_values) == 0:
            continue
        print(bar_values)
        plt.bar(
            x=range(color_idx, 13 + color_idx, 4),
            height=bar_values,
            color=plotutils.COLORS[color_idx],
            label=f"{period}", hatch=plotutils.HATCHES[color_idx]
        )
        color_idx += 1

    plt.axhline(y=1.0, linestyle='--', color='k', alpha=0.5, linewidth=0.5)
    plt.ylim(0.9, 1.05)
    plt.ylabel("RX Volume")
    plt.xlabel("Duty cycle $\epsilon$")
    plt.xticks(range(1, 14, 4), ["50\%", "90\%", "98\%", "99\%"])

    plt.arrow(13, 1.02, 0, -0.015, length_includes_head=True)
    plt.text(x=13, y=1.022, s="100$\mu$s", horizontalalignment='center')

    plt.legend(frameon=False, ncol=3, loc="lower left", columnspacing=0.5, handlelength=1.2, bbox_to_anchor=(0, 0.92))
    plt.subplots_adjust(left=0.16, bottom=0.20, top=0.9, right=0.99)
    plt.savefig(f"{IMG_PATH}/compare_total_received_bytes_reconfiguration2.pdf")
    plt.close()


def plot_comparison_topology_size():
    start_time_ms = 0
    end_time_ms = 5_000
    print("topo")

    IMG_PATH = f"../img/"

    force = False
    color_idx = 0

    load = 60
    plotutils.create_figure(ncols=1, fontsize=10)

    ax_rx = plt.gca()
    SIZES = [64, 128, 256]
    norm = 1
    for topo, label, color, marker in [
        ("ddb-2s-6da-mixed-co10000000-rto15ms-cwnd30-day9900us-night100us-q50-hp1mb-1500",
         literals.DUO, literals.COLOR_DUO, plotutils.MARKER[3]),
        ("ddb-2s-6da-mixed-co10000000-rto15ms-cwnd30-day9900us-night100us-q50-hp1mb-segr-1500",
         literals.SEGR, literals.COLOR_SEGR, plotutils.MARKER[4]),
        ("expander-mixed-rto15ms-q50-hp1mb-1500", literals.EXPANDER, literals.COLOR_EXP,
         plotutils.MARKER[1]),

    ]:
        topo_total_bytes = list()
        for topo_size in SIZES:
            DATA_PATH = f"../data/datamining/{topo_size}racks_8hosts_10s/"

            folder = os.path.join(DATA_PATH, topo)
            print(folder)
            if not os.path.exists(folder):
                topo_total_bytes.append(0)
                continue
            fname = os.path.join(folder, "aggregated_total_received.h5")
            if not os.path.exists(fname) or (force and topo_size == 256):
                cd.aggregate_all_load_data(folder)
            total_bytes_df = pd.read_hdf(fname)

            index = (total_bytes_df.time >= start_time_ms) & (total_bytes_df.time < end_time_ms) & (
                        total_bytes_df.load_percent == load)
            grouped_df = total_bytes_df[index].groupby("load_percent")
            total_bytes = grouped_df.max().total_bytes

            topo_total_bytes.append(total_bytes / norm / 1e12)
            if "ddb" in topo and topo_size == 64 and "segr" not in topo:  # Use Duo as reference
                print("norm", topo_total_bytes[-1])
                norm = topo_total_bytes[-1]
                topo_total_bytes[-1] = 1

        print(topo, topo_total_bytes)
        ax_rx.plot(SIZES, topo_total_bytes, color=color, label=f"{label}", marker=marker,
                   linewidth=1)

        color_idx += 1

    ax_rx.set_ylabel("RX Volume")
    ax_rx.set_ylim(0.5, 4)
    ax_rx.set_xlabel("Topology Size")
    plt.xticks(SIZES)
    plt.grid()
    ax_rx.legend(frameon=False, ncol=3, loc="lower left", handlelength=1., columnspacing=0.3, bbox_to_anchor=(0, 0.92))
    plt.subplots_adjust(left=0.11, bottom=0.2, top=0.9, right=0.99)
    plt.savefig(f"{IMG_PATH}/compare_topology_size2_{load}.pdf")
    plt.close()


if __name__ == "__main__":
    aggregate_num_tors(64)
    plot()
    aggregate_plot_comparison_config_duty_cycle()
    plot_comparison_topology_size()
