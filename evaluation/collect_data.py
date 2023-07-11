from __future__ import annotations
import os
from typing import Tuple, List, Dict

import numpy as np
import pandas as pd
import matplotlib.pyplot as plt

import plotutils as plutils


def parse_fct_line_duo(line_splt: List[str], data_fct: Dict):
    data_fct.update(
        {'bytes_weighted_hops': int(line_splt[6]) if len(line_splt) > 6 else -1,
         'num_pkts': int(line_splt[7]) if len(line_splt) > 6 else -1,
         'received_bytes': int(line_splt[8]) if len(line_splt) > 6 else -1}
    )

    if len(line_splt) > 9:
        hop_count_bytes = dict()
        for i, bytes in enumerate(line_splt[9].split(",")[:-1]):
            hop_count_bytes[f"bytes_hops_{i}"] = int(bytes)
        data_fct.update(hop_count_bytes)

        hop_count = dict()
        for i, pkts in enumerate(line_splt[10].split(",")[:-1]):
            hop_count[f"pkts_hops_{i}"] = int(pkts)
        data_fct.update(hop_count)

    if len(line_splt) > 11:
        # We have RTX data
        data_fct.update({
            "nacks": int(line_splt[11].split("=")[1]),
            "rtx_after_nack": int(line_splt[12].split("=")[1]),
            "rtx_received_pkts": int(line_splt[13].split("=")[1]),
            "rtx_received_bytes": int(line_splt[14].split("=")[1]),
        })
    if len(line_splt) > 15:
        # We have strip location data
        strip_count_pkts = dict()
        for i, pkts in enumerate(line_splt[15][9:].split(",")[:-1]):
            # Add 2 here since hop count starts with -1 in simulation (for easier routing)
            strip_count_pkts[f"pkts_stripped_at_hop_{i}"] = int(pkts)
        data_fct.update(strip_count_pkts)

    if len(line_splt) > 16:
        # hops_da=0,0,0,0,0,0,0,0,0,0, hops_static=0,0,0,0,0,251,0,0,0,0, redirected_bytes=0
        strip_count_pkts = dict()
        for i, pkts in enumerate(line_splt[16][8:].split(",")[:-1]):
            # Add 2 here since hop count starts with -1 in simulation (for easier routing)
            strip_count_pkts[f"bytes_da_hops_{i}"] = int(pkts)
        data_fct.update(strip_count_pkts)

        strip_count_pkts = dict()
        for i, pkts in enumerate(line_splt[17][12:].split(",")[:-1]):
            # Add 2 here since hop count starts with -1 in simulation (for easier routing)
            strip_count_pkts[f"bytes_static_hops_{i}"] = int(pkts)
        data_fct.update(strip_count_pkts)

        data_fct.update({
            "bytes_redirected": int(line_splt[18].split("=")[1])
        })
    if len(line_splt) > 19:
        # We have RTX data
        data_fct.update({
            "num_rtos": int(line_splt[19].split("=")[1]),
            "num_dupacks": int(line_splt[20].split("=")[1])
        })
    if len(line_splt) > 21:
        data_fct["bytes_imh"] = int(line_splt[21].split("=")[1])

    if len(line_splt) > 22:
        data_fct["bytes_1dahop"] = int(line_splt[22].split("=")[1])
    if len(line_splt) > 23:
        data_fct["t_handshake"] = float(line_splt[23].split("=")[1])

    if len(line_splt) == 26:
        # NDP flow
        data_fct["seqno_diff"] = float(line_splt[23].split("=")[1])
        data_fct["min_seqno_diff"] = float(line_splt[24].split("=")[1])
        data_fct["max_seqno_diff"] = float(line_splt[25].split("=")[1])
    elif len(line_splt) == 27:
        # TCP flow
        data_fct["seqno_diff"] = float(line_splt[24].split("=")[1])
        data_fct["min_seqno_diff"] = float(line_splt[25].split("=")[1])
        data_fct["max_seqno_diff"] = float(line_splt[26].split("=")[1])


def parse_fct_line_expander(line_splt: List[str], data_fct: Dict):
    data_fct.update(
        {'bytes_weighted_hops': int(line_splt[6]) if len(line_splt) > 6 else -1,
         'num_pkts': int(line_splt[7]) if len(line_splt) > 6 else -1,
         'received_bytes': int(line_splt[8]) if len(line_splt) > 6 else -1}
    )

    if len(line_splt) > 9:
        hop_count_bytes = dict()
        for i, bytes in enumerate(line_splt[9].split(",")[:-1]):
            hop_count_bytes[f"bytes_hops_{i-1}"] = int(bytes)
        data_fct.update(hop_count_bytes)

        hop_count = dict()
        for i, pkts in enumerate(line_splt[10].split(",")[:-1]):
            hop_count[f"pkts_hops_{i-1}"] = int(pkts)
        data_fct.update(hop_count)

        # We have RTX data
        data_fct.update({
            "nacks": int(line_splt[11].split("=")[1]),
            "rtx_after_nack": int(line_splt[12].split("=")[1]),
            "rtx_received_pkts": int(line_splt[13].split("=")[1]),
            "rtx_received_bytes": int(line_splt[14].split("=")[1]),
        })

        # We have strip location data
        # stripped=0,0,0,0,0,0,0,0,0,0,
        strip_count_pkts = dict()
        for i, pkts in enumerate(line_splt[15][9:].split(",")[:-1]):
            # Add 2 here since hop count starts with -1 in simulation (for easier routing)
            strip_count_pkts[f"pkts_stripped_at_hop_{i}"] = int(pkts)
        data_fct.update(strip_count_pkts)

        # We have RTX data
        data_fct.update({
            "num_rtos": int(line_splt[16].split("=")[1]),
            "num_dupacks": int(line_splt[17].split("=")[1])
        })


