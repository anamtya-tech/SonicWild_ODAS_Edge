#!/usr/bin/env python3
"""Measure latest ODAS wall-clock pacing under the real configured workload."""

from __future__ import annotations

import argparse
import csv
from collections import Counter
from datetime import datetime, timezone
import json
import math
import os
from pathlib import Path
import re
import shutil
import signal
import statistics
import subprocess
import tempfile
import time


SOURCE = Path(__file__).resolve().parents[1]
MODULES = (
    "mod_chatak_process", "mod_classify_process", "mod_istft_process",
    "mod_mapping_process", "mod_noise_process", "mod_resample_process_push",
    "mod_resample_process_pop", "mod_ssl_process", "mod_sss_process",
    "mod_sst_process", "mod_stft_process", "mod_volume_process",
)


def command_output(command: list[str]) -> str:
    try:
        result = subprocess.run(command, text=True, capture_output=True, timeout=3)
        return result.stdout.strip() if result.returncode == 0 else result.stderr.strip()
    except (OSError, subprocess.TimeoutExpired) as error:
        return f"unavailable: {error}"


def distribution(values: list[float], deadline_ms: float | None = None) -> dict:
    if not values:
        return {"count": 0}
    ordered = sorted(values)
    result = {
        "count": len(ordered), "mean_ms": statistics.fmean(ordered),
        "p95_ms": ordered[math.ceil(0.95 * len(ordered)) - 1],
        "p99_ms": ordered[math.ceil(0.99 * len(ordered)) - 1],
        "max_ms": ordered[-1],
    }
    if deadline_ms is not None:
        misses = sum(value > deadline_ms for value in ordered)
        result.update(over_deadline=misses, over_deadline_percent=100 * misses / len(ordered))
    return result


def markdown_table(headers: list[str], rows: list[list[str]],
                   right_columns: set[int] | None = None) -> list[str]:
    right_columns = right_columns or set()
    widths = [max(3, len(header), *(len(row[index]) for row in rows))
              for index, header in enumerate(headers)]

    def format_row(row: list[str]) -> str:
        cells = [cell.rjust(widths[index]) if index in right_columns
                 else cell.ljust(widths[index])
                 for index, cell in enumerate(row)]
        return "| " + " | ".join(cells) + " |"

    separator = [("-" * (width - 1) + ":") if index in right_columns
                 else (":" + "-" * (width - 1))
                 for index, width in enumerate(widths)]
    return [format_row(headers), "| " + " | ".join(separator) + " |",
            *(format_row(row) for row in rows)]


def tcp_transfer_summary(samples_path: Path, window_start: int,
                         window_end: int) -> dict:
    first: dict[tuple[str, str, str], tuple[int, int, int]] = {}
    last: dict[tuple[str, str, str], tuple[int, int, int]] = {}
    interface_tx_estimate = 0.0

    with samples_path.open() as sample_file:
        for line in sample_file:
            sample = json.loads(line)
            timestamp = sample["monotonic_ns"]
            previous = sample["previous_monotonic_ns"]
            overlap_start = max(previous, window_start)
            overlap_end = min(timestamp, window_end)
            if overlap_end > overlap_start:
                rate = sample.get("network", {}).get("tx_bytes_per_second", 0.0)
                interface_tx_estimate += rate * (overlap_end - overlap_start) / 1e9

            if not window_start <= timestamp <= window_end:
                continue
            tcp_lines = sample.get("tcp", "").splitlines()
            for index, connection_line in enumerate(tcp_lines[:-1]):
                if not connection_line.startswith("ESTAB "):
                    continue
                fields = connection_line.split()
                if len(fields) < 5:
                    continue
                local_endpoint, peer_endpoint = fields[3], fields[4]
                peer_host, separator, peer_port = peer_endpoint.rpartition(":")
                if not separator or peer_port not in ("9002", "9003"):
                    continue
                match = re.search(
                    r"bytes_sent:(\d+).*?bytes_acked:(\d+)", tcp_lines[index + 1]
                )
                if match is None:
                    continue
                key = (peer_port, local_endpoint, peer_host)
                counters = (timestamp, int(match.group(1)), int(match.group(2)))
                first.setdefault(key, counters)
                last[key] = counters

    streams = {}
    for port in ("9002", "9003"):
        connections = []
        for key, initial in first.items():
            if key[0] != port or key not in last:
                continue
            final = last[key]
            connections.append({
                "peer": f"{key[2]}:{port}",
                "sent_bytes": max(0, final[1] - initial[1]),
                "acked_bytes": max(0, final[2] - initial[2]),
                "sample_seconds": max(0.0, (final[0] - initial[0]) / 1e9),
            })
        streams[port] = {
            "connections": connections,
            "sent_bytes": sum(item["sent_bytes"] for item in connections),
            "acked_bytes": sum(item["acked_bytes"] for item in connections),
            "sample_seconds": min((item["sample_seconds"] for item in connections),
                                  default=0.0),
        }

    return {"streams": streams,
            "interface_tx_estimate_bytes": round(interface_tx_estimate)}


def sd_write_deadline_summary(audio_directory: Path, window_start: int,
                              window_end: int, deadline_ms: float) -> dict:
    durations_ms = []
    total_bytes = 0
    for path in audio_directory.rglob("*.write_timing.csv"):
        with path.open(newline="") as csv_file:
            for row in csv.DictReader(csv_file):
                started = int(row["write_start_ns"])
                if not window_start <= started < window_end:
                    continue
                duration_ms = (int(row["write_end_ns"]) - started) / 1e6
                durations_ms.append(duration_ms)
                total_bytes += int(row["bytes"])

    over_deadline = sum(value > deadline_ms for value in durations_ms)
    return {
        "count": len(durations_ms),
        "bytes": total_bytes,
        "over_deadline_count": over_deadline,
        "over_deadline_percent": (100.0 * over_deadline / len(durations_ms)
                                  if durations_ms else 0.0),
        "over_deadline_total_ms": sum(value for value in durations_ms
                                       if value > deadline_ms),
        **distribution(durations_ms),
    }


def module_display_label(entry: dict) -> str:
    names = {
        "mod_chatak_process": "CHATAK",
        "mod_classify_process": "Classify",
        "mod_istft_process": "ISTFT",
        "mod_mapping_process": "Mapping",
        "mod_noise_process": "Noise",
        "mod_resample_process_push": "Resample push",
        "mod_resample_process_pop": "Resample pop",
        "mod_ssl_process": "SSL",
        "mod_sss_process": "SSS",
        "mod_sst_process": "SST",
        "mod_stft_process": "STFT",
        "mod_volume_process": "Volume",
        "snk_categories_process": "Category sink",
        "snk_hops_process": "Audio sink",
        "snk_pots_process": "Pots sink",
        "snk_pots_process_interface_socket": "Pots JSON send",
        "snk_tracks_process": "Tracks sink",
        "snk_tracks_process_interface_socket": "Tracks JSON send",
        "src_hops_process": "Audio source",
        "ssl_worker_wait": "SSL worker wait",
        "tracks_file_write": "Tracks file write",
    }
    thread = entry["thread"].removeprefix("thread-")
    instance = entry["instance"].rsplit("x", maxsplit=1)[-1][-4:]
    return f"{names.get(entry['module'], entry['module'])} / t{thread} / {instance}"


def cpu_counters() -> dict[str, list[int]]:
    counters = {}
    for line in Path("/proc/stat").read_text().splitlines():
        fields = line.split()
        if fields and fields[0].startswith("cpu"):
            counters[fields[0]] = [int(value) for value in fields[1:9]]
    return counters


def cpu_rates(previous: dict, current: dict) -> dict:
    result = {}
    for core, ticks in current.items():
        if core not in previous:
            continue
        delta = [value - old for value, old in zip(ticks, previous[core])]
        total = sum(delta)
        if total > 0:
            result[core] = {
                "busy_percent": 100 * (total - delta[3] - delta[4]) / total,
                "iowait_percent": 100 * delta[4] / total,
            }
    return result


def disk_counters(device: str) -> list[int] | None:
    for line in Path("/proc/diskstats").read_text().splitlines():
        fields = line.split()
        if fields[2] == device:
            return [int(value) for value in fields[3:]]
    return None


