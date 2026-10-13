# Latest-Version Live Timing

## 1. Does It Keep Up, End to End?

Use the special tester in this `93a1b81` checkout. The old Python stage and
concurrent testers, their prebuilt binaries, the original checkout, and older
snapshots are not changed. SSL/SST stage profiling remains enabled in the new
tester, alongside wall-clock tracing.

Build a separate instrumented executable:

```bash
cd /home/chatak/chatak-odas_20261004_93a1b81
cmake -S . -B build/live_timing -DCMAKE_BUILD_TYPE=Release
cmake --build build/live_timing --target odaslive -j2
python3 scripts/run_live_timing.py --self-test
python3 scripts/run_live_timing.py --seconds 120 --warmup 10 --interface wlan0
```

The tester runs `odaslive -c CONFIG -rl`: `-rl` selects live raw recording.
`-r` is its default-live equivalent. Do **not** use `-sr` for the concurrent
test: `-s` selects sequential processing and changes the workload.

Before the measured run, start the real visualizer and its normal audio stream,
activate the intended WiFi hotspot, and exercise its usual bidirectional
controls throughout the run. The default GUI config sends SSL JSON to
`192.168.1.9:9003` and SST JSON to `192.168.1.9:9002`; `--config` selects another
existing config without changing the shared GUI configuration. The tester does
not start a substitute visualizer or change the network topology.

Default recording is a fresh `live_timing_runs/TIMESTAMP/audio` directory in
this checkout. Here that is on `/dev/mmcblk0p2`, the SD card. Use `--record-dir`
to select a different existing SD mount. The GUI's normal `/mnt/CHATAK_VM`
recording directory is on USB `/dev/sda1`, not SD. The new tester overrides
recording paths for its child processes only.

`--deadline-ms` defaults to 8 ms for the current 128-sample, 16000 Hz SSL hop.
For another configuration, set it to `1000 * general.size.hopSize /
general.samplerate.mu`. Use long measured windows to exercise writeback,
network bursts and sustained temperature effects.

## 2. What to Measure

1. **Inter-frame wall-clock interval:** `clock_gettime(CLOCK_MONOTONIC)` at SSL
   entry; consecutive start deltas include upstream blocking and scheduling.
2. **Full distribution:** mean, nearest-rank p95/p99, max, deadline exceedance
   count/percentage, every raw delta, and an empirical CDF. A terminal gap
   without a subsequent frame is reported separately, not silently discarded.
3. **Per-core CPU utilisation:** `/proc/stat` deltas for ALL cores, with busy
   and iowait separated. This works without installed `mpstat` or `htop`.
4. **Storage wait and load:** actual recording mount/device, `/proc/diskstats`
   read/write bandwidth, average completed-operation wait and device busy time.
   These are timestamped alongside the frame events, like `iostat -x 1`.
5. **Network-side load:** hotspot-interface RX/TX bytes, packets and drops;
   ODAS TCP connection states, queues and retransmission details from `ss`.
   Interface totals include other traffic; RX alone does not prove control use.
6. **Thermal/clock state:** repeated `vcgencmd get_throttled`, `measure_temp`
   and `measure_clock arm`. Current bits 0-3 and historical bits 16-19 distinguish
   undervoltage, frequency capping, throttling and soft temperature limits.
7. **ALL modules:** timestamped entry/exit for CHATAK, classify, ISTFT, mapping,
   noise, resampler push/pop, SSL, SSS, SST, STFT and volume. Threads/object
   instances stay separate; source and sink boundaries are traced too.
8. **Audio/file writes and JSON transmission:** raw-hop sidecars contain
   monotonic read-start/write-start/write-end times, hop indices, byte offsets
   and sizes. SST sidecar writes and both socket-send scopes are timestamped.
   SSL/SST JSON adds `monotonic_ns` and `frame_id`, retaining existing `timeStamp`.
9. **Real bidirectional activity:** retain the real receiver and control traffic
   while measuring. Local collectors only validate instrumentation, not this
   workload. Receiver arrival/render latency needs receiver-side evidence.

## Outputs And Interpretation

Each run creates `report.md`, `summary.json`, `ssl_intervals.csv`,
`ssl_distribution.csv`, `module_timings.csv`, `ssl_spike_context.csv`,
`ssl_queue_depth.csv`, `ssl_burst_cycles.csv`, `ssl_relative_schedule.csv`,
`system_samples.jsonl`, `odas.log`, `traces/`, and
timestamped recording files. System samples and C/raw-writer traces use the
same local monotonic clock. Match frame spikes to the corresponding sample
intervals; raw audio hop indices belong to a separate capture process and must
not be equated with SSL frame IDs.

Trace records are line-buffered on tmpfs (`/dev/shm` by default) and copied
after shutdown to avoid extra SD trace writes during measurement. Tracing and
sampling themselves add CPU load. File-write times cover buffered application
writes, not fsync durability; delayed kernel writeback appears in disk samples.
JSON `monotonic_ns` is formatting time; socket scope start/end is send-call time,
not receiver delivery. Downstream stalls affect SSL pacing only after queue
backpressure reaches SSL. Resampler invocation rates can differ from audio-hop
rates. Missing measurements are not treated as zero-cost work.

