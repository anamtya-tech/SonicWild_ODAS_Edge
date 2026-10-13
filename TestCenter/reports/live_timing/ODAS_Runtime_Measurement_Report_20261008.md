# ODAS Runtime Measurement Guide for Raspberry Pi 4: Measured Report

*What was measured, how to interpret it, and which operating test cases remain
to be verified. Answers appear directly beneath each guide question.*

**Report date:** 8 October 2026.  
**Hardware:** Raspberry Pi 4 Model B Rev 1.5, verified from the device tree.  
**Code:** latest instrumented checkout, revision `93a1b81`. Older checkouts,
older Python testers and their prebuilt binaries remain unchanged.  
**Primary evidence:** run `20261008T121911.770342Z`, with a five-second excluded
warm-up and a **120.000-second measured window**.

## Executive Answer

**ODAS maintained average throughput and repeatedly caught up after periodic
input/worker waits. It did not deliver an evenly spaced frame every 8 ms.**

- SSL processed 15,000 frame entries, giving 14,999 inter-frame intervals.
- Mean interval was **7.986 ms**; p99 was **224.379 ms**; maximum was
  **230.797 ms**. **480 intervals, or 3.200%, exceeded 8 ms**.
- Of those events, **479 were long pauses** above the descriptive 80 ms
  threshold; one was a **9.846 ms** exceedance. The actual deadline remains 8 ms.
- Long pauses recurred approximately every **250.013 ms**, mostly every
  31-32 generated SSL frame IDs.
- The monitored SSL input FIFO reached zero in **478 of 478 complete
  long-pause/burst cycles**, with an after-pop peak of **30 messages**.
- No skipped, duplicated or reversed generated SSL frame IDs were observed.
  The first-to-last ten-second median relative schedule change was
  **-0.967 ms**, supporting catch-up rather than accumulating schedule lag in
  the measured window.
- This is **not** proof of zero physical audio loss, bounded delay in every
  pipeline queue, or a measured microphone-to-visualizer rendering latency.
- Live capture, JSON transmission and SD recording were exercised. The
  complete hotspot, bidirectional control and additional-UI workload was
  **not fully verified**.

## 1. The Real Question: Does It Keep Up, End to End?

> ODAS's own stage-level timers only measure CPU-busy time inside the algorithm.
> They do not capture all the effects of WiFi, live visualizer traffic, SD
> recording and additional UI interactions competing for system resources.

**Answer and correction:** the existing SSL/SST stage timers in this checkout
use `clock_gettime(CLOCK_MONOTONIC)`. They measure **elapsed wall-clock time
inside timed algorithm regions**, not strictly CPU-busy time. Preemption or
blocking inside those regions can be included; waits outside them are not.

The concern is nevertheless valid: stage averages alone do not describe
capture pacing, input/output queue waits, delayed kernel writeback, TCP
blocking, or receiver rendering. The new tester therefore retains the original
stage timers and also measures module entry/exit times, frame-entry pacing,
worker waits, FIFO depth, recording writes and timestamped system counters.

**What “end to end” means here:** we have measured device-side runtime
behavior, not the complete microphone-to-screen path. SSL entry intervals
include upstream delivery and scheduling effects. A downstream stall affects
SSL only after backpressure reaches it. Receiver-side arrival/render timestamps
are still required to establish actual displayed-result latency.

### The Deadline That Matters

> Frame period = hopSize / sampleRate. Confirm that 16 kHz with a 128-sample
> hop gives an 8 ms frame budget. [SSL1]

**[SSL1] Confirmed against the active configuration:**

| Setting | Observed value |
|:--|--:|
| `raw.fS` | 16,000 Hz |
| `raw.hopSize` | 128 samples |
| `general.samplerate.mu` | 16,000 Hz |
| `general.size.hopSize` | 128 samples |

$$
T_{frame}=\frac{128}{16000}=0.008\ \mathrm{s}=8\ \mathrm{ms}
$$

