#!/usr/bin/env python3
"""Run four prebuilt ODAS versions with staggered starts and collect CPU/stage data."""

from __future__ import annotations

import argparse
from dataclasses import dataclass, field
from datetime import datetime, timedelta
import os
from pathlib import Path
import signal
import statistics
import subprocess
import sys
import time

from run_stage_profile import normalize_markdown_tables, parse_stage_table


ROOT = Path(__file__).resolve().parent.parent
RESULT_DIR = ROOT / "reports" / "concurrent_stress"
REPORT = RESULT_DIR / "ODAS_Concurrent_Stress_Report.md"
RUNS = (
    ("bcb8454", "odas_20261004_bcb8454_base", "bench_20261004_bcb8454"),
    ("c603141", "chatak-odas_20261004_c603141", "bench_20261004_c603141"),
    ("18a8a73", "chatak-odas_20261004_18a8a73", "bench_20261004_18a8a73"),
    ("93a1b81", "chatak-odas_20261004_93a1b81", "bench_20261004_93a1b81"),
)
SSL_STAGES = (
    "Frames profiled",
    "Pre-XCorr",
    "GCC-PHAT XCorr",
    "SRP Accumulation",
    "Peak Extraction + Mapping",
    "Freq at Peak",
    "Format DOA Angles",
    "Full SSL Frame",
)
SST_STAGES = (
    "Frames profiled",
    "Track Management",
    "Predict",
    "Coherence",
    "Mixture + Assignment",
    "Update",
    "Activity + Classify",
    "Transitions + Dynamic",
    "Output Copy",
    "Full SST Frame",
)
CLOCK_TICKS = os.sysconf("SC_CLK_TCK")


@dataclass
class ProcessRun:
    label: str
    source: Path
    process: subprocess.Popen[bytes]
    log_path: Path
    log_file: object
    started: str
    final_window_cpu_percent_samples: list[float] = field(default_factory=list)
    ssl: dict[str, str] = field(default_factory=dict)
    sst: dict[str, str] = field(default_factory=dict)


@dataclass
class CpuPhase:
    label: str
    system_percent_samples: list[float] = field(default_factory=list)
    process_percent_samples: dict[str, list[float]] = field(default_factory=dict)


def monotonic_seconds() -> float:
    return time.monotonic()


def wait_until(deadline: float) -> None:
    while True:
        remaining = deadline - monotonic_seconds()
        if remaining <= 0:
            return
        time.sleep(min(0.1, remaining))


def read_system_cpu_ticks() -> tuple[int, int]:
    with Path("/proc/stat").open(encoding="ascii") as stat_file:
        cpu_line = stat_file.readline().split()
    values = [int(value) for value in cpu_line[1:9]]
    total = sum(values)
    idle = values[3] + values[4]
    return total, idle


def read_process_cpu_ticks(process_id: int) -> int | None:
    try:
        stat_line = Path(f"/proc/{process_id}/stat").read_text(encoding="ascii")
    except FileNotFoundError:
        return None
    fields = stat_line.rsplit(")", maxsplit=1)[1].split()
    return int(fields[11]) + int(fields[12])


def cpu_sample_percent(previous: tuple[int, int], current: tuple[int, int]) -> float:
    total_delta = current[0] - previous[0]
    busy_delta = total_delta - (current[1] - previous[1])
    return 100.0 * busy_delta / total_delta if total_delta > 0 else 0.0


def average(values: list[float]) -> float:
    return statistics.fmean(values) if values else 0.0


def sample_cpu_phase(
    phase: CpuPhase,
    runs: list[ProcessRun],
    start: float,
    end: float,
    sample_interval: float,
    is_final_window: bool = False,
) -> None:
    wait_until(start)
    previous_time = monotonic_seconds()
    previous_system = read_system_cpu_ticks()
    previous_process = {
        run.label: read_process_cpu_ticks(run.process.pid) for run in runs
    }

    while previous_time < end:
        wait_until(min(previous_time + sample_interval, end))
        current_time = monotonic_seconds()
        elapsed = current_time - previous_time
        current_system = read_system_cpu_ticks()
        phase.system_percent_samples.append(
            cpu_sample_percent(previous_system, current_system)
        )

        process_load = 0.0
        for run in runs:
            if run.process.poll() is not None:
                raise RuntimeError(
                    f"{run.label} exited during the stress run; inspect {run.log_path}"
                )
            current_ticks = read_process_cpu_ticks(run.process.pid)
            previous_ticks = previous_process[run.label]
            if current_ticks is not None and previous_ticks is not None and elapsed > 0:
                process_percent = (
                    100.0 * (current_ticks - previous_ticks) / CLOCK_TICKS / elapsed
                )
                if is_final_window:
                    run.final_window_cpu_percent_samples.append(process_percent)
                phase.process_percent_samples.setdefault(run.label, []).append(
                    process_percent
                )
                process_load += process_percent
            previous_process[run.label] = current_ticks

        previous_system = current_system
        previous_time = current_time
        print(
            f"{phase.label}: host {phase.system_percent_samples[-1]:.1f}% CPU, "
            f"ODAS processes {process_load:.1f}% of one core",
            flush=True,
        )


