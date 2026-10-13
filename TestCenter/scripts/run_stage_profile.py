#!/usr/bin/env python3
"""Sequentially profile the four prebuilt ODAS timing snapshots."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from datetime import datetime
import os
from pathlib import Path
import re
import signal
import subprocess
import sys


ROOT = Path(__file__).resolve().parent.parent
RESULT_DIR = ROOT / "reports" / "stage_timing"
REPORT = RESULT_DIR / "ODAS_Timing_Experiment_Summary.md"
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
PROFILE_ROW = re.compile(
    r"\|\s*(.*?)\.+\s*([\d,]+(?:\.\d+)?)\s*(ms)?\s*\|"
)
TABLE_SEPARATOR_CELL = re.compile(r":?-{3,}:?")


@dataclass(frozen=True)
class RunResult:
    label: str
    started: str
    log_path: Path
    ssl: dict[str, str]
    sst: dict[str, str]


def parse_stage_table(log_text: str, module: str) -> dict[str, str]:
    title = f"{module} Stage Timing"
    start = log_text.find(title)
    if start < 0:
        raise RuntimeError(f"{module} profiler summary was not found in the run log")

    end_marker = f"Full {module} Frame"
    end = log_text.find(end_marker, start)
    if end < 0:
        raise RuntimeError(f"{module} full-frame timing was not found in the run log")

    line_end = log_text.find("\n", end)
    if line_end < 0:
        line_end = len(log_text)
    block = log_text[start:line_end]
    rows: dict[str, str] = {}
    for line in block.splitlines():
        match = PROFILE_ROW.search(line)
        if match:
            label = match.group(1).strip()
            value = match.group(2).replace(",", "")
            rows[label] = value

    if int(rows.get("Frames profiled", "0")) <= 0:
        raise RuntimeError(f"{module} profiler found no processed frames")
    return rows


def run_profile(label: str, source: Path, binary: Path, seconds: int,
                config: Path, log_path: Path) -> RunResult:
    started_at = datetime.now().astimezone()
    started = started_at.isoformat(timespec="seconds")
    environment = os.environ.copy()
    environment["ODAS_SSL_PROFILE"] = "1"
    environment["ODAS_SST_PROFILE"] = "1"

    print(f"[{started}] Running {label} for {seconds}s; log: {log_path}", flush=True)
    with log_path.open("w", encoding="utf-8") as log_file:
        process = subprocess.Popen(
            [str(binary), "-c", str(config)],
            cwd=source,
            env=environment,
            stdout=log_file,
            stderr=subprocess.STDOUT,
            start_new_session=True,
        )
        try:
            process.wait(timeout=seconds)
        except subprocess.TimeoutExpired:
            process.send_signal(signal.SIGINT)
            try:
                process.wait(timeout=20)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
                raise RuntimeError(f"{label} did not stop cleanly after SIGINT")
        except KeyboardInterrupt:
            process.send_signal(signal.SIGINT)
            process.wait(timeout=20)
            raise

    if process.returncode not in (0, -signal.SIGINT):
        raise RuntimeError(
            f"{label} exited with status {process.returncode}; see {log_path}"
        )

    log_text = log_path.read_text(encoding="utf-8", errors="replace")
    ssl = parse_stage_table(log_text, "SSL")
    sst = parse_stage_table(log_text, "SST")
    if label == "bcb8454":
        ssl["Freq at Peak"] = "N/A"
    return RunResult(label, started, log_path, ssl, sst)


def render_table(title: str, stages: tuple[str, ...], results: list[RunResult],
                 field: str) -> str:
    lines = [
        f"| {title:<28} | " + " | ".join(f"`{result.label}`" for result in results) + " |",
        "|:" + "-" * 28 + "|" + "|".join("-" * 12 + ":" for _ in results) + "|",
    ]
    for stage in stages:
        values = [getattr(result, field).get(stage, "0.000") for result in results]
        display = [f"**{value}**" if stage.startswith("Full ") else value for value in values]
        lines.append(f"| {stage:<28} | " + " | ".join(display) + " |")
    return "\n".join(lines)


def normalize_markdown_tables(markdown: str) -> str:
    lines = markdown.splitlines()
    normalized: list[str] = []
    index = 0

    while index < len(lines):
        if index + 1 >= len(lines):
            normalized.append(lines[index])
            break

        header = lines[index].strip()
        separator = lines[index + 1].strip()
        if not (header.startswith("|") and header.endswith("|")):
            normalized.append(lines[index])
            index += 1
            continue

        header_cells = [cell.strip() for cell in header[1:-1].split("|")]
        separator_cells = [cell.strip() for cell in separator.strip("|").split("|")]
        if (len(header_cells) != len(separator_cells) or
                not all(TABLE_SEPARATOR_CELL.fullmatch(cell) for cell in separator_cells)):
            normalized.append(lines[index])
            index += 1
            continue

        rows = [header_cells]
        index += 2
        while index < len(lines):
            row = lines[index].strip()
            if not (row.startswith("|") and row.endswith("|")):
                break
            cells = [cell.strip() for cell in row[1:-1].split("|")]
            if len(cells) != len(header_cells):
                break
            rows.append(cells)
            index += 1

        alignments = []
        for cell in separator_cells:
            starts_colon = cell.startswith(":")
            ends_colon = cell.endswith(":")
            if starts_colon and ends_colon:
                alignments.append("center")
            elif ends_colon:
                alignments.append("right")
            else:
                alignments.append("left")

        widths = [
            max(3, *(len(row[column]) for row in rows))
            for column in range(len(header_cells))
        ]
        separator_line = (
            "| " + " | ".join(
                ":" + "-" * (width - 1) if alignment == "left" else
                "-" * (width - 1) + ":" if alignment == "right" else
                ":" + "-" * (width - 2) + ":"
                for width, alignment in zip(widths, alignments)
            ) + " |"
        )
        formatted_rows = []
        for row in rows:
            padded_cells = []
            for cell, width, alignment in zip(row, widths, alignments):
                if alignment == "right":
                    padded_cells.append(cell.rjust(width))
                elif alignment == "center":
                    padded_cells.append(cell.center(width))
                else:
                    padded_cells.append(cell.ljust(width))
            formatted_rows.append("| " + " | ".join(padded_cells) + " |")
        normalized.extend((formatted_rows[0], separator_line))
        normalized.extend(formatted_rows[1:])

    return "\n".join(normalized) + "\n"


def update_report(results: list[RunResult]) -> None:
    original = REPORT.read_text(encoding="utf-8")
    section_title = "## Stage Breakdown: Instrumented Reruns"
    prefix = original.split(section_title, maxsplit=1)[0].rstrip()
    generated_at = datetime.now().astimezone().isoformat(timespec="seconds")

    run_dates = [
        "| Version | Run started (local) |",
        "|:--|:--|",
    ]
    run_dates.extend(f"| `{result.label}` | {result.started} |" for result in results)

    section = [
        section_title,
        "",
        f"Generated: {generated_at}. Four existing Release snapshots were run sequentially for the configured duration against the live `multi_capture` input. Each process was stopped with `SIGINT` so it could print its profiler summaries.",
        "",
        *run_dates,
        "",
        render_table("SSL stage (ms/frame)", SSL_STAGES, results, "ssl"),
        "",
        render_table("SST stage (ms/frame)", SST_STAGES, results, "sst"),
        "",
        "`N/A` means that stage does not exist in the baseline implementation. Values are rounded to 0.001 ms, so small measurements display as 0.000. These live windows may not meaningfully exercise YAMNet classification; “Activity + Classify” combines activity and classifier work and is not an isolated YAMNet inference measurement. Stage totals cover SSL/SST module work, not end-to-end latency or worst-case timing.",
        "",
    ]
    temporary_report = REPORT.with_suffix(".md.tmp")
    report_text = prefix + "\n\n" + "\n".join(section)
    temporary_report.write_text(normalize_markdown_tables(report_text), encoding="utf-8")
    temporary_report.replace(REPORT)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--config",
        type=Path,
        help="live ODAS config for this Raspberry Pi",
    )
    parser.add_argument(
        "--seconds",
        type=int,
        default=15,
        help="live capture duration for each version (default: 15)",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="check configured paths and print build/run commands without executing them",
    )
    parser.add_argument(
        "--normalize-only",
        action="store_true",
        help="normalize table spacing in the existing report without running benchmarks",
    )
    args = parser.parse_args()
    if args.seconds < 1:
        parser.error("--seconds must be at least 1")
    if args.normalize_only:
        report_text = REPORT.read_text(encoding="utf-8")
        REPORT.write_text(normalize_markdown_tables(report_text), encoding="utf-8")
        print(f"Normalized table spacing in {REPORT}")
        return 0
    if args.config is None:
        parser.error("--config is required unless --normalize-only is used")
    config = args.config.expanduser().resolve()

    entries = []
    for label, directory, build_name in RUNS:
        source = ROOT / directory
        build = source / "build" / build_name
        binary = build / "bin" / "odaslive"
        if not source.is_dir() or not binary.is_file():
            raise SystemExit(f"Missing prebuilt binary for {label}: {binary}")
        entries.append((label, source, binary))

    if not config.is_file():
        raise SystemExit(f"Live input configuration not found: {config}")

    if args.dry_run:
        print(f"Input config: {config}")
        for label, source, binary in entries:
            print(f"{label}: {binary} -c {config} ({args.seconds}s, then SIGINT)")
        print(f"Report: {REPORT}")
        return 0

    log_dir = RESULT_DIR / "stage_runs"
    log_dir.mkdir(parents=True, exist_ok=True)
    run_id = datetime.now().astimezone().strftime("%Y%m%d_%H%M%S")

    results = []
    for label, source, binary in entries:
        log_path = log_dir / f"stage_profile_{run_id}_{label}.log"
        results.append(run_profile(label, source, binary, args.seconds, config, log_path))

    update_report(results)
    print(f"Updated report: {REPORT}")
    print(f"Raw run logs: {log_dir}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, subprocess.SubprocessError, RuntimeError) as error:
        print(f"Error: {error}", file=sys.stderr)
        raise SystemExit(1)