# ODAS Latest-Version Live Timing

## 1. Does It Keep Up, End to End?

Revision: `93a1b81`. Command: `~/chatak-odas_20261004_93a1b81/build/live_timing/bin/odaslive -c ~/ChatakGUI/config/sonicwild.cfg -rl`.
Measured window: 120.000 s; deadline: 8.000 ms.
SSL start-to-start intervals include computation, scheduling and upstream blocking;
they are not receiver-render latency, and downstream stalls matter only when they backpressure SSL.

SSL terminal gap: **18.579 ms** (right-censored: no following frame; not included in the delta distribution).
SSL intervals: **15015**; mean **7.991 ms**, p95 **2.610 ms**, p99 **222.451 ms**, max **227.587 ms**; over deadline **3.20%**.

## What The Overruns Mean And How ODAS Handles Them

An overrun here is a start-to-start SSL interval longer than the configured frame period. It is NOT an ALSA xrun, a dropped frame, or proof that SSL computation took that long.
There is no fixed '299 frames' category: the actual count depends on duration and this run's observed intervals. p99 is a percentile, not a dropped-frame count.
ODAS waits on blocking audio input, consumes messages in FIFO order, and processes available frames in bursts. Empty FIFOs make workers wait; full bounded queues backpressure producers. This checkout configures 100 message slots per asynchronous queue.
The SSL worker has no 8 ms deadline rejection or automatic late-frame discard. The measurement labels gaps; it does not change how ODAS processes them.

Observed gap events: **480**. Median spacing: **250.033 ms**; frame-ID spacing counts: `{'30': 1, '31': 357, '32': 121}`.
Event classification: **480 long pauses** over the descriptive 80.0 ms threshold and **0 other deadline exceedances**. The actual deadline is still 8.000 ms. Median spacing between long pauses: **250.033 ms**.
SSL frame-ID continuity: **0 skipped IDs**, **0 duplicate/reversed transitions**. Continuous generated IDs do not prove physical audio samples were never lost.
Measured input FIFO after-pop peak: **30 queued messages** across **15016 samples**. It reached zero in **479 / 479 complete pause/burst cycles**.
Worker wait associated with gap frames: mean **218.911 ms**, max **226.889 ms**. This wait includes spectra input, CHATAK input and output-slot availability, not just ALSA reading.
Mean post-gap burst span: **30.153 ms**. Mean relative schedule recovery per complete cycle: **211.851 ms**.
Relative schedule residual range: **-149.127 to 88.168 ms**. Last-minus-first 10.0-second median change: **2.144 ms**.
The residual is elapsed time minus frame-ID progression times the nominal period, referenced to the first measured frame. It tests accumulating schedule drift, NOT absolute microphone-to-screen lag. It does not identify the cause of the periodic pauses.

**Measured conclusion:** Observed repeated catch-up: the monitored SSL input FIFO drains and relative schedule medians show less than one frame-period change in this window. This does not establish absolute end-to-end latency or hardware sample-loss status.

| Window (warm-up excluded) | Intervals | Overruns | Overrun % |
| :------------------------ | --------: | -------: | --------: |
| first_5s                  |       640 |       20 |     3.125 |
| middle_without_5s_edges   |    13,749 |      439 |     3.193 |
| last_5s                   |       624 |       19 |     3.045 |

Use continuity, cycle recovery, FIFO draining and schedule drift together to judge catch-up. Throughput alone does not prove bounded latency; this probe observes only the SSL input FIFO, not every pipeline queue.
Evidence: ssl_queue_depth.csv, ssl_burst_cycles.csv and ssl_relative_schedule.csv. Queue-depth snapshots may miss peaks between samples.

### Why the ALSA sizes are relevant

ODAS requests one 128-frame hop from ALSA at a time; that does not guarantee a new hop is available every 8 ms. While a blocking read/worker waits, samples can accumulate upstream. When data becomes available and the worker runs, successive hop reads can complete quickly and drain the backlog.

| Quantity                       | This run                                  |
| :----------------------------- | :---------------------------------------- |
| ODAS read request              | 128 frames (8.000 ms)                     |
| One ALSA period                | 2000 frames (125.000 ms; 15.625 hops)     |
| Two ALSA periods               | 4000 frames (250.000 ms; 31.250 hops)     |
| ALSA buffer capacity           | 6000 frames (375.000 ms; 46.875 hops)     |
| Observed pause cycle           | 250.033 ms; most frame-ID gaps were 31-32 |
| ODAS input FIFO after-pop peak | 30 messages; drained in 479/479 cycles    |