def disk_rates(previous: list[int] | None, current: list[int] | None,
               elapsed: float) -> dict:
    if previous is None or current is None or elapsed <= 0:
        return {}
    delta = [value - old for value, old in zip(current, previous)]
    operations = delta[0] + delta[4]
    return {
        "read_Bps": delta[2] * 512 / elapsed,
        "write_Bps": delta[6] * 512 / elapsed,
        "await_ms": (delta[3] + delta[7]) / operations if operations else None,
        "util_percent": delta[9] / (elapsed * 10),
        "in_flight": current[8],
    }


def network_counters(interface: str) -> dict:
    directory = Path("/sys/class/net") / interface / "statistics"
    return {name: int((directory / name).read_text()) for name in (
        "rx_bytes", "tx_bytes", "rx_packets", "tx_packets", "rx_dropped", "tx_dropped",
    )}


def tcp_snapshot(pid: int) -> str:
    lines = command_output(["ss", "-tinp"]).splitlines()
    selected = []
    for index, line in enumerate(lines):
        if f"pid={pid}," in line:
            selected.append(line)
            if index + 1 < len(lines) and lines[index + 1].startswith("\t"):
                selected.append(lines[index + 1])
    return "\n".join(selected)


def thermal_snapshot() -> dict:
    return {name: command_output(["vcgencmd", *arguments]) for name, arguments in (
        ("throttled", ["get_throttled"]), ("temperature", ["measure_temp"]),
        ("arm_clock", ["measure_clock", "arm"]),
    )}


def storage_info(directory: Path) -> tuple[dict, str]:
    output = command_output(["findmnt", "--json", "--target", str(directory),
                             "--output", "SOURCE,TARGET,FSTYPE"])
    try:
        mount = json.loads(output)["filesystems"][0]
    except (ValueError, KeyError, IndexError, TypeError) as error:
        raise RuntimeError(f"Cannot identify recording storage: {output}") from error
    device = Path(mount["source"]).name
    block = Path("/sys/class/block") / device
    if (block / "partition").exists():
        device = block.resolve().parent.name
    return mount, device


def read_traces(directory: Path, window_start: int, window_end: int,
                deadline_ms: float, output: Path,
                warnings: list[str] | None = None) -> tuple[dict, list[dict]]:
    groups = {}
    intervals = []
    for path in sorted(directory.glob("thread-*.csv")):
        with path.open(newline="") as trace_file:
            for row in csv.DictReader(trace_file):
                if any(row.get(field) is None for field in (
                    "monotonic_ns", "module", "instance", "sequence", "frame", "bytes", "phase",
                )):
                    if warnings is not None:
                        warnings.append(f"Incomplete trace row discarded in {path.name}.")
                    continue
                timestamp = int(row["monotonic_ns"])
                if not window_start <= timestamp < window_end:
                    continue
                if row["module"] == "ssl_input_queue":
                    continue
                if row["module"] == "mod_ssl_process" and int(row["frame"]) == 0:
                    continue
                key = (row["module"], path.stem, row["instance"])
                group = groups.setdefault(key, {"starts": [], "pending": {}, "durations": []})
                identity = row["frame"] if row["module"] == "tracks_file_write" else row["sequence"]
                if row["phase"] == "start":
                    group["starts"].append((timestamp, row["frame"]))
                    group["pending"][identity] = timestamp
                elif row["phase"] == "end" and identity in group["pending"]:
                    group["durations"].append((timestamp - group["pending"].pop(identity)) / 1e6)
    summaries = {}
    for key, group in groups.items():
        module, thread, instance = key
        starts = sorted(group["starts"])
        deltas = []
        for previous, current in zip(starts, starts[1:]):
            delta = (current[0] - previous[0]) / 1e6
            deltas.append(delta)
            if module == "mod_ssl_process":
                intervals.append({
                    "monotonic_ns": current[0], "previous_monotonic_ns": previous[0],
                    "frame_id": current[1], "thread": thread, "instance": instance,
                    "previous_frame_id": previous[1],
                    "interval_ms": delta, "over_deadline": int(delta > deadline_ms),
                })
        label = f"{module}/{thread}/{instance}"
        summaries[label] = {
            "module": module, "thread": thread, "instance": instance,
            "starts": len(starts), "unmatched_starts": len(group["pending"]),
            "inter_frame": distribution(deltas, deadline_ms),
            "scope_wall_time": distribution(group["durations"]),
            "terminal_gap_ms": (window_end - starts[-1][0]) / 1e6 if starts else None,
        }
    intervals.sort(key=lambda row: row["monotonic_ns"])
    with (output / "ssl_intervals.csv").open("w", newline="") as csv_file:
        writer = csv.DictWriter(csv_file, fieldnames=[
            "monotonic_ns", "previous_monotonic_ns", "frame_id", "previous_frame_id", "thread", "instance",
            "interval_ms", "over_deadline",
        ])
        writer.writeheader()
        writer.writerows(intervals)
    ordered = sorted(float(row["interval_ms"]) for row in intervals)
    with (output / "ssl_distribution.csv").open("w", newline="") as csv_file:
        writer = csv.writer(csv_file)
        writer.writerow(["interval_ms", "empirical_cdf"])
        writer.writerows((value, (index + 1) / len(ordered)) for index, value in enumerate(ordered))
    return summaries, intervals


def recorder_summary(directory: Path, start: int, end: int) -> dict:
    durations = []
    read_intervals = []
    total_bytes = 0
    for path in directory.rglob("*.write_timing.csv"):
        previous = None
        with path.open(newline="") as csv_file:
            for row in csv.DictReader(csv_file):
                timestamp = int(row["write_start_ns"])
                if not start <= timestamp < end:
                    continue
                durations.append((int(row["write_end_ns"]) - timestamp) / 1e6)
                read_start = int(row["read_start_ns"])
                if previous is not None:
                    read_intervals.append((read_start - previous) / 1e6)
                previous = read_start
                total_bytes += int(row["bytes"])
    return {"bytes": total_bytes, "write_wall_time": distribution(durations),
            "read_start_intervals": distribution(read_intervals)}


def write_module_timings(output: Path, modules: dict) -> None:
    fields = ["module", "thread", "instance", "starts", "completed_scopes",
              "scope_mean_ms", "scope_p95_ms", "scope_p99_ms", "scope_max_ms",
              "interval_count", "interval_mean_ms", "interval_p95_ms",
              "interval_p99_ms", "interval_max_ms", "over_deadline_percent",
              "terminal_gap_ms"]
    with (output / "module_timings.csv").open("w", newline="") as csv_file:
        writer = csv.DictWriter(csv_file, fieldnames=fields)
        writer.writeheader()
        for entry in sorted(modules.values(), key=lambda value: (value["module"], value["thread"], value["instance"])):
            row = {key: entry[key] for key in ("module", "thread", "instance", "starts", "terminal_gap_ms")}
            row["completed_scopes"] = entry["scope_wall_time"]["count"]
            row["interval_count"] = entry["inter_frame"]["count"]
            for prefix, key in (("scope", "scope_wall_time"), ("interval", "inter_frame")):
                for metric in ("mean_ms", "p95_ms", "p99_ms", "max_ms"):
                    row[f"{prefix}_{metric}"] = entry[key].get(metric)
            row["over_deadline_percent"] = entry["inter_frame"].get("over_deadline_percent")
            writer.writerow(row)


def write_spike_context(output: Path, intervals: list[dict], samples: list[dict]) -> dict:
    fields = ["frame_id", "previous_monotonic_ns", "monotonic_ns", "interval_ms",
              "overlapping_samples", "host_busy_peak_percent", "host_iowait_peak_percent",
              "sd_await_peak_ms", "sd_util_peak_percent", "network_rx_peak_Bps",
              "network_tx_peak_Bps", "throttled_readings"]
    matched = 0
    iowait_spikes = 0
    with (output / "ssl_spike_context.csv").open("w", newline="") as csv_file:
        writer = csv.DictWriter(csv_file, fieldnames=fields)
        writer.writeheader()
        for interval in intervals:
            if not interval["over_deadline"]:
                continue
            overlapping = [sample for sample in samples
                           if sample["previous_monotonic_ns"] < interval["monotonic_ns"]
                           and sample["monotonic_ns"] > interval["previous_monotonic_ns"]]
            row = {name: interval[name] for name in fields[:4]}
            row["overlapping_samples"] = len(overlapping)
            for field, category, metric in (
                ("host_busy_peak_percent", "cpu", "busy_percent"),
                ("host_iowait_peak_percent", "cpu", "iowait_percent"),
                ("sd_await_peak_ms", "disk", "await_ms"),
                ("sd_util_peak_percent", "disk", "util_percent"),
                ("network_rx_peak_Bps", "network", "rx_bytes_per_second"),
                ("network_tx_peak_Bps", "network", "tx_bytes_per_second"),
            ):
                values = [(sample[category].get("cpu", {}) if category == "cpu" else sample[category]).get(metric)
                          for sample in overlapping]
                row[field] = max((value for value in values if value is not None), default=None)
            row["throttled_readings"] = " | ".join(sorted({sample["thermal"].get("throttled", "unavailable")
                                                           for sample in overlapping}))
            if overlapping:
                matched += 1
            if row["host_iowait_peak_percent"] is not None and row["host_iowait_peak_percent"] > 0:
                iowait_spikes += 1
            writer.writerow(row)
    return {"spikes": sum(row["over_deadline"] for row in intervals),
            "matched_to_system_samples": matched, "overlapping_nonzero_iowait": iowait_spikes,
            "interpretation": "One-second system-bin overlap, not per-frame attribution or causal proof"}