The nominal rate is **125 frames/second**, or **15,000 frames in 120 seconds**.
The observed SSL entry count matches that nominal count. Matching throughput
does not establish low latency or prove lossless physical capture.

### Why Stage Averages Are Not Enough

> A reference run averaged 1.079 ms for SSL+SST, about 13.5% of the 8 ms budget.
> Does that establish real-time operation under the full workload?

**Answer: no.** The arithmetic for the quoted reference is correct:
`1.079 / 8 * 100 = 13.4875%`. However, its provenance is not established by the
current run and it must not be presented as the latest measurement.

The primary run's original stage-timer totals were:

| Stage-timer total | Average ms/frame |
|:--|--:|
| SSL | 0.795 |
| SST | 0.014 |
| SSL + SST, sum of reported averages | 0.809 |
| Fraction of the 8 ms budget | 10.11% |

These low algorithm-stage averages coexist with approximately 224 ms p99
frame-entry gaps. The traced full SSL scope also remained below 8 ms in this
run: mean **0.865 ms**, p99 **2.346 ms**, maximum **7.722 ms**. Consequently,
the long entry gaps must not be described as 224 ms of SSL computation.

Stage averages and parallel/nested scope averages are not a substitute for
pipeline latency and must not be summed indiscriminately.

## 2. What to Measure: Inline Answers

### 1. Inter-Frame Wall-Clock Interval

> Log a monotonic timestamp at the start of each SSL frame during a live run
> with hotspot, visualizer and SD writing active. Measure consecutive deltas,
> not only stage duration.

**Implemented and measured:** the SSL processing entry hook records
`CLOCK_MONOTONIC` nanoseconds and generated frame IDs. Consecutive timestamps
within the measured window produce the reported deltas. The five-second
warm-up and post-window shutdown are excluded.

| Observed workload component | Status |
|:--|:--|
| Live microphone capture through `multi_capture` | Exercised |
| SSL JSON socket, configured port 9003 | Exercised |
| SST JSON socket, configured port 9002 | Exercised |
| ODAS TCP samples | Established connections observed |
| Raw audio recording with `-rl` | Exercised on SD |
| SST JSON recording sidecar | Exercised |
| Pi-hosted WiFi hotspot/AP | Not confirmed; `wlan0` reported managed mode |
| Live bidirectional visualizer control messages | Not independently verified |
| Added UI interaction workload | Not scripted or independently verified |
| Receiver arrival/render timing | Not measured |

SSL frame-entry timing measures **local processing-start pacing**, not the
acquisition time of the corresponding microphone samples. The configuration
uses ALSA `dsnoop`; the exact negotiated ALSA period/buffer setting responsible
for the recurring delivery pattern has not been established.

**Boundary observation:** the final measured SSL entry occurred **123.696 ms**
before the measurement-window end. This right-censored terminal gap has no
following frame in the window and is reported separately, not invented as a
completed inter-frame interval.

### 2. Full Distribution: Mean, p95, p99 and Maximum

> Do not rely on averages. Report the complete distribution and the tail events
> that may exceed the frame deadline.

**Measured results:** all values below are milliseconds. Percentiles use the
nearest-rank method. p95 means 95% of observed values are at or below that
value; p99 means 99%. Neither is a frame-loss percentage.

| Local frame-entry stream | Intervals | Mean | p95 | p99 | Maximum |
|:--|--:|--:|--:|--:|--:|
| SSL | 14,999 | 7.986 | 2.283 | 224.379 | 230.797 |
| SST | 14,999 | 7.986 | 2.659 | 225.328 | 232.750 |

For SSL, **480 / 14,999 = 3.200%** of completed intervals exceeded 8 ms.
The full ordered empirical distribution and every raw delta are retained, not
only these summary statistics.

**Interpretation:** the low p95 and much larger p99/mean reflect quick bursts
separated by long pauses. Average throughput keeps up, but smooth eight-ms
delivery and a hard real-time latency guarantee are not demonstrated.