def parse_fct_line_opera(line_splt: List[str], data_fct: Dict):
    if len(line_splt) > 6:
        hop_count_bytes = dict()
        for i, bytes in enumerate(line_splt[6].split(",")[:-1]):
            # Add 2 here since hop count starts with -1 in simulation (for easier routing)
            hop_count_bytes[f"bytes_hops_{i-1}"] = int(bytes)
        data_fct.update(hop_count_bytes)

        hop_count = dict()
        for i, pkts in enumerate(line_splt[7].split(",")[:-1]):
            # Add 2 here since hop count starts with -1 in simulation (for easier routing)
            hop_count[f"pkts_hops_{i-1}"] = int(pkts)
        data_fct.update(hop_count)

    if len(line_splt) == 11:
        # NDP flow
        data_fct["seqno_diff"] = float(line_splt[8].split("=")[1])
        data_fct["min_seqno_diff"] = float(line_splt[9].split("=")[1])
        data_fct["max_seqno_diff"] = float(line_splt[10].split("=")[1])


def read_fcts_and_utilization(data_fname: str) -> Tuple[pd.DataFrame, pd.DataFrame, pd.DataFrame, pd.DataFrame,
                                                        int, pd.DataFrame, pd.DataFrame]:
    data_util = list()
    data_fct = list()
    data_da_links = list()

    data_total_bytes_received = list()
    seqno_data = list()

    da_link_types = list()
    last_util_time = 0
    current_time_shortcuts = 0
    current_time_default = 0
    current_time_random = 0

    wallclock_start = None
    wallclock_end = None
    with open(data_fname, "r") as fd:
        for line in fd.readlines():
            if line.startswith("Starttime"):
                wallclock_start = int(line.split(" ")[1])
            elif line.startswith("Endtime"):
                wallclock_end = int(line.split(" ")[1])
            elif line.startswith("FCT"):
                line_splt = line.split(" ")
                data_fct.append(
                    {'src': int(line_splt[1]),
                     'dst': int(line_splt[2]),
                     'size': int(line_splt[3]),
                     'fct': float(line_splt[4]),
                     't_arrival': float(line_splt[5])}
                )
                if "ddb" in data_fname:
                    parse_fct_line_duo(line_splt, data_fct[-1])
                elif "exp" in data_fname:
                    parse_fct_line_expander(line_splt, data_fct[-1])
                else:  # Opera
                    parse_fct_line_opera(line_splt, data_fct[-1])

            elif line.startswith("Util"):
                line_splt = line.split(" ")
                data_util.append(
                    {'utilization': float(line_splt[1]), 'time': float(line_splt[2])}
                )

                # Also new timestamp for link counting
                da_link_types.append({
                    'time': last_util_time, 'shortcuts': current_time_shortcuts, 'default': current_time_default,
                    'random': current_time_random
                })
                last_util_time = data_util[-1]["time"]
                current_time_default = 0
                current_time_shortcuts = 0
                current_time_random = 0
            elif line.startswith("DA-Link"):
                # DA-Link Connected 61 20 OCS-0 22.000000 Queuesize: 0
                line_splt = line.split(" ")
                data_da_links.append(
                    {'src': int(line_splt[2]), 'dst': int(line_splt[3]), 'via': line_splt[4],
                     'time': float(line_splt[5]),
                     'type': line_splt[1], 'no_link_change': 1 if line_splt[1] == "Connected" else -1,
                     'lost': int(line_splt[7]) if len(line_splt) > 7 else 0}
                )
            elif line.startswith("Found shortcut"):
                current_time_shortcuts += 1
            elif line.startswith("Setting default link"):
                current_time_default += 1
            elif line.startswith("Setting random link"):
                current_time_random += 1
            elif line.startswith("GlobalReceivedBytes"):
                data_total_bytes_received.append({
                    "time": float(line.split(" ")[1]),
                    "total_bytes": float(line.split(" ")[2])
                })
            elif line.startswith("diff_seqno_distribution"):
                rel_line = line.split("=")[1]
                seqno_data = [
                    {'seqno_diff': int(x.split(":")[0]), 'count': int(x.split(":")[1])} for x in rel_line.strip("\n").split(",")[:-1]
                ]
            else:
                pass

    df_fct = pd.DataFrame.from_records(data_fct)
    df_util = pd.DataFrame.from_records(data_util)
    df_da_links = pd.DataFrame.from_records(data_da_links)
    df_da_types = pd.DataFrame.from_records(da_link_types)
    df_total_bytes_received = pd.DataFrame.from_records(data_total_bytes_received)
    df_seqno = pd.DataFrame.from_records(seqno_data)
    if len(df_seqno) > 0:
        df_seqno["cumsum"] = df_seqno["count"].cumsum()

    if wallclock_start is not None and wallclock_end is not None:
        wallclock_duration = wallclock_end - wallclock_start
    else:
        wallclock_duration = -1

    return df_fct, df_util, df_da_links, df_da_types, wallclock_duration, df_total_bytes_received, df_seqno