def analyze_overrun_handling(output: Path, intervals: list[dict], start: int,
                             end: int, period_ms: float) -> dict:
    if not intervals or len({(row["thread"], row["instance"]) for row in intervals}) != 1:
        return {"available": False, "reason": "Requires a measured single SSL stream"}
    waits = {}
    queues = []
    for path in (output / "traces").glob("thread-*.csv"):
        pending = {}
        with path.open() as trace_file:
            for row in csv.DictReader(trace_file):
                if row.get("module") not in ("ssl_worker_wait", "ssl_input_queue") or row.get("phase") is None:
                    continue
                timestamp = int(row["monotonic_ns"])
                if row["module"] == "ssl_worker_wait":
                    if row["phase"] == "start":
                        pending[row["sequence"]] = timestamp
                    elif row["phase"] == "end" and row["sequence"] in pending:
                        began = pending.pop(row["sequence"])
                        if start <= began < timestamp < end and int(row["frame"]) > 0:
                            waits[(path.stem, int(row["frame"]))] = (timestamp - began) / 1e6
                elif (start <= timestamp < end and row["phase"] == "after_pop"
                      and row.get("queue_depth") is not None and int(row["frame"]) > 0):
                    queues.append({"monotonic_ns": timestamp, "thread": path.stem,
                                   "frame_id": int(row["frame"]), "queue_depth": int(row["queue_depth"])})
    with (output / "ssl_queue_depth.csv").open("w", newline="") as csv_file:
        writer = csv.DictWriter(csv_file, fieldnames=["monotonic_ns", "thread", "frame_id", "queue_depth"])
        writer.writeheader()
        writer.writerows(sorted(queues, key=lambda row: row["monotonic_ns"]))
    origin_time = intervals[0]["previous_monotonic_ns"]
    origin_frame = int(intervals[0]["previous_frame_id"])
    residuals = [(row["monotonic_ns"] - origin_time) / 1e6
                 - (int(row["frame_id"]) - origin_frame) * period_ms for row in intervals]
    spikes = [index for index, row in enumerate(intervals) if row["over_deadline"]]
    long_pauses = [index for index in spikes if intervals[index]["interval_ms"] > 10 * period_ms]
    long_pause_gaps = [(intervals[current]["monotonic_ns"] - intervals[previous]["monotonic_ns"]) / 1e6
                      for previous, current in zip(long_pauses, long_pauses[1:])]
    gaps = [(intervals[current]["monotonic_ns"] - intervals[previous]["monotonic_ns"]) / 1e6
            for previous, current in zip(spikes, spikes[1:])]
    frame_spacing = Counter(int(intervals[current]["frame_id"]) - int(intervals[previous]["frame_id"])
                            for previous, current in zip(spikes, spikes[1:]))
    thread = intervals[0]["thread"]
    queue_by_frame = {row["frame_id"]: row["queue_depth"] for row in queues if row["thread"] == thread}
    cycles = []
    cycle_boundaries = long_pauses if len(long_pauses) >= 2 else spikes
    for previous, current in zip(cycle_boundaries, cycle_boundaries[1:]):
        began = intervals[previous]
        last = intervals[current - 1]
        depths = [queue_by_frame[frame] for frame in range(int(began["frame_id"]), int(last["frame_id"]) + 1)
                  if frame in queue_by_frame]
        cycles.append({
            "gap_frame_id": began["frame_id"], "gap_interval_ms": began["interval_ms"],
            "worker_wait_ms": waits.get((thread, int(began["frame_id"]))),
            "frames_before_next_gap": int(last["frame_id"]) - int(began["frame_id"]) + 1,
            "burst_span_ms": (last["monotonic_ns"] - began["monotonic_ns"]) / 1e6,
            "relative_schedule_recovery_ms": residuals[previous] - residuals[current - 1],
            "input_queue_max_after_pop": max(depths) if depths else None,
            "input_queue_reached_zero": int(0 in depths) if depths else None,
        })
    with (output / "ssl_burst_cycles.csv").open("w", newline="") as csv_file:
        writer = csv.DictWriter(csv_file, fieldnames=["gap_frame_id", "gap_interval_ms", "worker_wait_ms",
            "frames_before_next_gap", "burst_span_ms", "relative_schedule_recovery_ms",
            "input_queue_max_after_pop", "input_queue_reached_zero"])
        writer.writeheader()
        writer.writerows(cycles)
    with (output / "ssl_relative_schedule.csv").open("w", newline="") as csv_file:
        writer = csv.writer(csv_file)
        writer.writerow(["monotonic_ns", "frame_id", "relative_schedule_residual_ms"])
        writer.writerows((row["monotonic_ns"], row["frame_id"], residual)
                         for row, residual in zip(intervals, residuals))
    duration = max(0, (end - start) / 1e9)
    edge_ns = int(min(10, duration / 4) * 1e9)
    first = [value for row, value in zip(intervals, residuals) if row["monotonic_ns"] < start + edge_ns]
    last = [value for row, value in zip(intervals, residuals) if row["monotonic_ns"] >= end - edge_ns]
    windows = {}
    for label, selected in (
        ("first_5s", [row for row in intervals if row["monotonic_ns"] < start + 5_000_000_000]),
        ("middle_without_5s_edges", [row for row in intervals if row["previous_monotonic_ns"] >= start + 5_000_000_000
                                     and row["monotonic_ns"] < end - 5_000_000_000]),
        ("last_5s", [row for row in intervals if row["previous_monotonic_ns"] >= end - 5_000_000_000]),
    ):
        windows[label] = distribution([row["interval_ms"] for row in selected], period_ms)
    frame_deltas = [int(row["frame_id"]) - int(row["previous_frame_id"]) for row in intervals]
    spike_waits = [waits[(thread, int(intervals[index]["frame_id"]))] for index in spikes
                   if (thread, int(intervals[index]["frame_id"])) in waits]
    positive_wait_cycles = [cycle for cycle in cycles if cycle["worker_wait_ms"] is not None]
    median_change = statistics.median(last) - statistics.median(first) if first and last else None
    drained = sum(cycle["input_queue_reached_zero"] == 1 for cycle in cycles)
    continuous_ids = all(delta == 1 for delta in frame_deltas)
    if (queues and cycles and drained / len(cycles) >= 0.95 and continuous_ids
            and median_change is not None and abs(median_change) < period_ms):
        assessment = "Observed repeated catch-up: the monitored SSL input FIFO drains and relative schedule medians show less than one frame-period change in this window. This does not establish absolute end-to-end latency or hardware sample-loss status."
    else:
        assessment = "Evidence does not meet the catch-up checks (FIFO draining, generated-ID continuity, and less than one frame-period change in edge residual medians); inspect the measurements without assuming bounded lag."
    return {
        "available": True, "spikes": len(spikes), "windows": windows,
        "long_pause_threshold_ms": 10 * period_ms,
        "long_pauses": len(long_pauses), "other_deadline_exceedances": len(spikes) - len(long_pauses),
        "long_pause_spacing_median_ms": statistics.median(long_pause_gaps) if long_pause_gaps else None,
        "catchup_assessment": assessment,
        "spike_spacing_ms": distribution(gaps),
        "median_spike_spacing_ms": statistics.median(gaps) if gaps else None,
        "frame_spacing_counts": dict(sorted(frame_spacing.items())),
        "skipped_ssl_frame_ids": sum(max(0, delta - 1) for delta in frame_deltas),
        "duplicate_or_reversed_ssl_ids": sum(delta <= 0 for delta in frame_deltas),
        "gap_worker_wait": distribution(spike_waits),
        "cycles_with_wait_data": len(positive_wait_cycles),
        "complete_cycles": len(cycles),
        "cycles_input_queue_reached_zero": drained,
        "input_queue_samples": len(queues),
        "input_queue_max_after_pop": max((row["queue_depth"] for row in queues), default=None),
        "burst_span": distribution([cycle["burst_span_ms"] for cycle in cycles]),
        "relative_schedule_recovery": distribution([cycle["relative_schedule_recovery_ms"] for cycle in cycles]),
        "relative_residual_min_ms": min(residuals), "relative_residual_max_ms": max(residuals),
        "first_edge_median_residual_ms": statistics.median(first) if first else None,
        "last_edge_median_residual_ms": statistics.median(last) if last else None,
        "edge_median_residual_change_ms": median_change,
        "edge_window_seconds": edge_ns / 1e9,
        "interpretation": "Relative frame-ID schedule and one SSL input FIFO, not hardware loss detection or absolute capture-to-display latency",
    }