#### Are the ~3% Events Just Startup or Shutdown?

**No: they persist in the middle of the measurement.** Warm-up was already
excluded before applying the following additional boundary trim:

| Measured-window subset | Intervals | Overruns | Overrun percentage |
|:--|--:|--:|--:|
| First 5 seconds | 624 | 19 | 3.045% |
| Middle, excluding 5 seconds at each edge | 13,749 | 439 | 3.193% |
| Last 5 seconds | 624 | 20 | 3.205% |

Intervals crossing a trim boundary are excluded from the relevant trimmed
subset, so these rows need not add to the full-run interval count.

An earlier unchanged-probe duration comparison also showed:

| Earlier run ID | Seconds | Intervals | Overruns | Percentage | Median spacing between overruns |
|:--|--:|--:|--:|--:|--:|
| `20261008T115126.550285Z` | 60 | 7,499 | 239 | 3.187% | 250.012 ms |
| `20261008T115454.348505Z` | 120 | 14,999 | 479 | 3.194% | 250.010 ms |

Counts nearly doubled when duration doubled, with approximately 40 events in
each ten-second bin. That is a recurring runtime pattern, not a fixed
startup/shutdown cost. These earlier runs predate the extra queue/wait probes;
they establish recurrence, not an instrumentation-overhead benchmark.

#### What Are These Intervals, and How Does ODAS Handle Them?

**Measured classification in the primary run:**

| Evidence | Result |
|:--|:--|
| All intervals exceeding the actual 8 ms deadline | 480 |
| Long pauses above descriptive threshold 80 ms | 479 |
| Other deadline exceedance | One, 9.846 ms |
| Median spacing between long pauses | 250.013 ms |
| Spacing in generated SSL IDs, across all gap events | 31: 358 cases; 32: 119; 5: 1; 26: 1 |
| Gap-associated SSL worker wait, mean / maximum | 220.435 / 230.118 ms |
| Complete long-pause/burst cycles | 478 |
| Cycles where monitored SSL input FIFO reached zero | 478 / 478 |
| FIFO after-pop samples / peak queued messages | 15,000 / 30 |
| Mean post-pause burst span | 28.348 ms |
| Mean relative schedule recovery per complete cycle | 213.643 ms |
| Skipped generated SSL IDs | 0 |
| Duplicate/reversed generated SSL-ID transitions | 0 |
| Last-minus-first ten-second median schedule-residual change | -0.967 ms |

**Handling in the inspected code:** workers consume FIFO messages in order.
Empty queues make consumers wait; full bounded queues block producers and
propagate backpressure. Asynchronous queues in this checkout are configured
with 100 message slots. The SSL worker does not reject a message for being late
or discard it merely because the tester observes an interval over 8 ms.

The new worker-wait measurement includes waiting for spectra input, CHATAK
input and an output slot. It does not isolate each wait or attribute all of
that time specifically to ALSA.

**Measured conclusion:** the monitored SSL FIFO repeatedly drained, generated
IDs progressed continuously, and relative schedule medians did not shift
later by even one frame period over this run. This supports **repeated
catch-up, not steadily accumulating schedule lag in the observed window**.

The schedule residual is elapsed time minus generated-frame progression times
8 ms, referenced to the first measured frame. Its observed range was
**-225.580 to 4.226 ms**. Negative values are relative to that arbitrary
reference; they do not mean negative physical latency.

The roughly 250 ms periodicity, approximately 249 ms capture-source tail, and
long worker waits are consistent with upstream capture batching/blocking.
They do not prove the exact ALSA/device cause. Only one input FIFO was probed;
snapshots can miss short-lived peaks, and other queues were not profiled for
depth. Generated IDs and preserved FIFO order do not establish zero sample
loss in hardware or ALSA buffers. A measurement deadline exceedance is not the
same as an ALSA xrun.

### 3. Per-Core CPU Utilisation

> Use `mpstat -P ALL 1` or `htop` to distinguish compute saturation from
> blocking/I/O waits.

