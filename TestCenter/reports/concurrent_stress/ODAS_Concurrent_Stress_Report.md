# ODAS Concurrent Stress Run

**Report generated:** 2026-10-08T11:16:16+05:30  
**Capture config:** `~/ChatakGUI/config/sonicwild.cfg`  
**Logical CPUs:** 4

## Schedule

Instances start 5 seconds apart in version order and are stopped together after the fourth has run for 15 seconds. The final 5 seconds after the fourth start are warm-up; stage counters are clipped to the common final 10-second measurement window.

**Shared measurement window:** 2026-10-08T11:16:06+05:30 to 2026-10-08T11:16:16+05:30

| Version | Started (local)           |
| :------ | :------------------------ |
| bcb8454 | 2026-10-08T11:15:46+05:30 |
| c603141 | 2026-10-08T11:15:51+05:30 |
| 18a8a73 | 2026-10-08T11:15:56+05:30 |
| 93a1b81 | 2026-10-08T11:16:01+05:30 |


## CPU by Concurrency

Host CPU is normalized to 0-100% of the machine. ODAS CPU sum uses 100% per logical core and sums active ODAS processes; it can exceed 100%.

| Concurrent versions   | Samples | Host CPU | ODAS CPU sum |
| :-------------------- | ------: | -------: | -----------: |
| 1 version             |       5 |    23.6% |        29.4% |
| 2 versions            |       5 |    19.5% |        64.6% |
| 3 versions            |       5 |    31.7% |       107.8% |
| 4 versions            |       5 |    41.2% |       143.5% |
| 4 versions, final 10s |      10 |    40.7% |       140.0% |


## CPU in Final Window

Per-process CPU is reported as a percentage of one logical core. The combined row is the sum across ODAS instances.

| Version            | Process CPU (one core = 100%) |
| :----------------- | ----------------------------: |
| bcb8454            |                         19.4% |
| c603141            |                         36.3% |
| 18a8a73            |                         41.8% |
| 93a1b81            |                         42.6% |
| All ODAS processes |                        140.0% |


## SSL Stage Timing (ms/frame)

| SSL stage                 | `bcb8454` | `c603141` | `18a8a73` | `93a1b81` |
| :------------------------ | --------: | --------: | --------: | --------: |
| Frames profiled           |      1250 |      1262 |      1235 |      1273 |
| Pre-XCorr                 |     0.104 |     0.133 |     0.135 |     0.170 |
| GCC-PHAT XCorr            |     0.476 |     0.436 |     0.389 |     0.448 |
| SRP Accumulation          |     0.421 |     0.485 |     0.431 |     0.309 |
| Peak Extraction + Mapping |     0.001 |     0.004 |     0.002 |     0.002 |
| Freq at Peak              |       N/A |     0.547 |     0.613 |     0.541 |
| Format DOA Angles         |     0.000 |     0.095 |     0.088 |     0.095 |
| Full SSL Frame            | **1.002** | **1.700** | **1.659** | **1.565** |


## SST Stage Timing (ms/frame)

| SST stage             | `bcb8454` | `c603141` | `18a8a73` | `93a1b81` |
| :-------------------- | --------: | --------: | --------: | --------: |
| Frames profiled       |      1250 |      1264 |      1235 |      1273 |
| Track Management      |     0.001 |     0.002 |     0.002 |     0.002 |
| Predict               |     0.000 |     0.000 |     0.000 |     0.000 |
| Coherence             |     0.000 |     0.000 |     0.000 |     0.000 |
| Mixture + Assignment  |     0.006 |     0.007 |     0.008 |     0.007 |
| Update                |     0.000 |     0.001 |     0.001 |     0.000 |
| Activity + Classify   |     0.000 |     0.001 |     0.001 |     0.001 |
| Transitions + Dynamic |     0.001 |     0.001 |     0.001 |     0.001 |
| Output Copy           |     0.001 |     0.001 |     0.001 |     0.001 |
| Full SST Frame        | **0.010** | **0.013** | **0.015** | **0.013** |


Raw process logs are stored alongside this report in `stage_runs/`. If an instance exits early or produces no profiled frames, the script stops the remaining instances and does not write a misleading report.