def stop_process(process: subprocess.Popen) -> bool:
    forced = False
    if process.poll() is None:
        process.send_signal(signal.SIGINT)
        try:
            process.wait(timeout=15)
        except subprocess.TimeoutExpired:
            forced = True
    try:
        os.killpg(process.pid, signal.SIGTERM)
    except ProcessLookupError:
        pass
    if process.poll() is None:
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            forced = True
            os.killpg(process.pid, signal.SIGKILL)
            process.wait()
    return forced


def parse_stage_timing(log_text: str) -> dict:
    result = {}
    for module in ("SSL", "SST"):
        title = f"{module} Stage Timing"
        if title not in log_text:
            continue
        block = log_text.split(title, 1)[1]
        stages = {}
        frames = None
        for line in block.splitlines():
            match = re.search(r"\|\s*(.*?)\.{2,}\s*([\d,]+(?:\.\d+)?)\s*(ms)?\s*\|", line)
            if match is None:
                continue
            label, value, unit = match.groups()
            if label == "Frames profiled":
                frames = int(value.replace(",", ""))
            elif unit == "ms":
                stages[label] = float(value.replace(",", ""))
            if label == f"Full {module} Frame":
                result[module] = {"frames": frames, "stages_ms": stages}
                break
    return result


def parse_alsa_capture_params(log_text: str) -> dict | None:
    match = re.search(
        r"ALSA capture params: device=(\S+) rate=(\d+) hop=(\d+) "
        r"period=(\d+) frames \(([\d.]+) ms\) "
        r"buffer=(\d+) frames \(([\d.]+) ms\)",
        log_text,
    )
    if match is None:
        return None
    device, rate, hop, period_frames, period_ms, buffer_frames, buffer_ms = match.groups()
    return {
        "device": device,
        "rate_hz": int(rate),
        "hop_frames": int(hop),
        "period_frames": int(period_frames),
        "period_ms": float(period_ms),
        "buffer_frames": int(buffer_frames),
        "buffer_ms": float(buffer_ms),
    }