**Implemented equivalent:** approximately one-second `/proc/stat` deltas,
with CPU-busy and iowait percentages separated. Samples include all host
activity, not just ODAS or the recorder. Means below are means of valid sampled
percentages. Busy excludes idle and iowait.

| CPU | Mean busy % | Maximum sampled busy % | Mean iowait % | Maximum sampled iowait % |
|:--|--:|--:|--:|--:|
| Whole-system aggregate | 13.029 | 25.231 | 0.004 | 0.246 |
| Core 0 | 13.740 | 35.514 | 0.008 | 0.962 |
| Core 1 | 12.787 | 29.808 | 0.008 | 0.971 |
| Core 2 | 12.807 | 29.808 | 0.008 | 0.971 |
| Core 3 | 12.746 | 23.810 | 0.000 | 0.000 |

**Interpretation:** these samples show no sustained saturated core during this
run. The regular long delivery gaps coexist with substantial idle CPU time,
which supports investigating input delivery/blocking rather than assuming
insufficient SSL compute capacity. One-second sampling can hide brief
saturation; it is not a measurement of per-thread or network-only CPU cost.

### 4. I/O Wait and SD Read/Write Behaviour

> Watch iowait and SD-device await. Coincidence with frame spikes can point
> toward the storage path.

**Implemented equivalent:** timestamped `/proc/diskstats` deltas for the actual
recording device, plus the CPU iowait measurements above. The recording mount
was **`/dev/mmcblk0p2`, ext4, mounted at `/`**, and the monitored whole SD device
was **`mmcblk0`**. This is not the GUI's USB recording mount `/dev/sda1`.

Rates use decimal kB/s and MB/s. These counters cover the whole SD device,
including OS and other-process activity. Await combines completed reads and
writes; the reported mean is the mean of valid sampling-bin await values, not
an operation-weighted latency across the complete run.

| SD-device metric | Mean of sampled values | Maximum sampled value |
|:--|--:|--:|
| Read bandwidth | 0.471 kB/s | 30.814 kB/s |
| Write bandwidth | 0.382 MB/s | 14.358 MB/s |
| Completed-operation await | 40.584 ms | 652.833 ms |
| Device busy time | 1.868% | 60.492% |
| Operations in flight at sampling instants | 0 | 0 |

Zero in-flight snapshots do not mean that no I/O occurred between samples.
Small observed SD reads do not establish an ODAS audio-replay workload; this
run captured live microphones and recorded audio.

**Recorder-specific application measurements:**

| Operation | Calls / intervals | Mean ms | p95 ms | p99 ms | Maximum ms |
|:--|--:|--:|--:|--:|--:|
| Buffered audio-file write | 15,000 | 0.016 | 0.058 | 0.089 | 1.620 |
| Recorder microphone-read start interval, not SD read | 14,999 | 8.001 | 124.079 | 124.499 | 248.647 |

Audio written during the window was **23,040,000 bytes = 23.04 MB**, matching
120 seconds at 16,000 Hz, six channels and 16 bits/sample. This agreement alone
does not establish lossless microphone acquisition.

Application writes are buffered: they can complete quickly while dirty pages
are written later. They measure neither `fsync` durability nor the recorder's
isolated CPU consumption. Consequently, the 1.620 ms application-write maximum
and a 652.833 ms sampled SD await are not contradictory.

All 480 SSL spike events overlapped valid system sampling bins. **Ten**
overlapped bins with nonzero host iowait. This is **coarse temporal correlation,
not causal proof**: multiple frames can share one bin, background I/O is
included, and kernel writeback need not block the recorder or SSL worker.

The repeated 250 ms pattern also persisted in runs where recorder write times
were much lower than in other runs. Current evidence does not identify SD
writing as the cause of the recurring majority of SSL entry gaps.

### 5. Network-Side Load

> Confirm visualizer-link bandwidth and CPU, and whether traffic is bursty
> or steady, using `nethogs`, `iftop` or equivalent measurements.