def read_per_tor_utilization(data_fname: str):
    data_util = list()
    header_util = list()
    data_bytes_per_dst = list()
    header_bytes_per_dst = list()
    data_pkts_dropped = list()
    sending_host_util = list()
    sending_host_header_util = list()
    data_bytes_rtx = list()
    packets_with_large_hopcnt = list()
    hop_count = list()
    times = list()

    current_array_util = None
    current_array_header_util = None
    current_array_hopcount = None
    current_array_rtx = None
    with open(data_fname, "r") as fd:
        for line in fd.readlines():
            if line.startswith("Util"):
                line_splt = line.split(" ")
                times.append(float(line_splt[2]))
                if current_array_util is not None:
                    data_util.append(np.array(current_array_util))
                if current_array_header_util is not None:
                    header_util.append(np.array(current_array_header_util))
                if current_array_hopcount is not None:
                    hop_count.append(np.array(current_array_hopcount))
                current_array_util = list()
                current_array_header_util = list()
                current_array_hopcount = list()
            elif line.startswith("T") and not line.startswith("Traffic"):
                # DA-Link Connected 61 20 OCS-0 22.000000
                line_splt = line.split(" ")
                current_line = list()
                for elem in line_splt[1:-1]:
                    current_line.append(int(elem.split("=")[1])*1e2)  # Account for 10ms sampling
                current_array_util.append(current_line)
            elif line.startswith("HT"):
                # HT0 Q0=0 Q1=64 Q2=192 Q3=192 Q4=0 Q5=64 Q6=192 Q7=192 Q8=128 Q9=64
                line_splt = line.split(" ")
                current_line = list()
                for elem in line_splt[1:-1]:
                    current_line.append(int(elem.split("=")[1])*1e2)  # Account for 10ms sampling
                current_array_header_util.append(current_line)
            elif line.startswith("HC"):
                # CT0 Q0=0 Q1=64 Q2=192 Q3=192 Q4=0 Q5=64 Q6=192 Q7=192 Q8=128 Q9=64
                line_splt = line.split(" ")
                current_line = list()
                for elem in line_splt[1:-1]:
                    current_line.append(float(elem.split("=")[1]))  # Account for 10ms sampling
                current_array_hopcount.append(current_line)
            elif line.startswith("RTX Utilization"):
                if current_array_rtx is not None:
                    data_bytes_rtx.append(np.array(current_array_rtx))
                current_array_rtx = list()
            elif line.startswith("RT"):
                # DA-Link Connected 61 20 OCS-0 22.000000
                line_splt = line.split(" ")
                current_line = list()
                for elem in line_splt[1:-1]:
                    current_line.append(int(elem.split("=")[1])*1e2)  # Account for 10ms sampling
                current_array_rtx.append(current_line)
            elif line.startswith("DataBytesReceivedAtDstToR"):
                # BytesReceivedAtDstToR 700.000000 0=16156928 1=214316...
                line_splt = line.split(" ")
                current_line = list()
                for elem in line_splt[2:-1]:
                    current_line.append(int(elem.split("=")[1]))
                data_bytes_per_dst.append(current_line)
            elif line.startswith("HeaderBytesReceivedAtDstToR"):
                # BytesReceivedAtDstToR 700.000000 0=16156928 1=214316...
                line_splt = line.split(" ")
                current_line = list()
                for elem in line_splt[2:-1]:
                    current_line.append(int(elem.split("=")[1]))
                header_bytes_per_dst.append(current_line)
            elif line.startswith("PktsToHostsDropped"):
                # PktsToHostsDropped 700.000000 0=16156928 1=214316...
                line_splt = line.split(" ")
                current_line = list()
                for elem in line_splt[2:-1]:
                    current_line.append(int(elem.split("=")[1]))
                data_pkts_dropped.append(current_line)
            elif line.startswith("SendingHostsPipeUsageData"):
                # SendingHostsPipeUsageData 3330.000000 H0=1201492 H1=4437000 H2=0 H3=4113244
                line_splt = line.split(" ")
                current_line = list()
                for elem in line_splt[2:-1]:
                    current_line.append(int(elem.split("=")[1]))
                sending_host_util.append(current_line)
            elif line.startswith("SendingHostsPipeUsageHeader"):
                # SendingHostsPipeUsageData 3330.000000 H0=1201492 H1=4437000 H2=0 H3=4113244
                line_splt = line.split(" ")
                current_line = list()
                for elem in line_splt[2:-1]:
                    current_line.append(int(elem.split("=")[1]))
                sending_host_header_util.append(current_line)
            elif line.startswith("MaxHopReached"):
                continue
                # MaxHopReached 10794688 319-102 9000 11
                line_splt = line.split(" ")
                packets_with_large_hopcnt.append({
                    "time": times[-1],
                    "id": int(line_splt[1]),
                    "src": line_splt[2].split("-")[0],
                    "dst": line_splt[2].split("-")[1],
                    "size": int(line_splt[3]),
                    "hops": int(line_splt[4])
                })
            else:
                pass
        data_util.append(np.array(current_array_util))
        if current_array_header_util is not None:
            header_util.append(np.array(current_array_header_util))
        if current_array_hopcount is not None:
            hop_count.append(np.array(current_array_hopcount))
        if current_array_rtx is not None:
            data_bytes_rtx.append(np.array(current_array_rtx))
    return times, data_util, data_bytes_per_dst, header_bytes_per_dst, data_pkts_dropped, \
        data_bytes_rtx, pd.DataFrame.from_records(packets_with_large_hopcnt), header_util, hop_count, \
        sending_host_util, sending_host_header_util