Two negotiated ALSA periods equal about 31.25 ODAS hops, close to the measured ~250 ms pause spacing and 31-32-frame gaps. This is a plausible link to capture buffering, not proof that ALSA alone causes the pattern. The ALSA buffer value is maximum capacity, not the number of frames delivered as one batch; scheduling and other queues can contribute.

## SSL And SST Timing

All values are milliseconds. Pacing is start-to-start; processing time is entry-to-exit wall time.

### Frame pacing

| Module | Intervals | Mean ms | p95 ms |  p99 ms |  Max ms |
| :----- | --------: | ------: | -----: | ------: | ------: |
| SSL    |     15015 |   7.991 |  2.610 | 222.451 | 227.587 |
| SST    |     15016 |   7.990 |  3.749 | 222.404 | 231.251 |

### Processing scope

| Module | Scopes | Mean ms | p95 ms | p99 ms | Max ms |
| :----- | -----: | ------: | -----: | -----: | -----: |
| SSL    |  15016 |   0.896 |  1.638 |  2.565 |  6.319 |
| SST    |  15017 |   0.364 |  0.183 |  0.860 | 58.837 |

### Internal Stage Timers

The existing stage timers use CLOCK_MONOTONIC: elapsed time inside the algorithm, not strictly CPU-busy time. They exclude time spent waiting outside those regions.

| Module | Stage                     | Mean ms/frame |
| :----- | :------------------------ | ------------: |
| SSL    | Pre-XCorr                 |         0.083 |
| SSL    | GCC-PHAT XCorr            |         0.226 |
| SSL    | SRP Accumulation          |         0.141 |
| SSL    | Peak Extraction + Mapping |         0.002 |
| SSL    | Freq at Peak              |         0.312 |
| SSL    | Format DOA Angles         |         0.045 |
| SSL    | Full SSL Frame            |         0.808 |
| SST    | Track Management          |         0.002 |
| SST    | Predict                   |         0.005 |
| SST    | Coherence                 |         0.007 |
| SST    | Mixture + Assignment      |         0.091 |
| SST    | Update                    |         0.005 |
| SST    | Activity + Classify       |         0.211 |
| SST    | Transitions + Dynamic     |         0.002 |
| SST    | Output Copy               |         0.004 |
| SST    | Full SST Frame            |         0.327 |

## SD Recorder Write Timing

The measured window wrote **23,040,000 bytes** to the live audio recording.
| Recorder metric   | Buffered SD write() | Mic read-start interval* |
| :---------------- | :------------------ | :----------------------- |
| Calls / intervals | 15,000              | 14,999                   |
| Mean ms           | 0.038               | 8.001                    |
| p95 ms            | 0.088               | 123.778                  |
| p99 ms            | 0.161               | 124.443                  |
| Maximum ms        | 118.021             | 249.091                  |
| Over 8 ms         | 3 (0.020%)          | n/a                      |

*The read-start interval is microphone pacing, not SD read time.
**3 of 15,000** buffered write calls exceeded the 8 ms frame period. This shows occasional slow application writes (maximum 118.021 ms), not that audio was dropped or became late: writes are buffered, and the recorder runs separately from SSL. Live -rl recording exercises SD writes, not an artificial SD-read workload. Device I/O wait is below.
Buffered write-call duration does not measure fsync durability or when data reaches flash.

## 2. What to Measure