**Implemented measurements:** `/sys/class/net/wlan0/statistics` counter
deltas for bandwidth, packet rates and drops; `ss -tinp` records ODAS TCP
states, queues and available TCP diagnostic fields. Established connections
were observed, and both JSON sink paths were executed.

| WiFi-interface metric | Mean | Maximum sampled value |
|:--|--:|--:|
| RX bandwidth | 2.340 kB/s | 9.314 kB/s |
| TX bandwidth | 62.549 kB/s | 77.788 kB/s |
| RX packets/second | 37.100 | 75.467 |
| TX packets/second | 62.760 | 120.591 |
| RX dropped packets/second | 0.000 | 0.000 |
| TX dropped packets/second | 0.000 | 0.000 |

**Interpretation and limits:** these are interface-wide rates and include
traffic such as SSH and acknowledgements. RX is not proof of application-level
visualizer commands. Neither these counters nor whole-system CPU percentages
isolate visualizer-only bandwidth, networking CPU cost, packet-delivery
latency, receiver render delay or every TCP retransmission.

The interface reported **managed mode**, SSID `<redacted-ssid>`, on channel 1.
Pi-hosted hotspot/AP mode is therefore not demonstrated. Connection to an
external hotspot is also not established solely by the mode/SSID observation.
One-second rate bins can hide short bursts. Correlating those bins with frame
spikes is useful for investigation, not proof that WiFi caused the pauses.

### 6. Thermal/Clock Throttling

> Repeat `vcgencmd get_throttled`, `measure_temp` and `measure_clock arm` while
> concurrent workload heats the SoC.

**Measured:** 116 thermal/clock samples fell inside the measured window.

| Metric | Observed result |
|:--|:--|
| Temperature | 59.4-62.3 degrees C |
| ARM clock | Approximately 0.600-1.800 GHz |
| `get_throttled` | `0x0` in every measured sample |

**Correction to the guide:** a nonzero bitmask does not necessarily mean
current thermal throttling. It can identify undervoltage, frequency capping,
thermal throttling, soft temperature limits, or historical occurrences.
Current conditions use bits 0-3; corresponding historical flags use bits
16-19. A reduced clock alone may be normal frequency scaling.

No current or historical throttling flags were reported by the sampled masks
in this run. This two-minute measurement is not a ten-minute thermal-soak
certification and cannot guarantee behavior at higher ambient temperature.

## 3. Timing Details for ALL Processing Modules

These are **entry-to-exit elapsed wall times**, in milliseconds, not
inter-frame intervals or pure CPU times. Each row has 15,000 completed scope
measurements. Separate instances retain their thread IDs; they are not pooled
into misleading averages. No processing-module boundary was inactive.

| Module / thread | Mean ms | p95 ms | p99 ms | Maximum ms |
|:--|--:|--:|--:|--:|
| `mod_chatak_process` / 146686 | 0.208 | 0.877 | 1.954 | 5.527 |
| `mod_classify_process` / 146714 | 0.012 | 0.022 | 0.064 | 1.855 |
| `mod_istft_process` / 146699 | 0.068 | 0.113 | 0.222 | 3.249 |
| `mod_istft_process` / 146700 | 0.067 | 0.112 | 0.237 | 2.427 |
| `mod_mapping_process` / 146678 | 0.008 | 0.017 | 0.036 | 1.796 |
| `mod_noise_process` / 146684 | 0.057 | 0.101 | 0.283 | 3.331 |
| `mod_resample_process_pop` / 146680 | 0.011 | 0.019 | 0.044 | 2.513 |
| `mod_resample_process_pop` / 146703 | 0.013 | 0.021 | 0.056 | 2.048 |
| `mod_resample_process_pop` / 146704 | 0.014 | 0.022 | 0.069 | 0.925 |
| `mod_resample_process_push` / 146680 | 0.064 | 0.139 | 0.426 | 4.501 |
| `mod_resample_process_push` / 146703 | 0.111 | 0.201 | 0.419 | 2.532 |
| `mod_resample_process_push` / 146704 | 0.109 | 0.199 | 0.423 | 1.767 |
| `mod_ssl_process` / 146688 | 0.865 | 1.636 | 2.346 | 7.722 |
| `mod_sss_process` / 146696 | 0.198 | 0.303 | 0.611 | 2.621 |
| `mod_sst_process` / 146693 | 0.052 | 0.087 | 0.169 | 1.591 |
| `mod_stft_process` / 146682 | 0.042 | 0.084 | 0.213 | 3.605 |
| `mod_volume_process` / 146708 | 0.013 | 0.022 | 0.063 | 0.530 |
| `mod_volume_process` / 146709 | 0.014 | 0.022 | 0.077 | 1.118 |