def line_total_transmitted_topos(folders_and_links, img_path, img_suffix="", start_time_ms: int = 0,
                                 end_time_ms: int = 5000, path_to_traffic: str = None, ylim = None, yticks = None) -> None:
    plutils.create_figure(ncols=1, fontsize=10)

    for folder, (num_static, num_da), label, color, marker in folders_and_links:
        if not os.path.exists(folder):
            continue
        total_bytes_df = pd.read_hdf(os.path.join(folder, "aggregated_total_received.h5"))

        index = (total_bytes_df.time >= start_time_ms) & (total_bytes_df.time < end_time_ms)
        grouped_df = total_bytes_df[index].groupby("load_percent").max()

        plt.plot(
            grouped_df.total_bytes / 1e12, label=label, color=color, marker=marker,
        )
        print(label, grouped_df.total_bytes)

    # Offered as reference
    if path_to_traffic is not None:
        offered_loads = list()
        offered_total = list()
        for fname in filter(lambda x: x.endswith(".htsim"), sorted(os.listdir(path_to_traffic))):
            idx = fname.find("percLoad")
            offered_loads.append(int(fname[idx-2:idx]))
            traffic_data = pd.read_csv(os.path.join(path_to_traffic, fname), sep=" ", header=None)
            traffic_data.columns = ["src", "dst", "flowsize", "t_arrival"]

            index = (traffic_data.t_arrival / 1e6 >= start_time_ms) & (traffic_data.t_arrival / 1e6 < end_time_ms)
            offered_total.append(traffic_data[index].sum().flowsize)
            print("Offered", offered_loads[-1], traffic_data.sum().flowsize)

        plt.plot(offered_loads, np.array(offered_total) / 1e12, color='k', linestyle='--', label="Offered")

    plt.ylabel("RX Volume [TB]")
    plt.xlabel("Offered Load [\%]")
    plt.yticks(range(0, 6))
    if ylim is not None:
        plt.ylim(*ylim)
    if yticks is not None:
        plt.yticks(yticks)
    plt.grid()
    plt.subplots_adjust(left=0.12, bottom=0.2, top=0.99, right=0.99)
    plt.savefig(f"{img_path}/compare_total_received_bytes{img_suffix}.pdf")
    plt.close()


def line_total_transmitted_topos_skewed(folders_and_links, num_racks: int, hpr: int, img_path, img_suffix="",
                                        start_time_ms: int = 0, end_time_ms: int = 5000, path_to_traffic: str = None,
                                        ylim = None, yticks = None, normalize: bool = False) -> None:
    plutils.create_figure(ncols=1, fontsize=10)

    # Offered as reference
    if path_to_traffic is not None and False:
        offered_loads = list()
        offered_total = list()
        for fname in filter(lambda x: x.endswith(".htsim"), sorted(os.listdir(path_to_traffic))):
            idx = fname.find("hosts")
            offered_loads.append(int(fname[idx - 3:idx].strip("_")))
            traffic_data = pd.read_csv(os.path.join(path_to_traffic, fname), sep=" ", header=None)
            traffic_data.columns = ["src", "dst", "flowsize", "t_arrival"]

            index = (traffic_data.t_arrival / 1e6 >= start_time_ms) & (traffic_data.t_arrival / 1e6 < end_time_ms)
            offered_total.append(traffic_data[index].sum().flowsize)
            print("Offered", offered_loads[-1], traffic_data.sum().flowsize)

        plt.plot(offered_loads, np.array(offered_total) / 1e12, color='k', linestyle='--', label="Offered")

    xticks_raw = set()
    for folder, (num_static, num_da), label, color, marker in folders_and_links:
        if not os.path.exists(folder):
            continue
        total_bytes_df = pd.read_hdf(os.path.join(folder, "aggregated_total_received.h5"))

        index = (total_bytes_df.time >= start_time_ms) & (total_bytes_df.time < end_time_ms)
        grouped_df = total_bytes_df[index].groupby("load_percent").max()

        if path_to_traffic is not None and normalize:
            min_len = min(len(offered_total), len(grouped_df.total_bytes))
            plt.plot(
                grouped_df.total_bytes[:min_len] / np.array(offered_total[:min_len]), label=label, color=color,
                marker=marker
            )
            print(label, grouped_df.total_bytes[:min_len] / np.array(offered_total[:min_len]))
        else:
            plt.plot(
                grouped_df.total_bytes / 1e12, label=label, color=color, marker=marker
            )
            print(label, grouped_df.total_bytes)
        xticks_raw.update(grouped_df.index)

    xticks_raw_sorted = list(sorted(xticks_raw))
    xticks_converted = list()
    for xtick in xticks_raw_sorted:
        xticks_converted.append(
            int(np.round(xtick / hpr / num_racks, decimals=1) * 100)
        )

    plt.xticks(xticks_raw_sorted, xticks_converted)
    plt.ylabel("RX Volume [TB]")
    plt.xlabel("Active Racks [\%]")
    plt.yticks(range(0, 6))
    if ylim is not None:
        plt.ylim(*ylim)
    if yticks is not None:
        plt.yticks(yticks)
    plt.grid()

    plt.legend(ncol=3, fontsize=9, handlelength=0.8, bbox_to_anchor=(0, 0.95), loc="lower left", frameon=False)
    plt.subplots_adjust(left=0.12, bottom=0.2, top=0.9, right=0.99)
    plt.savefig(f"{img_path}/compare_total_received_bytes{img_suffix}.pdf")
    plt.close()