1. Inter-frame wall-clock interval: CLOCK_MONOTONIC at SSL entry; ssl_intervals.csv retains every delta and frame ID.
2. Full distribution: mean, nearest-rank p95/p99, max and deadline exceedances; ssl_distribution.csv is the full empirical CDF.
3. Per-core CPU utilisation: /proc/stat busy and iowait deltas for ALL cores in system_samples.jsonl.
4. Storage: /proc/diskstats read/write bandwidth, await (completed read/write operations) and device busy time; timestamps share the frame clock.
5. Network: hotspot-interface RX/TX bytes, packets and drops, plus ODAS TCP queues, retransmission counters and connection state from ss -tinp.
6. Thermal/clock: repeated vcgencmd get_throttled, measure_temp and measure_clock arm. Current bits 0-3 and historical bits 16-19 distinguish undervoltage, frequency capping, throttling and soft temperature limits.
7. ALL processing modules: entry/exit events by thread AND object instance, including separate resampler push/pop and source/sink boundaries. Uncalled modules are listed as inactive, not zero-cost.
8. File and JSON paths: audio *.write_timing.csv gives read/write monotonic times, hop index, byte offset and size; SST sidecar writes and JSON sends are traced. JSON gains monotonic_ns and frame_id without replacing timeStamp.
9. Live bidirectional traffic: keep the real visualizer and its control updates active during the run; interface RX alone does not prove visualizer commands were exercised.

## Run Validity

Recording mount: `{'source': '/dev/mmcblk0p2', 'target': '/', 'fstype': 'ext4'}`; monitored disk: `mmcblk0`.
WiFi state: `Interface wlan0
	ifindex 3
	wdev 0x1
	addr <redacted-mac>
	ssid <redacted-ssid>
	type managed
	wiphy 0
	channel 1 (2412 MHz), width: 20 MHz, center1: 2412 MHz
	txpower 31.00 dBm`.
Managed/client WiFi mode is valid for this measurement; a Pi-hosted access point is not required.
ALSA PCM: `multi_capture`, 16000 Hz; hop 128 frames, negotiated period 2000 frames (125.000 ms), buffer 6000 frames (375.000 ms).
The negotiated period/buffer are reported as settings only; their timing correlation does not prove they cause the pauses.
Raw audio bytes in window: 23040000; network traffic is interface-wide, not visualizer-only.
Buffered writes measure application write calls, not fsync durability; kernel writeback may occur later.
Tracing and sampling add load. Traces are line-buffered on RAM-backed storage then copied after stopping.
A quiet receiver or inactive controls cannot certify the requested full live workload.

- Live visualizer control activity and receiver render/arrival timing require external verification.

## JSON Network Sends

Sampled TCP counters span 119.295 s within the 120 s measurement window; counters are sampled about once per second.

| ODAS stream | Peer             | TCP sent bytes | TCP ACKed bytes |
| :---------- | :--------------- | -------------: | --------------: |
| SST tracks  | <private-ip>:9002 |      3,432,748 |       3,432,780 |
| SSL POTs    | <private-ip>:9003 |      5,176,530 |       5,177,940 |

| Socket send-call scope | Calls | Mean ms | p95 ms | p99 ms | Max ms |
| :--------------------- | ----: | ------: | -----: | -----: | -----: |
| SST tracks             | 15021 |   0.038 |  0.127 |  0.398 |  5.400 |
| SSL POTs               | 15017 |   0.056 |  0.195 |  0.522 |  2.132 |

Combined: **8,609,278 TCP bytes sent**, **8,610,720 bytes acknowledged by the peer TCP stack**. Interface-wide TX, including non-ODAS traffic, is approximately **9,736,552 bytes** over the measurement window.
Both local socket send-call maxima were below the 8 ms frame period. This means the calls returned within that budget; it does not prove the receiver application processed or displayed the data by the deadline. SSL still had 497 start-to-start intervals over 8 ms.

## System Measurements

| Core | Mean busy % | Max busy % | Mean iowait % | Max iowait % |
| :--- | ----------: | ---------: | ------------: | -----------: |
| cpu  |       17.78 |      29.05 |          1.95 |        21.52 |
| cpu0 |       18.88 |      43.40 |          2.21 |        50.96 |
| cpu1 |       17.37 |      30.19 |          1.65 |        13.86 |
| cpu2 |       17.41 |      30.39 |          1.98 |        18.00 |
| cpu3 |       17.43 |      32.08 |          1.92 |        40.78 |

### Disk rates

| Metric       |        Mean |      Maximum |
| :----------- | ----------: | -----------: |
| await ms     |       5.105 |      120.979 |
| in flight    |       0.000 |        0.000 |
| read Bps     |    1094.068 |   119060.096 |
| util percent |       9.536 |       74.371 |
| write Bps    | 1019126.861 | 15844211.097 |

### Interface rates