These observed call costs do not certify standalone neural-network inference
cost, worst-case costs for every possible audio event, or the complete cost of
asynchronous remote work triggered by a module. Resampler push/pop are
invocation boundaries and must not be assumed to have the same rate for every
configuration. Parallel and nested scope costs must not be summed as pipeline
latency.

### Source, Worker-Wait, File and JSON Sink Timings

The source and worker-wait rows have 14,999 completed scopes; one boundary
scope did not finish inside the window. Other rows have 15,000. All values are
milliseconds. Socket timings measure the send-call scope, not receiver arrival.

| Boundary / thread | Mean ms | p95 ms | p99 ms | Maximum ms |
|:--|--:|--:|--:|--:|
| `src_hops_process` / 146676 | 7.971 | 0.083 | 248.961 | 249.166 |
| `ssl_worker_wait` / 146688 | 7.083 | 0.114 | 223.650 | 230.118 |
| `snk_categories_process` / 146715 | 0.012 | 0.020 | 0.062 | 0.674 |
| `snk_hops_process` / 146710 | 0.043 | 0.124 | 0.225 | 2.488 |
| `snk_hops_process` / 146711 | 0.044 | 0.124 | 0.236 | 6.944 |
| `snk_pots_process` / 146689 | 0.104 | 0.256 | 0.500 | 3.482 |
| `snk_pots_process_interface_socket` / 146689 | 0.054 | 0.188 | 0.403 | 3.407 |
| `snk_tracks_process` / 146694 | 0.087 | 0.190 | 0.412 | 6.875 |
| `snk_tracks_process_interface_socket` / 146694 | 0.023 | 0.089 | 0.169 | 3.556 |
| `tracks_file_write` / 146694 | 0.033 | 0.069 | 0.149 | 6.829 |

`tracks_file_write` spans the current JSON sidecar write, any backlog flush,
and the existing `fflush`; it does not establish SD durability. The separate
raw audio recorder timings appear under measurement item 4.

### Pacing for Every Processing Instance

These are **start-to-start intervals**, not processing costs. Each row has
14,999 completed intervals; source and sink pacing are also preserved in the
linked machine-readable module table.