def cdf_seqno_diff_topos(load: float, folders_and_links, img_path, img_suffix="") -> None:
    fig, ax = plutils.create_figure(ncols=1, fontsize=10)
    ax.remove()
    (ax1, ax2) = fig.subplots(1, 2, gridspec_kw={'width_ratios': [1, 5]})

    for folder, (num_static, num_da), label, color, marker in folders_and_links:
        fname = os.path.join(folder, "aggregated_diff_seqno.h5")
        if not os.path.exists(fname):
            continue
        df = pd.read_hdf(fname).set_index("load_percent")
        try:
            df = df.loc[load]
        except KeyError:
            continue
        print(label)
        df = df[df["seqno_diff"] > -1e9]

        df_lt0 = df[df["seqno_diff"] < 0]
        df_eq0 = df[df["seqno_diff"] == 0]
        df_gt0 = df[df["seqno_diff"] > 0].sort_values("seqno_diff")
        df_grouped = df.sort_values("seqno_diff")
        total = df_grouped["count"].sum()

        print(df["seqno_diff"].min(), df["seqno_diff"].max(), df_lt0["count"].sum()/total, df_eq0["count"].sum()/total)
        y_lt0 = df_lt0["count"].sum()/total
        y_eq0 = df_eq0["count"].sum()/total
        offset = y_lt0 + y_eq0

        ax1.plot([-1, 0, 0.3], [y_lt0, y_eq0+y_lt0,  y_eq0+y_lt0], color=color, label=label)
        bins = list(range(1, 40_000, 4_000))
        print(df_gt0.groupby(pd.cut(df_gt0["count"], bins)).sum()["count"]/total, len(bins))
        ax2.plot(df_gt0["seqno_diff"], offset + df_gt0["count"].cumsum() / total ,
                 color=color, label=label, marker=marker, markevery=(20, 10000))

    ax1.spines.right.set_visible(False)
    ax2.spines.left.set_visible(False)
    ax1.set_xlim(-1.1, 0.2)
    ax1.set_xticks([-1, 0])
    ax1.set_xticklabels(["$<$0", "0"])
    ax2.tick_params(left=False, labelleft=False)
    ax1.set_ylim(-0.02, 1.1)
    ax2.set_ylim(-0.02, 1.1)

    ax2.set_xscale("log")
    ax1.set_ylabel("CDF")
    plt.xlabel("Seqno. difference [segments]")

    ax2.set_xlim(0.9, 3.5e4)
    ax2.grid("both")
    ax1.grid("both")

    d = .2  # proportion of vertical to horizontal extent of the slanted line
    kwargs = dict(marker=[(-d, -0.5), (d, 0.5)], markersize=6,
                  linestyle="none", color='k', mec='k', mew=1, clip_on=False)
    ax1.plot([1, 1], [0, 1], transform=ax1.transAxes, **kwargs)
    ax2.plot([-0, -0], [0, 1], transform=ax2.transAxes, **kwargs)

    plt.legend(ncol=4, fontsize=10, columnspacing=0.4, handlelength=0.5,
               bbox_to_anchor=(-0.05, 0.95), loc="lower left", frameon=False, handletextpad=0.3)
    plt.subplots_adjust(left=0.16, bottom=0.2, top=0.9, right=0.99, wspace=0.03)

    plt.savefig(f"{img_path}/cdf_compare_segnodiff{img_suffix}.pdf")
    plt.close()


