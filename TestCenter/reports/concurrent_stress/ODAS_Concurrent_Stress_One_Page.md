# ODAS Concurrent Stress Test

**Purpose:** Measure how four ODAS versions behave when run together, using per-stage timing and CPU sampling. This tests concurrent load; it is not a comparison of identical code paths because each snapshot has different features.

## Test Setup

Four prebuilt Release versions ran against the live `multi_capture` input on an ARM64 system reporting four logical CPUs. The first starts at 0 seconds; each next version starts 5 seconds after the previous one. Once the fourth starts, all four stay running together for 15 seconds, then they are stopped together.

| Time after first start | Action | Versions running |
|:--|:--|--:|
| 0 s | Start `bcb8454` | 1 |
| 5 s | Start `c603141` | 2 |
| 10 s | Start `18a8a73` | 3 |
| 15 s | Start `93a1b81`; begin all-four overlap | 4 |
| 20 s | Begin final 10-second measurement window | 4 |
| 30 s | Stop all four | 0 |

The first 5 seconds with all four running are warm-up. Stage timings and per-process CPU in the results use the final 10 seconds; CPU is also sampled during each 5-second launch phase.

## Results

| Version | Frames | SSL (ms/frame) | SST (ms/frame) | SSL + SST (ms/frame) | CPU (% of one core) |
|:--|--:|--:|--:|--:|--:|
| `bcb8454` | 1,250 | 1.002 | 0.010 | 1.012 | 19.4% |
| `c603141` | 1,262 | 1.700 | 0.013 | 1.713 | 36.3% |
| `18a8a73` | 1,235 | 1.659 | 0.015 | 1.674 | 41.8% |
| `93a1b81` | 1,273 | 1.565 | 0.013 | 1.578 | 42.6% |

| Concurrent ODAS versions | Host CPU | Sum of ODAS CPU |
|:--|--:|--:|
| 1 | 23.6% | 29.4% |
| 2 | 19.5% | 64.6% |
| 3 | 31.7% | 107.8% |
| 4 | 41.2% | 143.5% |
| 4, final measurement window | 40.7% | 140.0% |

Host CPU is a percentage of the whole machine. Per-process CPU is a percentage of one logical core, so 140% combined means about 1.4 cores in use, not 140% of the four-core machine. The per-process percentages are rounded.

## Summary

SSL was the main processing cost in every version; SST averaged 0.010–0.015 ms/frame. In the final concurrent window, SSL ranged from 1.002 to 1.700 ms/frame. The current `93a1b81` snapshot measured 1.565 ms/frame for SSL and 1.578 ms/frame for SSL plus SST, lower than `c603141` in this run. Across the final window, the four processes used roughly 1.4 logical cores combined while host-wide CPU averaged 40.7%.

The current version's largest SSL stage was Freq at Peak at 0.541 ms/frame. The reported Activity + Classify stage was about 0.001 ms/frame, which does not establish standalone YAMNet inference cost. Stage totals cover SSL/SST modules only, not full-pipeline latency or worst-case timing. Results are from one 10-second window and should be treated as a load snapshot, not a statistical benchmark.

**Full run details:** [ODAS_Concurrent_Stress_Report.md](ODAS_Concurrent_Stress_Report.md)