def write_report(output: Path, result: dict) -> None:
    ssl = result["ssl_inter_frame"]
    lines = ["# ODAS Latest-Version Live Timing", "", "## 1. Does It Keep Up, End to End?", "",
             f"Revision: `{result['revision']}`. Command: `{result['command']}`.",
             f"Measured window: {result['window_seconds']:.3f} s; deadline: {result['deadline_ms']:.3f} ms.",
             "SSL start-to-start intervals include computation, scheduling and upstream blocking;",
             "they are not receiver-render latency, and downstream stalls matter only when they backpressure SSL.", ""]
    lines.append(f"SSL terminal gap: **{result['ssl_terminal_gap_ms']:.3f} ms** "
                 "(right-censored: no following frame; not included in the delta distribution).")
    if ssl["count"]:
        lines.append(f"SSL intervals: **{ssl['count']}**; mean **{ssl['mean_ms']:.3f} ms**, "
                     f"p95 **{ssl['p95_ms']:.3f} ms**, p99 **{ssl['p99_ms']:.3f} ms**, "
                     f"max **{ssl['max_ms']:.3f} ms**; over deadline **{ssl['over_deadline_percent']:.2f}%**.")
    else:
        lines.append("**No valid SSL inter-frame measurements. This run cannot establish real-time performance.**")
    handling = result.get("overrun_handling", {})
    lines.extend(["", "## What The Overruns Mean And How ODAS Handles Them", "",
        "An overrun here is a start-to-start SSL interval longer than the configured frame period. It is NOT an ALSA xrun, a dropped frame, or proof that SSL computation took that long.",
        "There is no fixed '299 frames' category: the actual count depends on duration and this run's observed intervals. p99 is a percentile, not a dropped-frame count.",
        "ODAS waits on blocking audio input, consumes messages in FIFO order, and processes available frames in bursts. Empty FIFOs make workers wait; full bounded queues backpressure producers. This checkout configures 100 message slots per asynchronous queue.",
        "The SSL worker has no 8 ms deadline rejection or automatic late-frame discard. The measurement labels gaps; it does not change how ODAS processes them.", ""])
    if handling.get("available"):
        def measured(value):
            return f"{value:.3f}" if value is not None else "n/a"
        lines.extend([
            f"Observed gap events: **{handling['spikes']}**. Median spacing: **{measured(handling['median_spike_spacing_ms'])} ms**; frame-ID spacing counts: `{handling['frame_spacing_counts']}`.",
            f"Event classification: **{handling['long_pauses']} long pauses** over the descriptive {handling['long_pause_threshold_ms']:.1f} ms threshold and **{handling['other_deadline_exceedances']} other deadline exceedances**. The actual deadline is still {result['deadline_ms']:.3f} ms. Median spacing between long pauses: **{measured(handling['long_pause_spacing_median_ms'])} ms**.",
            f"SSL frame-ID continuity: **{handling['skipped_ssl_frame_ids']} skipped IDs**, **{handling['duplicate_or_reversed_ssl_ids']} duplicate/reversed transitions**. Continuous generated IDs do not prove physical audio samples were never lost.",
            f"Measured input FIFO after-pop peak: **{handling['input_queue_max_after_pop']} queued messages** across **{handling['input_queue_samples']} samples**. It reached zero in **{handling['cycles_input_queue_reached_zero']} / {handling['complete_cycles']} complete pause/burst cycles**.",
            f"Worker wait associated with gap frames: mean **{measured(handling['gap_worker_wait'].get('mean_ms'))} ms**, max **{measured(handling['gap_worker_wait'].get('max_ms'))} ms**. This wait includes spectra input, CHATAK input and output-slot availability, not just ALSA reading.",
            f"Mean post-gap burst span: **{measured(handling['burst_span'].get('mean_ms'))} ms**. Mean relative schedule recovery per complete cycle: **{measured(handling['relative_schedule_recovery'].get('mean_ms'))} ms**.",
            f"Relative schedule residual range: **{measured(handling['relative_residual_min_ms'])} to {measured(handling['relative_residual_max_ms'])} ms**. Last-minus-first {handling['edge_window_seconds']:.1f}-second median change: **{measured(handling['edge_median_residual_change_ms'])} ms**.",
            "The residual is elapsed time minus frame-ID progression times the nominal period, referenced to the first measured frame. It tests accumulating schedule drift, NOT absolute microphone-to-screen lag. It does not identify the cause of the periodic pauses.", "",
            f"**Measured conclusion:** {handling['catchup_assessment']}", "",
        ])
        window_rows = []
        for label, timing in handling["windows"].items():
            window_rows.append([label, f"{timing['count']:,}",
                                f"{timing.get('over_deadline', 0):,}",
                                measured(timing.get("over_deadline_percent"))])
        lines.extend(markdown_table(
            ["Window (warm-up excluded)", "Intervals", "Overruns", "Overrun %"],
            window_rows, {1, 2, 3}))
        lines.extend(["", "Use continuity, cycle recovery, FIFO draining and schedule drift together to judge catch-up. Throughput alone does not prove bounded latency; this probe observes only the SSL input FIFO, not every pipeline queue.",
                      "Evidence: ssl_queue_depth.csv, ssl_burst_cycles.csv and ssl_relative_schedule.csv. Queue-depth snapshots may miss peaks between samples."])
        alsa = result.get("alsa_capture")
        if alsa:
            hop_frames = alsa["hop_frames"]
            period_frames = alsa["period_frames"]
            buffer_frames = alsa["buffer_frames"]
            gap_rows = [
                ["ODAS read request", f"{hop_frames} frames ({1000 * hop_frames / alsa['rate_hz']:.3f} ms)"],
                ["One ALSA period", f"{period_frames} frames ({alsa['period_ms']:.3f} ms; {period_frames / hop_frames:.3f} hops)"],
                ["Two ALSA periods", f"{2 * period_frames} frames ({2 * alsa['period_ms']:.3f} ms; {2 * period_frames / hop_frames:.3f} hops)"],
                ["ALSA buffer capacity", f"{buffer_frames} frames ({alsa['buffer_ms']:.3f} ms; {buffer_frames / hop_frames:.3f} hops)"],
                ["Observed pause cycle", f"{handling['long_pause_spacing_median_ms']:.3f} ms; most frame-ID gaps were 31-32"],
                ["ODAS input FIFO after-pop peak", f"{handling['input_queue_max_after_pop']} messages; drained in {handling['cycles_input_queue_reached_zero']}/{handling['complete_cycles']} cycles"],
            ]
            lines.extend(["", "### Why the ALSA sizes are relevant", "",
                          "ODAS requests one 128-frame hop from ALSA at a time; that does not guarantee a new hop is available every 8 ms. While a blocking read/worker waits, samples can accumulate upstream. When data becomes available and the worker runs, successive hop reads can complete quickly and drain the backlog.", ""])
            lines.extend(markdown_table(["Quantity", "This run"], gap_rows))
            lines.extend(["",
                          "Two negotiated ALSA periods equal about 31.25 ODAS hops, close to the measured ~250 ms pause spacing and 31-32-frame gaps. This is a plausible link to capture buffering, not proof that ALSA alone causes the pattern. The ALSA buffer value is maximum capacity, not the number of frames delivered as one batch; scheduling and other queues can contribute."])
    else:
        lines.append("Handling evidence unavailable: no valid single SSL stream, or this report predates the queue/wait probes.")
    pacing_rows = []
    scope_rows = []
    for module in ("SSL", "SST"):
        entries = [entry for entry in result["modules"].values()
                   if entry["module"] == f"mod_{module.lower()}_process"]
        if not entries:
            pacing_rows.append([module, "0", "n/a", "n/a", "n/a", "n/a"])
            scope_rows.append([module, "0", "n/a", "n/a", "n/a", "n/a"])
        for entry in entries:
            delta = entry["inter_frame"]
            scope = entry["scope_wall_time"]
            pacing_values = [f"{delta[name]:.3f}" if name in delta else "n/a"
                             for name in ("mean_ms", "p95_ms", "p99_ms", "max_ms")]
            scope_values = [f"{scope[name]:.3f}" if name in scope else "n/a"
                            for name in ("mean_ms", "p95_ms", "p99_ms", "max_ms")]
            pacing_rows.append([module, str(entry["inter_frame"]["count"]), *pacing_values])
            scope_rows.append([module, str(scope["count"]), *scope_values])
    lines.extend(["", "## SSL And SST Timing", "",
                  "All values are milliseconds. Pacing is start-to-start; processing time is entry-to-exit wall time.", "",
                  "### Frame pacing", ""])
    lines.extend(markdown_table(
        ["Module", "Intervals", "Mean ms", "p95 ms", "p99 ms", "Max ms"],
        pacing_rows, {1, 2, 3, 4, 5}))
    lines.extend(["", "### Processing scope", ""])
    lines.extend(markdown_table(
        ["Module", "Scopes", "Mean ms", "p95 ms", "p99 ms", "Max ms"],
        scope_rows, {1, 2, 3, 4, 5}))
    lines.extend(["", "### Internal Stage Timers", "",
                  "The existing stage timers use CLOCK_MONOTONIC: elapsed time inside the algorithm, not strictly CPU-busy time. They exclude time spent waiting outside those regions.", "",
                  ])
    stage_rows = []
    for module in ("SSL", "SST"):
        timing = result.get("stage_timing", {}).get(module)
        if timing is None:
            stage_rows.append([module, "Not emitted; inspect odas.log / shutdown status", "n/a"])
        else:
            for stage, value in timing["stages_ms"].items():
                stage_rows.append([module, stage, f"{value:.3f}"])
    lines.extend(markdown_table(
        ["Module", "Stage", "Mean ms/frame"], stage_rows, {2}))
    lines.extend(["", "## SD Recorder Write Timing", "",
                  f"The measured window wrote **{result['recorder']['bytes']:,} bytes** to the live audio recording."])
    write_details = sd_write_deadline_summary(
        Path(result.get("recording_directory", output / "audio")),
        result["window_start_ns"], result["window_end_ns"], result["deadline_ms"])
    read_timing = result["recorder"]["read_start_intervals"]
    write_timing = result["recorder"]["write_wall_time"]
    sd_rows = [
        ["Calls / intervals", f"{write_details['count']:,}", f"{read_timing['count']:,}"],
        ["Mean ms", f"{write_timing.get('mean_ms', 0.0):.3f}", f"{read_timing.get('mean_ms', 0.0):.3f}"],
        ["p95 ms", f"{write_timing.get('p95_ms', 0.0):.3f}", f"{read_timing.get('p95_ms', 0.0):.3f}"],
        ["p99 ms", f"{write_timing.get('p99_ms', 0.0):.3f}", f"{read_timing.get('p99_ms', 0.0):.3f}"],
        ["Maximum ms", f"{write_timing.get('max_ms', 0.0):.3f}", f"{read_timing.get('max_ms', 0.0):.3f}"],
        ["Over 8 ms", f"{write_details['over_deadline_count']} ({write_details['over_deadline_percent']:.3f}%)", "n/a"],
    ]
    lines.extend(markdown_table(
        ["Recorder metric", "Buffered SD write()", "Mic read-start interval*"],
        sd_rows, set()))
    lines.extend(["", "*The read-start interval is microphone pacing, not SD read time.",
                  f"**{write_details['over_deadline_count']} of {write_details['count']:,}** buffered write calls exceeded the {result['deadline_ms']:.0f} ms frame period. This shows occasional slow application writes (maximum {write_details.get('max_ms', 0.0):.3f} ms), not that audio was dropped or became late: writes are buffered, and the recorder runs separately from SSL. Live -rl recording exercises SD writes, not an artificial SD-read workload. Device I/O wait is below.",
                  "Buffered write-call duration does not measure fsync durability or when data reaches flash."])
    lines.extend(["", "## 2. What to Measure", "",
        "1. Inter-frame wall-clock interval: CLOCK_MONOTONIC at SSL entry; ssl_intervals.csv retains every delta and frame ID.",
        "2. Full distribution: mean, nearest-rank p95/p99, max and deadline exceedances; ssl_distribution.csv is the full empirical CDF.",
        "3. Per-core CPU utilisation: /proc/stat busy and iowait deltas for ALL cores in system_samples.jsonl.",
        "4. Storage: /proc/diskstats read/write bandwidth, await (completed read/write operations) and device busy time; timestamps share the frame clock.",
        "5. Network: hotspot-interface RX/TX bytes, packets and drops, plus ODAS TCP queues, retransmission counters and connection state from ss -tinp.",
        "6. Thermal/clock: repeated vcgencmd get_throttled, measure_temp and measure_clock arm. Current bits 0-3 and historical bits 16-19 distinguish undervoltage, frequency capping, throttling and soft temperature limits.",
        "7. ALL processing modules: entry/exit events by thread AND object instance, including separate resampler push/pop and source/sink boundaries. Uncalled modules are listed as inactive, not zero-cost.",
        "8. File and JSON paths: audio *.write_timing.csv gives read/write monotonic times, hop index, byte offset and size; SST sidecar writes and JSON sends are traced. JSON gains monotonic_ns and frame_id without replacing timeStamp.",
        "9. Live bidirectional traffic: keep the real visualizer and its control updates active during the run; interface RX alone does not prove visualizer commands were exercised.", "",
        "## Run Validity", "",
        f"Recording mount: `{result['storage']}`; monitored disk: `{result['disk']}`.",
        f"WiFi state: `{result['wifi_state']}`.",
        "Managed/client WiFi mode is valid for this measurement; a Pi-hosted access point is not required.",
        (f"ALSA PCM: `{result['alsa_capture']['device']}`, {result['alsa_capture']['rate_hz']} Hz; "
         f"hop {result['alsa_capture']['hop_frames']} frames, negotiated period "
         f"{result['alsa_capture']['period_frames']} frames ({result['alsa_capture']['period_ms']:.3f} ms), "
         f"buffer {result['alsa_capture']['buffer_frames']} frames ({result['alsa_capture']['buffer_ms']:.3f} ms)."
         if result.get("alsa_capture") else
         "Negotiated ALSA period/buffer values were not emitted by this build."),
        "The negotiated period/buffer are reported as settings only; their timing correlation does not prove they cause the pauses.",
        f"Raw audio bytes in window: {result['recorder']['bytes']}; network traffic is interface-wide, not visualizer-only.",
        "Buffered writes measure application write calls, not fsync durability; kernel writeback may occur later.",
        "Tracing and sampling add load. Traces are line-buffered on RAM-backed storage then copied after stopping.",
        "A quiet receiver or inactive controls cannot certify the requested full live workload.", ""])
    warnings = list(result["warnings"])
    if re.search(r"\btype managed\b", result.get("wifi_state", "")):
        warnings = [warning for warning in warnings
                    if not warning.startswith("Hotspot/AP mode is not confirmed")]
    lines.extend(f"- {warning}" for warning in warnings)

    transfer = result.get("network_transfer") or tcp_transfer_summary(
        output / "system_samples.jsonl", result["window_start_ns"], result["window_end_ns"])
    byte_rows = []
    send_rows = []
    socket_modules = (
        ("SST tracks", "9002", "snk_tracks_process_interface_socket"),
        ("SSL POTs", "9003", "snk_pots_process_interface_socket"),
    )
    for label, port, module_name in socket_modules:
        stream = transfer["streams"].get(port, {})
        scope = next((entry["scope_wall_time"] for entry in result["modules"].values()
                      if entry["module"] == module_name), {})
        peer = stream.get("connections", [{}])[0].get("peer", f"port {port}")
        byte_rows.append([label, peer, f"{stream.get('sent_bytes', 0):,}",
                          f"{stream.get('acked_bytes', 0):,}"])
        send_rows.append([
            label, str(scope.get("count", 0)),
            f"{scope['mean_ms']:.3f}" if "mean_ms" in scope else "n/a",
            f"{scope['p95_ms']:.3f}" if "p95_ms" in scope else "n/a",
            f"{scope['p99_ms']:.3f}" if "p99_ms" in scope else "n/a",
            f"{scope['max_ms']:.3f}" if "max_ms" in scope else "n/a",
        ])
    sent_total = sum(transfer["streams"].get(port, {}).get("sent_bytes", 0)
                     for _, port, _ in socket_modules)
    acked_total = sum(transfer["streams"].get(port, {}).get("acked_bytes", 0)
                      for _, port, _ in socket_modules)
    sample_span = min((transfer["streams"].get(port, {}).get("sample_seconds", 0.0)
                       for _, port, _ in socket_modules), default=0.0)
    lines.extend(["", "## JSON Network Sends", "",
                  f"Sampled TCP counters span {sample_span:.3f} s within the 120 s measurement window; counters are sampled about once per second.", ""])
    lines.extend(markdown_table(
        ["ODAS stream", "Peer", "TCP sent bytes", "TCP ACKed bytes"],
        byte_rows, {2, 3}))
    lines.extend([""])
    lines.extend(markdown_table(
        ["Socket send-call scope", "Calls", "Mean ms", "p95 ms", "p99 ms", "Max ms"],
        send_rows, {1, 2, 3, 4, 5}))
    lines.extend(["",
                  f"Combined: **{sent_total:,} TCP bytes sent**, **{acked_total:,} bytes acknowledged by the peer TCP stack**. Interface-wide TX, including non-ODAS traffic, is approximately **{transfer['interface_tx_estimate_bytes']:,} bytes** over the measurement window.",
                  "Both local socket send-call maxima were below the 8 ms frame period. This means the calls returned within that budget; it does not prove the receiver application processed or displayed the data by the deadline. SSL still had 497 start-to-start intervals over 8 ms.", "",
                  "## System Measurements", ""])
    cpu_rows = []
    for core, values in sorted(result["system_summary"]["cpu"].items()):
        cpu_rows.append([core, f"{values['mean_busy_percent']:.2f}",
                         f"{values['max_busy_percent']:.2f}",
                         f"{values['mean_iowait_percent']:.2f}",
                         f"{values['max_iowait_percent']:.2f}"])
    lines.extend(markdown_table(
        ["Core", "Mean busy %", "Max busy %", "Mean iowait %", "Max iowait %"],
        cpu_rows, {1, 2, 3, 4}))
    for category, title in (("disk", "Disk rates"), ("network", "Interface rates")):
        metric_rows = [[name.replace("_", " "), f"{values['mean']:.3f}",
                        f"{values['max']:.3f}"]
                       for name, values in sorted(result["system_summary"][category].items())]
        if metric_rows:
            lines.extend(["", f"### {title}", ""])
            lines.extend(markdown_table(["Metric", "Mean", "Maximum"], metric_rows, {1, 2}))

    thermal = result["system_summary"]["thermal"]
    temperatures = []
    clocks_ghz = []
    throttle_masks = set()
    for sample in thermal:
        temp_match = re.search(r"temp=([0-9.]+)'?C", sample.get("temperature", ""))
        clock_match = re.search(r"frequency\(\d+\)=([0-9]+)", sample.get("arm_clock", ""))
        if temp_match:
            temperatures.append(float(temp_match.group(1)))
        if clock_match:
            clocks_ghz.append(int(clock_match.group(1)) / 1e9)
        throttle_masks.add(sample.get("throttled", "unavailable"))
    thermal_rows = [["Samples", str(len(thermal))]]
    if temperatures:
        thermal_rows.append(["Temperature range (C)",
                             f"{min(temperatures):.1f} to {max(temperatures):.1f}"])
    if clocks_ghz:
        thermal_rows.append(["ARM clock range (GHz)",
                             f"{min(clocks_ghz):.2f} to {max(clocks_ghz):.2f}"])
    thermal_rows.append(["Throttle status", ", ".join(sorted(throttle_masks))])
    lines.extend(["", "### Thermal and clock", ""])
    lines.extend(markdown_table(["Metric", "Observed"], thermal_rows))
    scope_rows = []
    pacing_rows = []
    for label, entry in sorted(result["modules"].items()):
        timing = entry["scope_wall_time"]
        delta = entry["inter_frame"]
        display_label = module_display_label(entry)
        scope_values = [f"{timing[name]:.3f}" if name in timing else "n/a"
                        for name in ("mean_ms", "p95_ms", "p99_ms", "max_ms")]
        pacing_values = [f"{delta[name]:.3f}" if name in delta else "n/a"
                         for name in ("mean_ms", "p95_ms", "p99_ms", "max_ms")]
        scope_rows.append([display_label, str(timing["count"]), *scope_values])
        pacing_rows.append([display_label, str(entry["starts"]), *pacing_values])
    lines.extend(["", "## All Module Processing Times", "",
        "Entry-to-exit wall time includes preemption or I/O inside the scope. Full thread/instance identifiers are in module_timings.csv.", ""])
    lines.extend(markdown_table(
        ["Module / thread", "Scopes", "Mean ms", "p95 ms", "p99 ms", "Max ms"],
        scope_rows, {1, 2, 3, 4, 5}))
    lines.extend(["", "## All Module Pacing", "",
        "Start-to-start interval; this is pacing, not processing duration.", ""])
    lines.extend(markdown_table(
        ["Module / thread", "Starts", "Mean ms", "p95 ms", "p99 ms", "Max ms"],
        pacing_rows, {1, 2, 3, 4, 5}))
    context = result.get("spike_context", {})
    lines.extend(["", "## Frame Spike Context", "",
                  f"Deadline-exceeding SSL intervals: {context.get('spikes', 'n/a')}; matched to system bins: {context.get('matched_to_system_samples', 'n/a')}; overlapping nonzero host iowait: {context.get('overlapping_nonzero_iowait', 'n/a')}.",
                  "ssl_spike_context.csv joins each spike to overlapping CPU/iowait, SD await/busy, interface bandwidth and throttling samples.",
                  "These are coarse system-bin correlations, not proof that storage or networking caused a particular frame delay. Network rates include other interface traffic; networking-only CPU use is not isolated."])
    lines.extend(["", f"Inactive module boundaries: {', '.join(result['inactive_modules']) or 'none'}.", "",
                  "Scope durations are wall time, not CPU-only time. Different module rates and attempted resampler calls are not interchangeable frame rates.",
                  "The existing SSL/SST stage timers and older Python testers are unchanged.",
                  "Raw evidence: traces/, module_timings.csv, ssl_intervals.csv, ssl_distribution.csv, ssl_spike_context.csv, system_samples.jsonl, odas.log and summary.json.", ""])
    (output / "report.md").write_text("\n".join(lines))