The runner fails early-exit, forced-shutdown and long terminal-stall runs while
retaining their evidence. It warns about missing recording/sinks, non-SD
storage and unconfirmed hotspot mode. `full_live_workload_verified` stays false
because visualizer control activity and receiver timing need external proof.

## Validation On This Device

Build and tester self-test passed. A five-second loopback collector smoke test
verified every module hook, 640 valid SSL and 641 SST timestamped packets, SD
raw byte counts/offsets, SST JSON file writes, and clean shutdown. Its SSL mean
was 7.925 ms, p95 2.486 ms, p99 223.754 ms and max 235.111 ms. This is not a full
hotspot/visualizer performance result; capture batching makes tails important.

The real-endpoint attempt showed both GUI ports stuck in `SYN-SENT`, stalled
pipeline progression and forced shutdown. The current WiFi interface reports
managed mode, not AP mode. Start/fix the real receiver and intended hotspot
before drawing conclusions about the requested concurrent workload.

The smoke test also observed one pre-existing SSL startup packet containing
lowercase `nan`, which is invalid JSON. That unrelated algorithm-output issue
is unchanged; valid later packets were checked independently.

## Answers To The Runtime Guide

- The active GUI config confirms `raw.fS = 16000`,
   `general.samplerate.mu = 16000`, and both hop sizes are 128. The SSL
   frame period is therefore `128 / 16000 = 0.008 s = 8 ms`.
- Correct "CPU-busy time" to "elapsed time inside timed algorithm regions":
   the existing SSL/SST timers use `CLOCK_MONOTONIC`, not a thread CPU clock.
   Neither their averages nor SSL entry pacing proves receiver-render latency.
- The quoted 1.079 ms reference is not verified by this code or run. Use each
   report's actual SSL/SST stage totals; preserve their distinction from traced
   scope time and inter-frame intervals.
- Every processing module has an entry/exit hook: mapping, resampling push/pop,
   STFT, noise, CHATAK, SSL, SST, SSS, ISTFT, volume and classify. Reports now show
   mean/p95/p99/max **processing time** and **pacing** separately for every
   thread/object instance. Source, file-write and JSON sink scopes are included.
- `ssl_spike_context.csv` associates deadline-exceeding intervals with
   overlapping system sampling bins. Nonzero iowait and SD await alongside a
   spike support investigating storage; this coarse correlation is not proof
   of causation. Interface bandwidth likewise does not isolate network CPU use.
- A nonzero `get_throttled` mask does not always mean current thermal throttling:
   it can also indicate undervoltage, frequency capping or historical events.
   Lower clock speed by itself can be normal frequency scaling.

## Operating Test Cases

| Case | Conditions | Measurements / limits |
|:--|:--|:--|
| Live recording | Microphone capture, real JSON receiver and `-rl` on SD | All modules, CPU/iowait, SD writes, bandwidth, frame tails |
| Bidirectional/UI load | Same run while the user repeatedly exercises real visualizer controls and added UI interactions | Compare tails and system samples; record which controls were exercised |
| Sustained load | Same full workload for at least ten minutes | Temperature, current/historical throttling, clock, writeback and tail distributions |
| Receiver unavailable | Receiver intentionally stopped in a separate test | Expect connect stalls/backpressure; report invalid workload rather than a good average |
| SD replay/read | A separately prepared ODAS file-input config reading an SD recording | Read-workload test, not created by live `-rl`; cache can hide physical reads |

The automated runner executes the first case. It cannot drive unprovided
visualizer/UI APIs or establish that their controls were exercised. Keep the
actual hotspot and receiver active externally. Repeat the same command under
those different conditions, retain each run directory, and compare results.
Do not change network topology, clear caches, introduce fsync, or add synthetic
load without labeling the changed workload.

## Establishing What The Overruns Represent

The report includes a dedicated explanation of timing overruns versus audio
loss. It counts the actual intervals exceeding the nominal period; neither
p99 nor a stable three-percent fraction means that audio frames were dropped.
Warm-up and shutdown are excluded, and first/middle/last measurement windows
are compared separately.

Additional opt-in C probes measure the SSL worker's wait for its spectra input,
CHATAK input and output slot, plus the spectra FIFO depth before/after popping.
`ssl_queue_depth.csv` retains after-pop snapshots. `ssl_burst_cycles.csv`
measures frame progression, worker waiting, burst processing span, queue
draining and relative schedule recovery between successive gap events.

`ssl_relative_schedule.csv` records elapsed time minus generated SSL frame-ID
progression times the nominal frame period, referenced to the first observed
frame. A repeating residual with draining queues supports catch-up; increasing
residual medians support investigating accumulating delay. This is not an
absolute audio-sample age measurement and excludes unknown hardware buffering.
Queue snapshots can miss brief peaks and observe only one input FIFO. Generated
frame-ID continuity does not detect physical sample loss inside ALSA/device
buffers. Receiver-side arrival/render timing remains unmeasured.

The processing behavior is unchanged: no deadline-based discard was added.
The current SSL worker consumes FIFO messages in order and blocks when input or
output slots are unavailable. Async queues have 100 message slots in this
checkout; full queues propagate backpressure instead of automatically dropping
the oldest message. All original stage timers remain enabled.