| Module / thread | Mean ms | p95 ms | p99 ms | Maximum ms |
|:--|--:|--:|--:|--:|
| `mod_chatak_process` / 146686 | 7.985 | 2.091 | 241.428 | 247.584 |
| `mod_classify_process` / 146714 | 7.986 | 2.727 | 228.033 | 238.385 |
| `mod_istft_process` / 146699 | 7.986 | 2.749 | 226.889 | 237.445 |
| `mod_istft_process` / 146700 | 7.986 | 2.644 | 227.166 | 237.588 |
| `mod_mapping_process` / 146678 | 7.984 | 0.789 | 248.648 | 251.449 |
| `mod_noise_process` / 146684 | 7.984 | 2.046 | 245.161 | 249.038 |
| `mod_resample_process_pop` / 146680 | 7.984 | 1.009 | 247.216 | 249.729 |
| `mod_resample_process_pop` / 146703 | 7.986 | 2.654 | 228.221 | 238.614 |
| `mod_resample_process_pop` / 146704 | 7.986 | 2.602 | 227.906 | 238.168 |
| `mod_resample_process_push` / 146680 | 7.984 | 0.941 | 247.149 | 249.562 |
| `mod_resample_process_push` / 146703 | 7.986 | 2.594 | 228.138 | 238.500 |
| `mod_resample_process_push` / 146704 | 7.986 | 2.586 | 227.802 | 238.011 |
| `mod_ssl_process` / 146688 | 7.986 | 2.283 | 224.379 | 230.797 |
| `mod_sss_process` / 146696 | 7.986 | 2.653 | 226.005 | 234.667 |
| `mod_sst_process` / 146693 | 7.986 | 2.659 | 225.328 | 232.750 |
| `mod_stft_process` / 146682 | 7.984 | 1.618 | 246.307 | 250.056 |
| `mod_volume_process` / 146708 | 7.986 | 2.587 | 228.878 | 241.187 |
| `mod_volume_process` / 146709 | 7.986 | 2.670 | 228.501 | 240.185 |

### Original SSL/SST Algorithm-Stage Averages

The original profilers each reported 15,000 frames in the measured window.
Values printed as `0.000` are rounded, not proof that work takes zero time.
Rounding can make the displayed substage sum differ slightly from its total.

| Module | Stage | Average ms/frame |
|:--|:--|--:|
| SSL | Pre-XCorr | 0.079 |
| SSL | GCC-PHAT XCorr | 0.224 |
| SSL | SRP Accumulation | 0.143 |
| SSL | Peak Extraction + Mapping | 0.002 |
| SSL | Freq at Peak | 0.306 |
| SSL | Format DOA Angles | 0.042 |
| SSL | Full SSL Frame | 0.795 |
| SST | Track Management | 0.002 |
| SST | Predict | 0.000 |
| SST | Coherence | 0.000 |
| SST | Mixture + Assignment | 0.008 |
| SST | Update | 0.000 |
| SST | Activity + Classify | 0.001 |
| SST | Transitions + Dynamic | 0.001 |
| SST | Output Copy | 0.001 |
| SST | Full SST Frame | 0.014 |

## 4. Test Cases: Run Status and Remaining Work

| Test case | Status | What the evidence establishes / what remains |
|:--|:--|:--|
| Live microphone + real JSON receivers + `-rl` SD recording | Completed in the primary 120 s run | Device-side timings, established TCP connections, SD writes and catch-up evidence |
| Duration doubling, 60 s versus 120 s | Completed in earlier runs | Overrun count scales with duration; recurring pattern survives boundary trimming |
| Local JSON collector smoke test | Completed earlier | Timestamp metadata, both stream paths and raw-write offsets verified; not a full visualizer workload |
| Receiver unavailable | Observed in earlier attempts | Connect stalls/backpressure produced incomplete measurements; not a valid real-time result |
| Pi-hosted hotspot + bidirectional visualizer commands | Not fully verified | Confirm intended WiFi topology and log actual application command activity |
| Repeated added-UI interactions under recording load | Not independently verified | Exercise the actual UI workflow, timestamp actions and compare frame/queue distributions |
| Ten-minute or longer thermal soak | Not completed in this report | Repeat full workload at operating ambient temperature; examine tails and current/historical throttle flags |
| Dedicated SD audio replay/read test | Not completed | Use an explicit file-input config on SD; label cache effects and distinguish reads from live `-rl` writes |
| Physical audio continuity / hardware xrun detection | Not established | Record actual ALSA read results/xruns or device sample counters; generated IDs are insufficient |
| Microphone-to-visualizer render latency | Not measured | Correlate source sample/acquisition timestamps with synchronized receiver arrival/render timestamps |
| Isolated networking CPU or per-process recorder CPU | Not measured | Add process/thread accounting or profiling without relabeling host totals as isolated cost |

No artificial read stress, cache eviction, added `fsync`, network topology
change or synthetic UI interaction was introduced into the primary run.
Such experiments need separate, clearly labeled run conditions.

