# ODAS Raspberry Pi Test Center

This folder keeps four ODAS source snapshots and their reusable timing tools,
reports, and selected measurement evidence together. It is a source-and-results
archive, not a nested Git repository.

## Layout

```text
TestCenter/
├── odas_20261004_bcb8454_base/       # upstream ODAS baseline, bcb8454
├── chatak-odas_20261004_c603141/     # YAMNet JSON-output revision, c603141
├── chatak-odas_20261004_18a8a73/     # SST sidecar revision, 18a8a73
├── chatak-odas_20261004_93a1b81/     # latest revision, 93a1b81
├── scripts/
│   ├── run_stage_profile.py
│   ├── run_concurrent_stress.py
│   └── timing_probe.c
└── reports/
    ├── live_timing/
    ├── concurrent_stress/
    └── stage_timing/
```

The snapshots preserve the source files as captured, including local SSL/SST
timing instrumentation edits in the three older checkouts and the full live
timing instrumentation in `93a1b81`. Their original Git metadata is not
duplicated here; the short revision IDs above identify each snapshot's base
revision. Generated build trees, raw audio, classifier output, Python caches,
and bulk live trace directories are excluded. Model files and source
documentation are retained. Device-specific identifiers and absolute local
home-directory paths in the measurement reports have been redacted for sharing;
the timing measurements themselves are unchanged.

## Build the four stage-profile executables

Use a Raspberry Pi with the dependencies listed in each snapshot's build
documentation. Supply the ODAS configuration that is valid on that Pi; the
scripts deliberately do not bundle a machine-specific `ChatakGUI` config.

From this directory, build all four Release executables:

```bash
cmake -S odas_20261004_bcb8454_base \
  -B odas_20261004_bcb8454_base/build/bench_20261004_bcb8454 \
  -DCMAKE_BUILD_TYPE=Release
cmake --build odas_20261004_bcb8454_base/build/bench_20261004_bcb8454 \
  --target odaslive -j2

cmake -S chatak-odas_20261004_c603141 \
  -B chatak-odas_20261004_c603141/build/bench_20261004_c603141 \
  -DCMAKE_BUILD_TYPE=Release
cmake --build chatak-odas_20261004_c603141/build/bench_20261004_c603141 \
  --target odaslive -j2

cmake -S chatak-odas_20261004_18a8a73 \
  -B chatak-odas_20261004_18a8a73/build/bench_20261004_18a8a73 \
  -DCMAKE_BUILD_TYPE=Release
cmake --build chatak-odas_20261004_18a8a73/build/bench_20261004_18a8a73 \
  --target odaslive -j2

cmake -S chatak-odas_20261004_93a1b81 \
  -B chatak-odas_20261004_93a1b81/build/bench_20261004_93a1b81 \
  -DCMAKE_BUILD_TYPE=Release
cmake --build chatak-odas_20261004_93a1b81/build/bench_20261004_93a1b81 \
  --target odaslive -j2
```

## Run the stage timing tests

The stage tester runs the four builds sequentially. The concurrent stress
tester starts them in a staggered schedule and samples host/process CPU while
they overlap. Both need a working live input and receiver configuration.

```bash
python3 scripts/run_stage_profile.py \
  --config /path/to/your/sonicwild.cfg --seconds 15

python3 scripts/run_concurrent_stress.py \
  --config /path/to/your/sonicwild.cfg
```

Use `--dry-run` on either script to check the expected executable paths and
configuration before starting live capture. Reports and run logs are written
under `reports/stage_timing/` and `reports/concurrent_stress/`.

## Run the live wall-clock timing test

The latest snapshot has a separate tester for frame pacing, module timing,
queue depth, system counters, recording writes, and socket-send timing. Build
its instrumented executable and run its self-test before a live measurement:

```bash
cd chatak-odas_20261004_93a1b81
cmake -S . -B build/live_timing -DCMAKE_BUILD_TYPE=Release
cmake --build build/live_timing --target odaslive -j2
python3 scripts/run_live_timing.py --self-test
python3 scripts/run_live_timing.py \
  --config /path/to/your/sonicwild.cfg \
  --seconds 120 --warmup 10 --interface wlan0
```

The tester writes to `live_timing_runs/` by default. For a meaningful live
workload, start the intended receiver, use the Pi's normal capture/network
configuration, and verify bidirectional receiver/control activity separately.
Read [docs/LIVE_TIMING.md](./chatak-odas_20261004_93a1b81/docs/LIVE_TIMING.md)
before interpreting measurements. Timing intervals are not by themselves proof
of audio loss or microphone-to-screen rendering latency.

## Included reports

- [Live timing report](./reports/live_timing/run_20261008T143126.057422Z/report.md)
  and its summary, interval/distribution CSVs, module timing, queue/burst
  evidence, and timestamped system samples. This is the 120-second run whose
  report was shared with this package; bulky raw traces/audio are not included.
- [Runtime measurement guide/report](./reports/live_timing/ODAS_Runtime_Measurement_Report_20261008.md).
- [Concurrent stress report](./reports/concurrent_stress/ODAS_Concurrent_Stress_Report.md)
  and [one-page PDF](./reports/concurrent_stress/ODAS_Concurrent_Stress_One_Page.pdf).
- [Stage timing experiment report](./reports/stage_timing/ODAS_Timing_Experiment_Summary.md)
  and [PDF](./reports/stage_timing/ODAS_Timing_Experiment_Summary.pdf).

The live timing report is provided in Markdown with its machine-readable
evidence; no PDF version of that report was present in the source files.