| Metric                |      Mean |    Maximum |
| :-------------------- | --------: | ---------: |
| rx bytes per second   |  5241.279 |  13509.844 |
| rx dropped per second |     0.000 |      0.000 |
| rx packets per second |    60.839 |    110.961 |
| tx bytes per second   | 81615.974 | 111900.600 |
| tx dropped per second |     0.000 |      0.000 |
| tx packets per second |    98.426 |    170.710 |

### Thermal and clock

| Metric                | Observed      |
| :-------------------- | :------------ |
| Samples               | 116           |
| Temperature range (C) | 57.9 to 62.3  |
| ARM clock range (GHz) | 0.90 to 1.80  |
| Throttle status       | throttled=0x0 |

## All Module Processing Times

Entry-to-exit wall time includes preemption or I/O inside the scope. Full thread/instance identifiers are in module_timings.csv.

| Module / thread                    | Scopes | Mean ms | p95 ms |  p99 ms |  Max ms |
| :--------------------------------- | -----: | ------: | -----: | ------: | ------: |
| CHATAK / t257850 / c370            |  15000 |   0.220 |  0.925 |   2.251 |   9.943 |
| Classify / t257877 / 44e0          |  15021 |   0.012 |  0.021 |   0.055 |   0.928 |
| ISTFT / t257863 / cf80             |  15021 |   0.069 |  0.117 |   0.311 |   3.014 |
| ISTFT / t257864 / f5b0             |  15021 |   0.071 |  0.122 |   0.341 |   2.158 |
| Mapping / t257842 / 1600           |  15000 |   0.009 |  0.017 |   0.037 |   2.152 |
| Noise / t257848 / 2a60             |  15000 |   0.056 |  0.096 |   0.327 |   5.240 |
| Resample pop / t257844 / f210      |  15000 |   0.010 |  0.017 |   0.039 |   3.010 |
| Resample pop / t257867 / 53b0      |  15021 |   0.014 |  0.019 |   0.081 |   2.770 |
| Resample pop / t257868 / bd70      |  15021 |   0.015 |  0.019 |   0.074 |   5.683 |
| Resample push / t257844 / f210     |  15000 |   0.067 |  0.137 |   0.610 |   5.739 |
| Resample push / t257867 / 53b0     |  15021 |   0.111 |  0.228 |   0.547 |   3.606 |
| Resample push / t257868 / bd70     |  15021 |   0.109 |  0.215 |   0.558 |   3.528 |
| SSL / t257852 / 23e0               |  15016 |   0.896 |  1.638 |   2.565 |   6.319 |
| SSS / t257860 / e640               |  15020 |   0.585 |  1.169 |   1.797 |   5.350 |
| SST / t257857 / 40e0               |  15017 |   0.364 |  0.183 |   0.860 |  58.837 |
| STFT / t257846 / 48d0              |  15000 |   0.042 |  0.083 |   0.245 |   3.324 |
| Volume / t257871 / 5a20            |  15021 |   0.014 |  0.023 |   0.070 |   2.397 |
| Volume / t257872 / 5b00            |  15021 |   0.014 |  0.022 |   0.060 |   1.403 |
| Category sink / t257878 / 4f20     |  15021 |   0.011 |  0.020 |   0.044 |   0.858 |
| Audio sink / t257873 / 31f0        |  15021 |   0.077 |  0.155 |   0.492 | 110.091 |
| Audio sink / t257874 / 3b60        |  15021 |   0.078 |  0.158 |   0.561 | 111.348 |
| Pots sink / t257853 / 47d0         |  15017 |   0.107 |  0.284 |   0.655 |   2.195 |
| Pots JSON send / t257853 / 47d0    |  15017 |   0.056 |  0.195 |   0.522 |   2.132 |
| Tracks sink / t257858 / 07a0       |  15021 |   0.201 |  0.512 |   1.285 | 112.608 |
| Tracks JSON send / t257858 / 07a0  |  15021 |   0.038 |  0.127 |   0.398 |   5.400 |
| Audio source / t257840 / 50d0      |  14999 |   7.971 |  0.097 | 248.945 | 251.566 |
| SSL worker wait / t257852 / 2230   |  15016 |   7.034 |  0.322 | 221.640 | 226.889 |
| Tracks file write / t257858 / 07a0 |  15021 |   0.118 |  0.305 |   1.011 | 112.435 |

## All Module Pacing

Start-to-start interval; this is pacing, not processing duration.