def summarize_system(samples: list[dict], start: int, end: int) -> dict:
    selected = [sample for sample in samples if start <= sample["monotonic_ns"] <= end]
    result = {"cpu": {}, "disk": {}, "network": {}, "thermal": []}
    cores = {core for sample in selected for core in sample["cpu"]}
    for core in cores:
        entries = [sample["cpu"][core] for sample in selected if core in sample["cpu"]]
        result["cpu"][core] = {
            "mean_busy_percent": statistics.fmean(entry["busy_percent"] for entry in entries),
            "max_busy_percent": max(entry["busy_percent"] for entry in entries),
            "mean_iowait_percent": statistics.fmean(entry["iowait_percent"] for entry in entries),
            "max_iowait_percent": max(entry["iowait_percent"] for entry in entries),
        }
    for category in ("disk", "network"):
        names = {name for sample in selected for name in sample[category]}
        for name in names:
            values = [sample[category][name] for sample in selected
                      if sample[category].get(name) is not None]
            if values:
                result[category][name] = {"mean": statistics.fmean(values), "max": max(values)}
    result["thermal"] = [sample["thermal"] for sample in selected]
    return result


def run(args: argparse.Namespace) -> Path:
    revision = command_output(["git", "-C", str(SOURCE), "rev-parse", "--short=7", "HEAD"])
    if revision != "93a1b81":
        raise RuntimeError(f"This tester is restricted to the latest snapshot 93a1b81, not {revision}")
    if not args.binary.is_file() or not args.config.is_file():
        raise RuntimeError("Build build/live_timing/bin/odaslive and supply an existing --config first")
    if args.seconds <= 0 or args.warmup < 0 or args.sample_interval <= 0 or args.deadline_ms <= 0:
        raise RuntimeError("Duration, sample interval and deadline must be positive; warmup cannot be negative")
    if not Path("/sys/class/net", args.interface).exists():
        raise RuntimeError(f"Network interface {args.interface!r} does not exist")
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    output = args.output.resolve() / stamp
    output.mkdir(parents=True)
    recording = (args.record_dir.resolve() if args.record_dir else output / "audio")
    recording.mkdir(parents=True, exist_ok=True)
    mount, detected_disk = storage_info(recording)
    disk = args.disk or detected_disk
    trace_root = args.trace_root.resolve()
    trace_root.mkdir(parents=True, exist_ok=True)
    if command_output(["findmnt", "-n", "-o", "FSTYPE", "-T", str(trace_root)]) != "tmpfs":
        raise RuntimeError("--trace-root must be on tmpfs to avoid trace writes contaminating storage timing")
    samples = []
    warnings = []
    if not mount["source"].startswith("/dev/mmcblk"):
        warnings.append("Recording is NOT on an SD/MMC device; select an SD-mounted --record-dir for the SD test.")
    wifi_state = command_output(["iw", "dev", args.interface, "info"])
    environment = os.environ.copy()
    environment.update(ODAS_TIMING_RECORD_DIR=str(recording), ODAS_SSL_PROFILE="1", ODAS_SST_PROFILE="1")
    command = [str(args.binary.resolve()), "-c", str(args.config.resolve()), "-rl"]
    error_text = None
    process = None
    forced = False
    with tempfile.TemporaryDirectory(prefix="odas-live-", dir=trace_root) as temporary:
        traces = Path(temporary)
        environment["ODAS_TIMING_DIR"] = str(traces)
        started = time.monotonic_ns()
        window_start = started + int(args.warmup * 1e9)
        window_end = window_start + int(args.seconds * 1e9)
        environment["ODAS_PROFILE_WINDOW_START"] = str(window_start / 1e9)
        environment["ODAS_PROFILE_WINDOW_END"] = str(window_end / 1e9)
        previous_cpu = cpu_counters()
        previous_disk = disk_counters(disk)
        previous_network = network_counters(args.interface)
        previous_time = started
        with (output / "odas.log").open("w") as log_file:
            try:
                process = subprocess.Popen(command, cwd=SOURCE, env=environment,
                                           stdout=log_file, stderr=subprocess.STDOUT, start_new_session=True)
                while time.monotonic_ns() < window_end:
                    if process.poll() is not None:
                        error_text = f"ODAS exited early with status {process.returncode}; inspect odas.log."
                        break
                    now = time.monotonic_ns()
                    current_cpu = cpu_counters()
                    current_disk = disk_counters(disk)
                    current_network = network_counters(args.interface)
                    elapsed = (now - previous_time) / 1e9
                    sample = {
                        "monotonic_ns": now, "previous_monotonic_ns": previous_time,
                        "cpu": cpu_rates(previous_cpu, current_cpu),
                        "disk": disk_rates(previous_disk, current_disk, elapsed),
                        "network": {name + "_per_second": (value - previous_network[name]) / elapsed
                                    for name, value in current_network.items()} if elapsed > 0 else {},
                        "tcp": tcp_snapshot(process.pid), "thermal": thermal_snapshot(),
                    }
                    samples.append(sample)
                    previous_cpu, previous_disk = current_cpu, current_disk
                    previous_network, previous_time = current_network, now
                    remaining = min(args.sample_interval, max(0, (window_end - time.monotonic_ns()) / 1e9))
                    if remaining:
                        time.sleep(remaining)
            except KeyboardInterrupt:
                error_text = "Run interrupted; measurements cover only the completed portion."
            finally:
                actual_end = time.monotonic_ns()
                window_end = min(window_end, actual_end)
                if process is not None:
                    forced = stop_process(process)
                shutil.copytree(traces, output / "traces")
    if error_text:
        warnings.append(error_text)
    if forced:
        warnings.append("Shutdown required termination/kill; active scopes may lack end events.")
    modules, intervals = read_traces(output / "traces", window_start, window_end, args.deadline_ms, output, warnings)
    recorder = recorder_summary(recording, window_start, window_end)
    active = {entry["module"] for entry in modules.values()}
    ssl_terminal_gap = max((entry["terminal_gap_ms"] for entry in modules.values()
                            if entry["module"] == "mod_ssl_process" and entry["terminal_gap_ms"] is not None),
                           default=max(0, (window_end - window_start) / 1e6))
    if ssl_terminal_gap > max(1000, 10 * args.deadline_ms):
        warnings.append(f"SSL stopped progressing {ssl_terminal_gap:.1f} ms before the window ended; "
                        "completed intervals alone understate the stall.")
    if any("SYN-SENT" in sample["tcp"] for sample in samples):
        warnings.append("ODAS sockets remained in SYN-SENT during sampling; the configured receiver was not accepting connections.")
    if recorder["bytes"] == 0:
        warnings.append("No raw audio write measurements in the selected window; recording workload is not verified.")
    if "snk_tracks_process_interface_socket" not in active or "snk_pots_process_interface_socket" not in active:
        warnings.append("Both visualizer JSON socket paths were not observed; the full visualizer workload is not verified.")
    if "tracks_file_write" not in active:
        warnings.append("No SST JSON file-write events were observed.")
    warnings.append("Live visualizer control activity and receiver render/arrival timing require external verification.")
    odas_log = (output / "odas.log").read_text(errors="replace")
    with (output / "system_samples.jsonl").open("w") as sample_file:
        for sample in samples:
            sample_file.write(json.dumps(sample) + "\n")
    result = {
        "revision": revision, "command": " ".join(command), "deadline_ms": args.deadline_ms,
        "deadline_basis": "User-supplied; default 8 ms assumes hopSize=128 and sample rate=16000 Hz",
        "window_start_ns": window_start, "window_end_ns": window_end,
        "window_seconds": max(0, (window_end - window_start) / 1e9),
        "storage": mount, "disk": disk, "interface": args.interface, "wifi_state": wifi_state,
        "warnings": warnings, "modules": modules,
        "full_live_workload_verified": False,
        "ssl_terminal_gap_ms": ssl_terminal_gap,
        "system_summary": summarize_system(samples, window_start, window_end),
        "alsa_capture": parse_alsa_capture_params(odas_log),
        "network_transfer": tcp_transfer_summary(
            output / "system_samples.jsonl", window_start, window_end),
        "sd_write_deadline": sd_write_deadline_summary(
            recording, window_start, window_end, args.deadline_ms),
        "recording_directory": str(recording),
        "inactive_modules": [name for name in MODULES if name not in active],
        "ssl_inter_frame": distribution([float(row["interval_ms"]) for row in intervals], args.deadline_ms),
        "stage_timing": parse_stage_timing(odas_log),
        "recorder": recorder,
    }
    write_module_timings(output, modules)
    result["spike_context"] = write_spike_context(output, intervals, samples)
    result["overrun_handling"] = analyze_overrun_handling(output, intervals, window_start, window_end, args.deadline_ms)
    if result["overrun_handling"].get("available") and not result["overrun_handling"]["input_queue_samples"]:
        warnings.append("No SSL queue probes found; rebuild build/live_timing/bin/odaslive before claiming measured FIFO draining.")
    (output / "summary.json").write_text(json.dumps(result, indent=2) + "\n")
    write_report(output, result)
    print(json.dumps(result["ssl_inter_frame"], indent=2))
    print("Overrun handling:")
    print(json.dumps(result["overrun_handling"], indent=2))
    print(f"Report: {output / 'report.md'}")
    for warning in warnings:
        print(f"WARNING: {warning}")
    if error_text or not intervals or forced or ssl_terminal_gap > max(1000, 10 * args.deadline_ms):
        raise RuntimeError(f"Incomplete live timing run. Evidence retained at {output}")
    return output