def launch_run(
    label: str,
    source: Path,
    binary: Path,
    config: Path,
    environment: dict[str, str],
    log_path: Path,
) -> ProcessRun:
    log_file = log_path.open("wb")
    started = datetime.now().astimezone().isoformat(timespec="seconds")
    process = subprocess.Popen(
        [str(binary), "-c", str(config)],
        cwd=source,
        env=environment,
        stdout=log_file,
        stderr=subprocess.STDOUT,
        start_new_session=True,
    )
    print(f"[{started}] Started {label} (PID {process.pid})", flush=True)
    return ProcessRun(label, source, process, log_path, log_file, started)


def stop_runs(runs: list[ProcessRun]) -> None:
    for run in runs:
        if run.process.poll() is None:
            run.process.send_signal(signal.SIGINT)
    for run in runs:
        if run.process.poll() is None:
            try:
                run.process.wait(timeout=20)
            except subprocess.TimeoutExpired:
                run.process.kill()
                run.process.wait()
        run.log_file.close()


def make_table(headers: list[str], rows: list[list[str]], right_aligned: set[int]) -> str:
    lines = [
        "| " + " | ".join(headers) + " |",
        "| " + " | ".join(
            "---:" if index in right_aligned else ":---"
            for index in range(len(headers))
        ) + " |",
    ]
    lines.extend("| " + " | ".join(row) + " |" for row in rows)
    return normalize_markdown_tables("\n".join(lines))


def render_stage_table(
    title: str,
    stages: tuple[str, ...],
    runs: list[ProcessRun],
    attribute: str,
) -> str:
    rows = []
    for stage in stages:
        values = [getattr(run, attribute).get(stage, "0.000") for run in runs]
        if stage == "Freq at Peak" and runs[0].label == "bcb8454":
            values[0] = "N/A"
        if stage.startswith("Full "):
            values = [f"**{value}**" for value in values]
        rows.append([stage, *values])
    return make_table([title, *(f"`{run.label}`" for run in runs)], rows,
                      set(range(1, len(runs) + 1)))


def phase_summary_rows(phases: list[CpuPhase]) -> list[list[str]]:
    rows = []
    for phase in phases:
        process_total = average([
            sum(values)
            for values in zip(*phase.process_percent_samples.values())
        ]) if phase.process_percent_samples else 0.0
        rows.append([
            phase.label,
            str(len(phase.system_percent_samples)),
            f"{average(phase.system_percent_samples):.1f}%",
            f"{process_total:.1f}%",
        ])
    return rows