def plot_legend(folders_and_links, img_path, img_suffix: str = "", offered: bool = True):
    plutils.create_figure(ncols=2, fontsize=8)

    if offered:
        plt.plot([0], [1], color='k', linestyle='--', label="Offered", linewidth=0.75)

    for folder, (num_static, num_da), label, color, marker in folders_and_links:
        plt.plot(
            [0], [1], label=label, color=color, marker=marker, linewidth=0.75
        )

    legend = plt.legend(ncol=6, loc="lower left", bbox_to_anchor=(-0.25, 1.05), frameon=False,  handlelength=1., handletextpad=0.2, columnspacing=1.5)
    fig = legend.figure
    fig.canvas.draw()
    bbox = legend.get_window_extent().transformed(fig.dpi_scale_trans.inverted())

    fig.subplots_adjust(left=0.1, bottom=-0.4, right=0.8, top=0.85)

    fig.savefig(f"{img_path}/compare_legend{img_suffix}.pdf", bbox_inches=bbox)
    plt.close()


def line_runtime_topos(folders_and_links, img_path, img_suffix="") -> None:
    plutils.create_figure(ncols=1, fontsize=10)

    for folder, (num_static, num_da), label, color, marker in folders_and_links:
        if not os.path.exists(folder):
            continue
        total_bytes_df = pd.read_json(os.path.join(folder, "aggregated_runtime.json"))

        grouped_df = total_bytes_df.groupby("load_percent").sum()
        if "opera" in label.lower() or "1.5" not in label:
            print(label)
            plt.plot(
                grouped_df.duration/3600, label=label, color=color, marker=marker,
                linewidth=1,
                markersize=5
            )
        else:
            plt.plot(
                grouped_df.duration/3600, color=color, marker=marker, linestyle="--",
                linewidth=1,
                markersize=5
            )
        print(label, grouped_df.duration/3600)
    plt.ylabel("Runtime [h]")
    plt.xlabel("Offered Load [\%]")
    plt.grid()
    plt.legend(ncol=5, handlelength=1., handletextpad=0.2, columnspacing=0.6, frameon=False, loc="lower left",
               bbox_to_anchor=(-0.15, 0.92))
    plt.subplots_adjust(left=0.13, bottom=0.2, top=0.9, right=0.99)
    plt.savefig(f"{img_path}/compare_runtime{img_suffix}.pdf")
    plt.close()


def line_fct_by_flowsize_topos(folders_and_links, img_path, load_perc, img_suffix="", quantile: float = 0.5,
                               metric: str = "fct", ylabel: str = "FCT") -> None:
    plutils.create_figure(ncols=1, fontsize=10)

    if quantile == 0.5:
        label_prefix = "Median"
    else:
        label_prefix = f"{int(quantile*100)}\%-ile"

    all_flowsizes = list()
    for folder, (num_static, num_da), label, color, marker in folders_and_links:
        if not os.path.exists(folder):
            continue
        fname = os.path.join(folder, "aggregated_fct.h5")
        fct_data = pd.read_hdf(fname).set_index(["load_percent"]).sort_index()

        try:
            data = fct_data.loc[load_perc]
        except KeyError:
            print(f"Load {load_perc} not found for {label}")
            continue

        fct_data_grouped = data.groupby("size")
        q99_fcts = fct_data_grouped.quantile(q=quantile)[metric].to_dict()
        flowsizes = sorted(list(q99_fcts.keys()))
        plt.plot(
            flowsizes,
            [q99_fcts.get(v, 0)*1e3 for v in flowsizes],
            label=f"{label}",
            marker=marker,
            color=color,
            linewidth=1,
            markersize=5
        )
        print(flowsizes, [q99_fcts.get(v, 0)*1e3 for v in flowsizes])
        all_flowsizes += flowsizes

    sorted_all_flowsizes = np.sort(np.unique(all_flowsizes))

    min_num_queues = 2
    min_num_links = 4
    packet_sizes = np.zeros(shape=(len(sorted_all_flowsizes)))
    for i, flowsize in enumerate(sorted_all_flowsizes):
        packet_sizes[i] = min(flowsize, 1436)

    # Taken from Opera Simulator Code
    ideal_latency = 1e6 * ((sorted_all_flowsizes + 64) * 8 / 10e9 +  # flow serialization at NIC
            (min_num_queues-1) * (packet_sizes + 64) * 8 / 10e9 +  # packet serialization at queues
            min_num_queues * 64 * 8 / 10e9 +  # ACK serialization
            min_num_links * 500e-9)  # link delay

    plt.plot(
        sorted_all_flowsizes,
        ideal_latency,
        linestyle='--', color='k', alpha=0.5
    )

    plt.xscale("log")
    plt.xlabel("Flowsize [bytes]")
    plt.yscale("log")
    plt.ylabel(f"{label_prefix} {ylabel} [$\mu$s]")
    plt.xlim(200, 1.5e9)
    plt.grid()
    plt.legend(ncol=4, loc="lower left", bbox_to_anchor=(-0.3, 0.92), frameon=False, columnspacing=0.5, handlelength=1)
    plt.subplots_adjust(left=0.16, bottom=0.2, top=0.9, right=0.99)
    plt.savefig(os.path.join(img_path, f"compare_fct_p{int(quantile*100)}_by_flow_size{img_suffix}_{load_perc}.pdf"))
    plt.close()