| Module / thread                    | Starts | Mean ms | p95 ms |  p99 ms |  Max ms |
| :--------------------------------- | -----: | ------: | -----: | ------: | ------: |
| CHATAK / t257850 / c370            |  15000 |   7.985 |  2.429 | 240.653 | 247.643 |
| Classify / t257877 / 44e0          |  15021 |   7.988 |  4.216 | 223.934 | 253.797 |
| ISTFT / t257863 / cf80             |  15021 |   7.988 |  3.985 | 223.079 | 253.312 |
| ISTFT / t257864 / f5b0             |  15021 |   7.988 |  3.979 | 223.019 | 253.069 |
| Mapping / t257842 / 1600           |  15000 |   7.984 |  0.786 | 248.576 | 251.229 |
| Noise / t257848 / 2a60             |  15000 |   7.984 |  2.154 | 245.091 | 251.020 |
| Resample pop / t257844 / f210      |  15000 |   7.984 |  1.193 | 246.951 | 250.465 |
| Resample pop / t257867 / 53b0      |  15021 |   7.988 |  4.164 | 224.289 | 254.657 |
| Resample pop / t257868 / bd70      |  15021 |   7.988 |  4.132 | 223.689 | 253.703 |
| Resample push / t257844 / f210     |  15000 |   7.984 |  1.137 | 246.867 | 250.358 |
| Resample push / t257867 / 53b0     |  15021 |   7.988 |  4.150 | 224.115 | 254.633 |
| Resample push / t257868 / bd70     |  15021 |   7.988 |  4.095 | 223.626 | 253.659 |
| SSL / t257852 / 23e0               |  15016 |   7.991 |  2.610 | 222.451 | 227.587 |
| SSS / t257860 / e640               |  15020 |   7.989 |  3.404 | 221.788 | 252.975 |
| SST / t257857 / 40e0               |  15017 |   7.990 |  3.749 | 222.404 | 231.251 |
| STFT / t257846 / 48d0              |  15000 |   7.984 |  1.793 | 246.126 | 251.816 |
| Volume / t257871 / 5a20            |  15021 |   7.988 |  4.370 | 225.129 | 254.897 |
| Volume / t257872 / 5b00            |  15021 |   7.988 |  4.388 | 224.389 | 254.542 |
| Category sink / t257878 / 4f20     |  15021 |   7.988 |  4.209 | 224.476 | 253.893 |
| Audio sink / t257873 / 31f0        |  15021 |   7.988 |  4.486 | 225.556 | 254.917 |
| Audio sink / t257874 / 3b60        |  15021 |   7.988 |  4.607 | 224.655 | 254.586 |
| Pots sink / t257853 / 47d0         |  15017 |   7.990 |  3.364 | 223.321 | 231.277 |
| Pots JSON send / t257853 / 47d0    |  15017 |   7.990 |  3.377 | 223.332 | 231.310 |
| Tracks sink / t257858 / 07a0       |  15021 |   7.988 |  3.914 | 223.268 | 254.727 |
| Tracks JSON send / t257858 / 07a0  |  15021 |   7.988 |  3.946 | 223.284 | 254.737 |
| Audio source / t257840 / 50d0      |  15000 |   7.984 |  0.152 | 249.005 | 252.199 |
| SSL worker wait / t257852 / 2230   |  15017 |   7.990 |  2.647 | 222.572 | 228.650 |
| Tracks file write / t257858 / 07a0 |  15021 |   7.988 |  3.937 | 223.396 | 254.729 |

## Frame Spike Context

Deadline-exceeding SSL intervals: 480; matched to system bins: 479; overlapping nonzero host iowait: 479.
ssl_spike_context.csv joins each spike to overlapping CPU/iowait, SD await/busy, interface bandwidth and throttling samples.
These are coarse system-bin correlations, not proof that storage or networking caused a particular frame delay. Network rates include other interface traffic; networking-only CPU use is not isolated.

Inactive module boundaries: none.

Scope durations are wall time, not CPU-only time. Different module rates and attempted resampler calls are not interchangeable frame rates.
The existing SSL/SST stage timers and older Python testers are unchanged.
Raw evidence: traces/, module_timings.csv, ssl_intervals.csv, ssl_distribution.csv, ssl_spike_context.csv, system_samples.jsonl, odas.log and summary.json.