## 5. Collection Method and Reproducibility

### Actual ODAS Child Command

```bash
~/chatak-odas_20261004_93a1b81/build/live_timing/bin/odaslive \
  -c ~/ChatakGUI/config/sonicwild.cfg -rl
```

`-rl` selects live recording. The tester overrides the recording directory for
this run to its SD-backed `audio/` directory without changing the shared GUI
config. `-s` would select sequential processing; `-sr` is therefore not the
same concurrent workload and was not used.

### Tester Command

```bash
cd ~/chatak-odas_20261004_93a1b81
python3 scripts/run_live_timing.py \
  --seconds 120 --warmup 5 --deadline-ms 8 \
  --interface wlan0 --disk mmcblk0
```

The old SSL/SST stage timers are enabled alongside the new timing probes.
Monotonic timestamps are shared across local C and Python processes. Queue
snapshots are taken immediately before/after SSL input dequeue; the report
uses the after-pop samples. Wait scopes cover blocking input/output-slot
acquisition before `mod_ssl_process` starts.

Trace files are line-buffered on tmpfs and copied to the run directory after
measurement to avoid adding their SD writes inside the measured window.
Recording timestamp sidecars still add I/O, and tracing, FIFO snapshots and
system sampling add CPU work. No quantified instrumentation-overhead baseline
was collected. System counters are sampled approximately once per second, not
at every frame or disk operation.

Build, syntax checks, tester self-tests, synthetic catch-up/growing-delay
checks and saved-trace/report validation passed. The primary run emitted no
forced-shutdown or missing-sink warning. Absence of a logged capture error is
not proof of zero undetected physical loss.

## 6. Evidence Files

All links below refer to the primary run; original evidence files are retained.

- [Generated runtime report](../live_timing_runs/20261008T121911.770342Z/report.md)
- [Machine-readable summary](../live_timing_runs/20261008T121911.770342Z/summary.json)
- [ALL module timings and object/thread identifiers](../live_timing_runs/20261008T121911.770342Z/module_timings.csv)
- [Every SSL interval and frame ID](../live_timing_runs/20261008T121911.770342Z/ssl_intervals.csv)
- [Complete SSL empirical distribution](../live_timing_runs/20261008T121911.770342Z/ssl_distribution.csv)
- [Frame spike/system-bin overlap](../live_timing_runs/20261008T121911.770342Z/ssl_spike_context.csv)
- [Measured SSL input FIFO snapshots](../live_timing_runs/20261008T121911.770342Z/ssl_queue_depth.csv)
- [Complete long-pause/burst cycles](../live_timing_runs/20261008T121911.770342Z/ssl_burst_cycles.csv)
- [Relative frame schedule residual](../live_timing_runs/20261008T121911.770342Z/ssl_relative_schedule.csv)
- [Timestamped CPU, disk, network and thermal samples](../live_timing_runs/20261008T121911.770342Z/system_samples.jsonl)
- [Original ODAS/recorder log and stage-timer output](../live_timing_runs/20261008T121911.770342Z/odas.log)
- [Earlier 60 s report](../live_timing_runs/20261008T115126.550285Z/report.md)
- [Earlier 120 s report](../live_timing_runs/20261008T115454.348505Z/report.md)

## Final Interpretation

**Confirmed:** the 8 ms period, expected average frame count, live JSON
transmission, SD recording, low sampled average CPU load, recurring input/worker
waits, and repeated catch-up in the monitored SSL FIFO.

**Not confirmed:** an eight-ms worst-case delivery guarantee, zero hardware
audio loss, absolute microphone-to-screen latency, and the complete hotspot,
bidirectional-control, additional-UI and long-duration thermal workload.

The current result supports **continued average live throughput with periodic
batching and jitter**, not smooth low-latency delivery. The most useful next
investigation is the negotiated capture period/buffering and source sample
age, followed by receiver-side latency and the unverified operating test cases.