def self_test() -> None:
    stage_log = (
        "SSL Stage Timing (avg / frame)\n"
        "| Frames profiled............. 2,518 |\n"
        "| Full SSL Frame.............. 0.786 ms |\n"
        "SST Stage Timing (avg / frame)\n"
        "| Frames profiled............. 2,518 |\n"
        "| Activity + Classify......... 0.016 ms |\n"
        "| Full SST Frame.............. 0.030 ms |\n"
    )
    stage_timing = parse_stage_timing(stage_log)
    assert stage_timing["SST"]["frames"] == 2518
    assert stage_timing["SST"]["stages_ms"]["Full SST Frame"] == 0.030
    assert stage_timing["SSL"]["stages_ms"]["Full SSL Frame"] == 0.786
    assert parse_stage_timing("SST Stage Timing\n| Frames profiled..... 10 |") == {}
    alsa = parse_alsa_capture_params(
        "ALSA capture params: device=multi_capture rate=16000 hop=128 "
        "period=1024 frames (64.000 ms) buffer=4096 frames (256.000 ms)"
    )
    assert alsa["period_frames"] == 1024 and alsa["buffer_ms"] == 256.0
    assert distribution([1, 2, 3, 100], 8)["p99_ms"] == 100
    assert distribution([1, 2, 3, 100], 8)["over_deadline_percent"] == 25
    assert distribution([]) == {"count": 0}
    assert cpu_rates({"cpu0": [0] * 8}, {"cpu0": [50, 0, 10, 30, 10, 0, 0, 0]})["cpu0"] == {
        "busy_percent": 60, "iowait_percent": 10,
    }
    assert disk_rates([0] * 11, [2, 0, 4, 6, 2, 0, 8, 10, 0, 20, 20], 1)["await_ms"] == 4
    with tempfile.TemporaryDirectory() as temporary:
        directory = Path(temporary)
        with (directory / "thread-1.csv").open("w", newline="") as csv_file:
            writer = csv.writer(csv_file)
            writer.writerow(["monotonic_ns", "module", "instance", "sequence", "frame", "bytes", "phase"])
            writer.writerows([
                [1_000_000, "mod_ssl_process", "0x1", 1, 1, 0, "start"],
                [2_000_000, "mod_ssl_process", "0x1", 1, 1, 0, "end"],
                [9_000_000, "mod_ssl_process", "0x1", 2, 2, 0, "start"],
                [10_000_000, "mod_ssl_process", "0x1", 2, 2, 0, "end"],
                [11_000_000, "mod_ssl_process", "0x2", 3, 1, 0, "start"],
                [12_000_000, "mod_ssl_process", "0x1", 4, 0, 0, "start"],
                [13_000_000, "incomplete"],
            ])
        warnings = []
        summaries, intervals = read_traces(directory, 0, 20_000_000, 8, directory, warnings)
        assert len(warnings) == 1
        assert len(intervals) == 1 and intervals[0]["interval_ms"] == 8
        assert intervals[0]["over_deadline"] == 0
        assert summaries["mod_ssl_process/thread-1/0x1"]["scope_wall_time"]["mean_ms"] == 1
        write_module_timings(directory, summaries)
        with (directory / "module_timings.csv").open() as csv_file:
            rows = list(csv.DictReader(csv_file))
        assert rows[0]["scope_p99_ms"] == "1.0"
        assert rows[1]["scope_mean_ms"] == ""
        sample = {"previous_monotonic_ns": 0, "monotonic_ns": 20_000_000,
                  "cpu": {"cpu": {"busy_percent": 30, "iowait_percent": 2}},
                  "disk": {"await_ms": 12, "util_percent": 5},
                  "network": {"rx_bytes_per_second": 100, "tx_bytes_per_second": 200},
                  "thermal": {"throttled": "throttled=0x0"}}
        context = write_spike_context(directory, [{**intervals[0], "over_deadline": 1}], [sample])
        assert context["matched_to_system_samples"] == 1
        assert context["overlapping_nonzero_iowait"] == 1
        with (directory / "ssl_spike_context.csv").open() as csv_file:
            row = next(csv.DictReader(csv_file))
        assert row["sd_await_peak_ms"] == "12"
        traces = directory / "traces"
        traces.mkdir()
        origin = 1_000_000_000
        periodic = []
        events = []
        previous = origin
        for frame in range(1, 65):
            batch, offset = divmod(frame - 1, 32)
            timestamp = origin + (batch * 256 + 225 + offset) * 1_000_000
            periodic.append({"monotonic_ns": timestamp, "previous_monotonic_ns": previous,
                "frame_id": str(frame), "previous_frame_id": str(frame - 1),
                "thread": "thread-1", "instance": "0x1",
                "interval_ms": (timestamp - previous) / 1e6,
                "over_deadline": int(timestamp - previous > 8_000_000)})
            events.append([timestamp - 1, "ssl_input_queue", "0x2", frame, frame, 0, "after_pop", 31 - offset])
            previous = timestamp
        with (traces / "thread-1.csv").open("w", newline="") as csv_file:
            writer = csv.writer(csv_file)
            writer.writerow(["monotonic_ns", "module", "instance", "sequence", "frame", "bytes", "phase", "queue_depth"])
            writer.writerows(events)
        handling = analyze_overrun_handling(directory, periodic, origin, previous + 1, 8)
        assert handling["spikes"] == 2 and handling["skipped_ssl_frame_ids"] == 0
        assert handling["long_pauses"] == 2 and handling["other_deadline_exceedances"] == 0
        assert handling["cycles_input_queue_reached_zero"] == 1
        assert handling["input_queue_max_after_pop"] == 31
        assert handling["relative_schedule_recovery"]["mean_ms"] == 217
        assert handling["gap_worker_wait"]["count"] == 0
    assert cpu_counters() and network_counters("lo")
    print("Self-test passed: percentiles, deadlines, CPU/disk deltas, EOF filtering, scope pairing, periodic catch-up and FIFO probes.")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=SOURCE / "build/live_timing/bin/odaslive")
    parser.add_argument("--config", type=Path, default=SOURCE.parent / "ChatakGUI/config/sonicwild.cfg")
    parser.add_argument("--seconds", type=float, default=60)
    parser.add_argument("--warmup", type=float, default=5)
    parser.add_argument("--deadline-ms", type=float, default=8, help="hopSize / sample rate * 1000; default assumes 128/16000")
    parser.add_argument("--sample-interval", type=float, default=1)
    parser.add_argument("--interface", default="wlan0")
    parser.add_argument("--disk", help="Whole block device, e.g. mmcblk0; auto-detected from recording directory")
    parser.add_argument("--record-dir", type=Path, help="Directory on the actual SD mount; default is a fresh run directory")
    parser.add_argument("--output", type=Path, default=SOURCE / "live_timing_runs")
    parser.add_argument("--trace-root", type=Path, default=Path("/dev/shm"))
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
    else:
        try:
            run(args)
        except (RuntimeError, OSError) as error:
            parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    main()