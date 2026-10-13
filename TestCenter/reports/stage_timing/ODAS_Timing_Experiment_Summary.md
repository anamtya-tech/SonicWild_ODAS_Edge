# ODAS Processing-Time Experiment

**Run date:** 2026-10-04  |  **Platform:** Raspberry Pi, ARM64  |  **Input:** `sonicwild.cfg`, live `multi_capture`

## Method

Four Release builds were run sequentially with the same configuration and physical audio input. An `LD_PRELOAD` probe measured successful, non-empty `mod_ssl_process` and `mod_sst_process` calls with `CLOCK_MONOTONIC`. Values below are average milliseconds per frame.

## Version Comparison

| Metric                 | Base `bcb8454`2024-06-18 | YAMNet `c603141`2026-05-19 | SST sidecar `18a8a73 2026-07-20 | Current `93a1b81`2026-09-06 |
| :--------------------- | -----------------------: | -------------------------: | ------------------------------: | --------------------------: |
| Frames                 |                    1,563 |                      1,266 |                           1,563 |                       1,469 |
| SSL average (ms/frame) |                 0.497371 |                   0.860922 |                        0.890678 |                    0.922171 |
| SST average (ms/frame) |                 0.012994 |                   0.016728 |                        0.014863 |                    0.088655 |
| SSL + SST / 8 ms frame |                    6.38% |                     10.97% |                          11.32% |                      12.64% |

The share is `(SSL average + SST average) / 8 ms x 100`; it is not end-to-end latency. Frame counts differ, so compare averages rather than total times.

## Stage Breakdown: Instrumented Reruns

Generated: 2026-10-08T18:42:37+05:30. Four existing Release snapshots were run sequentially for the configured duration against the live `multi_capture` input. Each process was stopped with `SIGINT` so it could print its profiler summaries.

| Version | Run started (local) |
|:--|:--|
| `bcb8454` | 2026-10-08T18:41:36+05:30 |
| `c603141` | 2026-10-08T18:41:51+05:30 |
| `18a8a73` | 2026-10-08T18:42:06+05:30 |
| `93a1b81` | 2026-10-08T18:42:21+05:30 |

| SSL stage (ms/frame)      | `bcb8454` | `c603141` | `18a8a73` | `93a1b81` |
| :------------------------ | --------: | --------: | --------: | --------: |
| Frames profiled           |      1766 |      1766 |      1782 |      1782 |
| Pre-XCorr                 |     0.061 |     0.078 |     0.080 |     0.084 |
| GCC-PHAT XCorr            |     0.257 |     0.236 |     0.225 |     0.221 |
| SRP Accumulation          |     0.238 |     0.213 |     0.216 |     0.143 |
| Peak Extraction + Mapping |     0.001 |     0.001 |     0.002 |     0.001 |
| Freq at Peak              |       N/A |     0.324 |     0.334 |     0.315 |
| Format DOA Angles         |     0.000 |     0.043 |     0.041 |     0.043 |
| Full SSL Frame            | **0.558** | **0.894** | **0.897** | **0.807** |

| SST stage (ms/frame)  | `bcb8454` | `c603141` | `18a8a73` | `93a1b81` |
| :-------------------- | --------: | --------: | --------: | --------: |
| Frames profiled       |      1766 |      1766 |      1781 |      1781 |
| Track Management      |     0.002 |     0.002 |     0.002 |     0.002 |
| Predict               |     0.000 |     0.000 |     0.000 |     0.000 |
| Coherence             |     0.000 |     0.000 |     0.000 |     0.000 |
| Mixture + Assignment  |     0.007 |     0.008 |     0.009 |     0.008 |
| Update                |     0.001 |     0.001 |     0.001 |     0.001 |
| Activity + Classify   |     0.000 |     0.001 |     0.001 |     0.001 |
| Transitions + Dynamic |     0.002 |     0.002 |     0.001 |     0.001 |
| Output Copy           |     0.001 |     0.001 |     0.001 |     0.001 |
| Full SST Frame        | **0.013** | **0.015** | **0.015** | **0.014** |

`N/A` means that stage does not exist in the baseline implementation. Values are rounded to 0.001 ms, so small measurements display as 0.000. These live windows may not meaningfully exercise YAMNet classification; “Activity + Classify” combines activity and classifier work and is not an isolated YAMNet inference measurement. Stage totals cover SSL/SST module work, not end-to-end latency or worst-case timing.