def update_report(
    runs: list[ProcessRun],
    phases: list[CpuPhase],
    window_start: str,
    window_end: str,
    cpu_count: int,
    config: Path,
    report_path: Path,
) -> None:
    phase_table = make_table(
        ["Concurrent versions", "Samples", "Host CPU", "ODAS CPU sum"],
        phase_summary_rows(phases),
        {1, 2, 3},
    )
    final_cpu_rows = [
        [run.label, f"{average(run.final_window_cpu_percent_samples):.1f}%"]
        for run in runs
    ]
    final_cpu_rows.append([
        "All ODAS processes",
        f"{sum(average(run.final_window_cpu_percent_samples) for run in runs):.1f}%",
    ])
    final_cpu_table = make_table(
        ["Version", "Process CPU (one core = 100%)"], final_cpu_rows, {1}
    )
    start_rows = [[run.label, run.started] for run in runs]
    start_table = make_table(["Version", "Started (local)"], start_rows, set())

    stage_rows = [
        "## SSL Stage Timing (ms/frame)",
        "",
        render_stage_table("SSL stage", SSL_STAGES, runs, "ssl"),
        "",
        "## SST Stage Timing (ms/frame)",
        "",
        render_stage_table("SST stage", SST_STAGES, runs, "sst"),
    ]
    contents = [
        "# ODAS Concurrent Stress Run",
        "",
        f"**Report generated:** {datetime.now().astimezone().isoformat(timespec='seconds')}  ",
        f"**Capture config:** `{config}`  ",
        f"**Logical CPUs:** {cpu_count}",
        "",
        "## Schedule",
        "",
        "Instances start 5 seconds apart in version order and are stopped together after the fourth has run for 15 seconds. The final 5 seconds after the fourth start are warm-up; stage counters are clipped to the common final 10-second measurement window.",
        "",
        f"**Shared measurement window:** {window_start} to {window_end}",
        "",
        start_table,
        "",
        "## CPU by Concurrency",
        "",
        "Host CPU is normalized to 0-100% of the machine. ODAS CPU sum uses 100% per logical core and sums active ODAS processes; it can exceed 100%.",
        "",
        phase_table,
        "",
        "## CPU in Final Window",
        "",
        "Per-process CPU is reported as a percentage of one logical core. The combined row is the sum across ODAS instances.",
        "",
        final_cpu_table,
        "",
        *stage_rows,
        "",
        "Raw process logs are stored alongside this report in `stage_runs/`. If an instance exits early or produces no profiled frames, the script stops the remaining instances and does not write a misleading report.",
        "",
    ]
    report_path.write_text(normalize_markdown_tables("\n".join(contents)), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--config",
        type=Path,
        required=True,
        help="live ODAS config for this Raspberry Pi",
    )
    parser.add_argument("--stagger-seconds", type=float, default=5.0)
    parser.add_argument("--overlap-seconds", type=float, default=15.0)
    parser.add_argument("--profile-seconds", type=float, default=10.0)
    parser.add_argument("--sample-interval", type=float, default=1.0)
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    config = args.config.expanduser().resolve()

    if min(args.stagger_seconds, args.overlap_seconds, args.profile_seconds,
           args.sample_interval) <= 0:
        parser.error("all durations must be greater than zero")
    if args.profile_seconds >= args.overlap_seconds:
        parser.error("--profile-seconds must be shorter than --overlap-seconds")
    if not Path("/proc/stat").is_file():
        raise SystemExit("CPU sampling requires Linux /proc/stat")
    if not config.is_file():
        raise SystemExit(f"Live input configuration not found: {config}")

    entries = []
    for label, directory, build_name in RUNS:
        source = ROOT / directory
        binary = source / "build" / build_name / "bin" / "odaslive"
        if not source.is_dir() or not binary.is_file():
            raise SystemExit(f"Missing prebuilt binary for {label}: {binary}")
        entries.append((label, source, binary))

    if args.dry_run:
        print(f"Input config: {config}")
        print(f"Schedule: starts {args.stagger_seconds}s apart; fourth runs for {args.overlap_seconds}s")
        print(f"Stage window: final {args.profile_seconds}s of the all-four overlap")
        for label, _source, binary in entries:
            print(f"{label}: {binary}")
        print(f"Report: {REPORT}")
        return 0

    log_dir = RESULT_DIR / "stage_runs"
    log_dir.mkdir(parents=True, exist_ok=True)
    run_id = datetime.now().astimezone().strftime("%Y%m%d_%H%M%S")
    process_runs: list[ProcessRun] = []
    phases: list[CpuPhase] = []
    start_epoch = monotonic_seconds()
    start_wall = datetime.now().astimezone()
    fourth_start = start_epoch + args.stagger_seconds * (len(entries) - 1)
    profile_start_epoch = fourth_start + args.overlap_seconds - args.profile_seconds
    profile_end_epoch = fourth_start + args.overlap_seconds
    profile_start_wall = start_wall + timedelta(seconds=profile_start_epoch - start_epoch)
    profile_end_wall = start_wall + timedelta(seconds=profile_end_epoch - start_epoch)
    environment = os.environ.copy()
    environment["ODAS_SSL_PROFILE"] = "1"
    environment["ODAS_SST_PROFILE"] = "1"
    environment["ODAS_PROFILE_WINDOW_START"] = f"{profile_start_epoch:.9f}"
    environment["ODAS_PROFILE_WINDOW_END"] = f"{profile_end_epoch:.9f}"

    try:
        for index, (label, source, binary) in enumerate(entries):
            launch_target = start_epoch + index * args.stagger_seconds
            wait_until(launch_target)
            log_path = log_dir / f"concurrent_{run_id}_{label}.log"
            process_runs.append(
                launch_run(label, source, binary, config, environment, log_path)
            )
            phase_end = launch_target + args.stagger_seconds
            phase = CpuPhase(f"{index + 1} version{'s' if index else ''}")
            phases.append(phase)
            sample_cpu_phase(
                phase, process_runs, launch_target, phase_end, args.sample_interval
            )

        final_phase = CpuPhase(f"4 versions, final {args.profile_seconds:g}s")
        phases.append(final_phase)
        sample_cpu_phase(
            final_phase,
            process_runs,
            profile_start_epoch,
            profile_end_epoch,
            args.sample_interval,
            is_final_window=True,
        )
    except KeyboardInterrupt:
        print("Interrupted; stopping all ODAS instances...", file=sys.stderr)
        stop_runs(process_runs)
        return 130
    finally:
        if any(run.process.poll() is None for run in process_runs):
            stop_runs(process_runs)

    for run in process_runs:
        log_text = run.log_path.read_text(encoding="utf-8", errors="replace")
        run.ssl = parse_stage_table(log_text, "SSL")
        run.sst = parse_stage_table(log_text, "SST")

    update_report(
        process_runs,
        phases,
        profile_start_wall.isoformat(timespec="seconds"),
        profile_end_wall.isoformat(timespec="seconds"),
        os.cpu_count() or 1,
        config,
        REPORT,
    )
    print(f"Updated report: {REPORT}")
    print(f"Raw logs: {log_dir}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, subprocess.SubprocessError, RuntimeError, ValueError) as error:
        print(f"Error: {error}", file=sys.stderr)
        raise SystemExit(1)