def calc_avg_path_length(data):
    bytes_weighted_hops_sum = data.filter(like="bytes_hops").sum()
    bytes_weighted_hops_sum /= np.sum(bytes_weighted_hops_sum)

    avg_path_length = 0
    for i in range(1, 10):
        if f"bytes_hops_{i}" not in bytes_weighted_hops_sum.index:
            continue
        avg_path_length += bytes_weighted_hops_sum[f"bytes_hops_{i}"] * i
    return avg_path_length


def calc_avg_path_length_opera(data):
    small_flows = data[data["size"] < 15000000]
    large_flows = data[data["size"] >= 15000000]

    total_small = small_flows["size"].sum()
    total_large = large_flows["size"].sum()
    frac_small = total_small/(total_small+total_large)
    frac_large = total_large/(total_small+total_large)

    print(f"Fraction small: {frac_small}")
    print(f"Fraction large: {frac_large}")

    avg_pl_small = calc_avg_path_length(small_flows)
    avg_pl_large = calc_avg_path_length(large_flows)
    print(f"Avg. path length small: {avg_pl_small}")
    print(f"Avg. path length large: {avg_pl_large}")

    not_corrected = avg_pl_small*frac_small+avg_pl_large*frac_large
    corrected = (avg_pl_small+1)*frac_small+avg_pl_large*frac_large
    print(f"Without correction: {not_corrected}; Corrected: {corrected}")
    return corrected


def line_avg_path_length_topos(folders_and_links, img_path, img_suffix="") -> None:
    plutils.create_figure(ncols=0.66, fontsize=9, aspect_ratio=0.75)

    for folder, (num_static, num_da), label, color, marker in folders_and_links:
        if not os.path.exists(folder):
            continue
        link_data = pd.read_hdf(os.path.join(folder, "aggregated_fct.h5"))
        if "opera" in label.lower():
            avg_path_length = link_data.groupby("load_percent").apply(calc_avg_path_length_opera)
        else:
            avg_path_length = link_data.groupby("load_percent").apply(calc_avg_path_length)

        plt.plot(avg_path_length.index, avg_path_length, color=color, marker=marker, label=label,
                 linewidth=1,
                 markersize=5)
        print(label, avg_path_length)

    plt.ylabel("Avg. path length")
    plt.xlabel("Offered Load [\%]")
    plt.ylim(1, 2.5)
    plt.grid()
    plt.legend(ncol=4, columnspacing=0.5, handlelength=0.8, bbox_to_anchor=(-0.23, .92), handletextpad=0.3,
               loc="lower left", frameon=False)
    plt.subplots_adjust(left=0.2, bottom=0.23, top=0.89, right=0.99)
    plt.savefig(f"{img_path}/compare_avg_path_length{img_suffix}.pdf")
    plt.close()


def bar_1dahop_and_imh_topos(folders_and_links, img_path, img_suffix="") -> None:
    def calc_1h_da(data):
        return data["bytes_1dahop"].sum()/data["received_bytes"].sum()

    def calc_imh(data):
        return data["bytes_imh"].sum()/data["received_bytes"].sum()

    assert len(folders_and_links) == 1
    for folder, (num_static, num_da), label, color, marker in folders_and_links:
        if not os.path.exists(folder):
            continue
        link_data = pd.read_hdf(os.path.join(folder, "aggregated_fct.h5"))

        grouped = link_data.groupby("load_percent")
        imh_data = grouped.apply(calc_imh)
        onedahop_data = grouped.apply(calc_1h_da)

        plutils.create_figure(ncols=0.66, fontsize=9, aspect_ratio=0.75)

        positions = list(range(len(imh_data)))
        width = 0.8
        plt.bar(positions, imh_data, width, label='IMH', color=plutils.COLORS[0])
        bottom0 = imh_data.values

        print("IMH", imh_data.values)

        plt.bar(positions, onedahop_data, width, bottom=bottom0, label="1 hop DA", color=plutils.COLORS[1],
                hatch="//")
        print("1da hop", onedahop_data.values)
        bottom1 = imh_data.values+onedahop_data.values
        plt.bar(positions, 1-imh_data-onedahop_data, width, bottom=bottom1, label="Rest", color=plutils.COLORS[2],
                hatch='*')
        print("Rest", 1-imh_data-onedahop_data)
        plt.ylabel("Frac. of traffic")
        plt.xlabel("Offered Load [\%]")
        plt.xticks(positions, imh_data.index)
        plt.legend(ncol=3, frameon=False, bbox_to_anchor=(-0.2, 0.92), loc="lower left", columnspacing=0.5,
                   handlelength=1, fontsize=9)

        plt.subplots_adjust(left=0.2, bottom=0.23, top=0.89, right=0.99)
        plt.savefig(f"{img_path}/compare_bar_frac_onedahop_imh{img_suffix}.pdf")
        plt.close()


def bar_1dahop_and_imh_topos_skew(folders_and_links, img_path: str, img_suffix: str = "",
                                  hpr:int = 8, num_racks: int = 64) -> None:
    def calc_1h(data):
        return data["bytes_hops_1"].sum()/data["received_bytes"].sum()

    def calc_1h_da(data):
        return data["bytes_1dahop"].sum()/data["received_bytes"].sum()

    def calc_imh(data):
        return data["bytes_imh"].sum()/data["received_bytes"].sum()

    assert len(folders_and_links) == 1
    for folder, (num_static, num_da), label, color, marker in folders_and_links:
        if not os.path.exists(folder):
            continue

        link_data = pd.read_hdf(os.path.join(folder, "aggregated_fct.h5"))

        grouped = link_data.groupby("load_percent")
        imh_data = grouped.apply(calc_imh)
        onedahop_data = grouped.apply(calc_1h_da)
        plutils.create_figure(ncols=1, fontsize=10)

        positions = list(range(len(imh_data)))
        width = 0.8
        plt.bar(positions, imh_data, width, label='IMH', color=plutils.COLORS[0])
        bottom0 = imh_data.values

        print("IMH", imh_data.values)

        plt.bar(positions, onedahop_data, width, bottom=bottom0, label="1 hop DA", color=plutils.COLORS[1],
                hatch="//")
        print("1da hop", onedahop_data.values)
        bottom1 = imh_data.values+onedahop_data.values
        plt.bar(positions, 1-imh_data-onedahop_data, width, bottom=bottom1, label="Rest", color=plutils.COLORS[2],
                hatch='*')
        print("Rest", 1-imh_data-onedahop_data)

        xticks_raw_sorted = imh_data.index
        xticks_converted = list()
        for xtick in xticks_raw_sorted:
            xticks_converted.append(
                int(np.round(xtick / hpr / num_racks, decimals=1) * 100)
            )
        plt.xticks(positions, xticks_converted)
        plt.ylabel("Frac. of traffic")
        plt.xlabel("Active Racks [\%]")

        plt.legend(ncol=3, frameon=False, bbox_to_anchor=(0, 0.95), loc="lower left", columnspacing=0.5,
                   handlelength=1, fontsize=9)
        plt.subplots_adjust(left=0.16, bottom=0.20, top=0.9, right=0.99)
        plt.savefig(f"{img_path}/compare_bar_frac_onedahop_imh{img_suffix}.pdf")
        plt.close()


def get_data_files(data_path: str) -> List[str]:
    return list(filter(lambda x: x.endswith(".txt"), os.listdir(data_path)))


def aggregate_all_load_data(data_path: str, get_data_files_fn: callable = get_data_files, ignore_fct: bool = False
                            ) -> None:
    all_load_fct = list()
    all_load_util = list()
    all_load_dalinks = list()
    all_datypes = list()
    all_durations = list()
    all_total_received = list()
    all_seqno = list()
    for fname in get_data_files_fn(data_path):
        print(fname)
        df_fct, df_util, df_dalinks, df_datypes, duration, total_bytes, seqno = read_fcts_and_utilization(
            os.path.join(data_path, fname))
        load_perc = int(fname[fname.rfind("_")+1:-8])
        if "cwnd" in fname:
            idx_cwnd = fname.find("cwnd")
            cwnd = int(fname[idx_cwnd+4:fname.rfind("_")])
        else:
            cwnd = -1
        if "_q" in fname:
            idx_q = fname.find("_q")
            q = fname[idx_q+2:].split("_")[0]
            q = int(q) if len(q) >0 else -1
        else:
            q = -1
        for col_name, value in [("load_percent", load_perc), ("cwnd", cwnd), ("qsize", q)]:
            df_fct[col_name] = value
            df_util[col_name] = value
            df_dalinks[col_name] = value
            total_bytes[col_name] = value
            seqno[col_name] = value

        all_load_util.append(df_util)
        if not ignore_fct:
            all_load_fct.append(df_fct)
        all_load_dalinks.append(df_dalinks)
        all_datypes.append(df_datypes)
        all_total_received.append(total_bytes)
        all_seqno.append(seqno)

        all_durations.append({"load_percent": load_perc, "cwnd": cwnd, "qsize": q, "duration": duration})

    if len(all_total_received) > 0:
        pd.concat(all_load_util).to_hdf(os.path.join(data_path, "aggregated_util.h5"), "w")
        if not ignore_fct:
            pd.concat(all_load_fct).to_hdf(os.path.join(data_path, "aggregated_fct.h5"), "w")
        pd.concat(all_load_dalinks).to_hdf(os.path.join(data_path, "aggregated_dalinks.h5"), "w")
        pd.concat(all_datypes).to_hdf(os.path.join(data_path, "aggregated_datypes.h5"), "w")
        pd.DataFrame.from_records(all_durations).to_json(os.path.join(data_path, "aggregated_runtime.json"))
        pd.concat(all_total_received).to_hdf(os.path.join(data_path, "aggregated_total_received.h5"), "w")
        pd.concat(all_seqno).to_hdf(os.path.join(data_path, "aggregated_diff_seqno.h5"), "w")
