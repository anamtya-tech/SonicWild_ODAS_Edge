# Event-Based YAMNet Classification Pipeline

## Overview

This document describes the design, implementation, and testing of the
event-based audio classification pipeline added to the ODAS Sound Source
Tracking (SST) module.  The work integrates Google's YAMNet neural network
directly into the C ODAS pipeline, replaces a fragile single-frame classification
approach with a robust 6-hop rolling-majority vote, and adds a `.bin` sidecar
mechanism so the Python simulator can reconstruct full-length audio from every
confirmed event.

---

## Background and Motivation

### What ODAS produces

ODAS tracks up to N simultaneous sound sources in 3-D space.  For each tracked
source (a "track") it maintains a circular spectral buffer: 96 frames of
half-spectra, each frame being 257 floats (STFT magnitude bins for a 512-point
FFT at 16 kHz).

| Quantity | Value | Derivation |
|---|---|---|
| Sample rate | 16 000 Hz | `general.samplerate.mu` |
| Hop size | 128 samples | `raw.hopSize` |
| Frame duration | 8 ms | 128 / 16 000 |
| Frames per track buffer | 96 | hard-coded `nFramesPerTrack` |
| Buffer duration | 768 ms | 96 × 8 ms |
| Half-spectrum bins | 257 | (512 / 2) + 1 |

### Problem with the old approach

The original classifier ran YAMNet on a single frame of 257 floats — 8 ms of
spectral data — and emitted a JSON line for every ODAS frame (~125 Hz).  This
caused three problems:

1. **Temporal context starvation** — YAMNet expects a 96-frame (≈1 s) patch.
   Running it on one frame produces noise-level confidence scores.
2. **JSON flooding** — every frame emitted a full 257-float `bins` array
   (~100 KB/s of logs).
3. **No consensus** — a single mis-classified frame could pollute the log.

---

## Design Decisions

### D1 — Top-K YAMNet output (K = 5)

Instead of keeping only the argmax class, the classifier now returns the top-5
classes and their confidences per hop.  This preserves ambiguous detections for
downstream analysis (e.g., "Bird → Owl → Animal" vs. "Dog → Bark → Animal").

### D2 — Sticky pot association with semantic boost

When a potential source (a "pot") is being assigned to an existing track, the
assignment score is multiplied by **1.2×** if the pot's current top-1 YAMNet
class matches the track's last confirmed class.  This prevents a tracked animal
call from being hijacked by a simultaneous background noise source.

### D3 — 6-hop rolling history buffer

A circular buffer of `ROLLING_HOPS = 6` `topk_hop_t` entries is maintained per
track.  One hop is written every **48 frames** (true 50% overlap: hop fires when
`(count − 96) % 48 == 0`).  Each hop stores:

```c
typedef struct {
    int   class_ids[TOPK];       // top-5 class IDs
    float confidences[TOPK];     // corresponding confidences
    unsigned long long timestamp; // ODAS frame counter
} topk_hop_t;
```

Timeline at steady state (frames, hop size = 48):

```
hop 1: frames   1–96   →  classify patch [  0..95]
hop 2: frames  49–144  →  classify patch [ 48..143]
hop 3: frames  97–192  →  classify patch [ 96..191]
...
hop N fires every 48 frames (≈ 384 ms)
6 hops span ≈ 2.3 s of audio context
```

### D4 — Event gate: `min_event_votes`

A JSON event is only written when **both** conditions hold:

1. The circular buffer is fully warm (`topk_count == ROLLING_HOPS == 6`).
2. The mode (most frequent top-1 class) across the 6 hops has at least
   `min_event_votes` agreeing hops (default **4 of 6**).

The `compute_event()` helper computes: mode class ID, vote count, and mean
confidence of the agreeing hops only.

```
votes required  | behaviour
----------------|-------------------------------------------------
1               | fires on every full buffer (useful for tuning)
4 (default)     | majority — filters transient noise well
6               | unanimous — only the most stable events fire
```

### D5 — `.bin` spectra sidecar (`sim_mode = 1`)

On Pi the 96×257 float32 patch (≈96 KB) is never written to disk — power and
I/O budgets are tight.  In simulator mode (`sim_mode = 1`) a binary sidecar is
written alongside the JSON log:

```
{classifier_log_dir}/patch_{trackID}_{timestamp}.bin
```

The file is a flat array of `96 × 257` `float32` values in C-native row-major
order.  Python loads it with:

```python
np.fromfile(path, dtype=np.float32).reshape(96, 257)
```

Six consecutive sidecars stitched by `AudioReconstructor.reconstruct_from_spectra_files()`
yield ~3 s of reconstructable audio per track.

### D6 — 48 ms JSON output gate *(added 2026-03-01)*

Originally `dump_track_buffers_to_json()` was called on every 8 ms ODAS hop
(125 Hz).  Because YAMNet only re-evaluates every `ROLLING_HOPS = 6` hops
(48 ms), 5 of every 6 emitted lines contained **identical, stale**
classification results.  This caused two problems:

1. **False timestamp compression**: the Python parser was designed to convert
   line numbers to seconds as `(line_num - 1) × 0.008`.  With 6× more lines
   than YAMNet evaluations, this formula compressed a 35-second session into
   ~5.5 seconds, making most ground-truth time windows unreachable.

2. **Redundant JSON volume**: a 35-second run produced ~4 150 lines instead of
   ~690.

**Fix**: emit only when `timeStamp % ROLLING_HOPS == 0`:

```c
if (obj->enable_classifier_output &&
    (obj->in1->timeStamp % ROLLING_HOPS == 0)) {
    dump_track_buffers_to_json(...);
    dump_track_fingerprint_only(...);
}
```

Each JSON line is now 1:1 with one `.bin` sidecar and one fresh YAMNet
evaluation.  The Python parser uses `timeStamp × 0.008` (hop counter ×
8 ms/hop) for correct absolute timestamps regardless of the JSON line rate.

---

## Files Changed

### C firmware — `z_odas_newbeamform/`

| File | Change summary |
|---|---|
| `include/yamnet/yamnet_classifier.h` | Added `TOP_K = 5` constant; added `ClassifyPatchTopK()` signature |
| `include/yamnet/yamnet_c_api.h` | Added `yamnet_classify_patch_topk()` C API declaration |
| `src/yamnet/yamnet_classifier.cpp` | Implemented `ClassifyPatchTopK()` using `std::partial_sort` on score–index pairs |
| `src/yamnet/yamnet_wrapper.cpp` | Added `yamnet_classify_patch_topk()` C wrapper |
| `include/odas/module/mod_sst.h` | Added `sst_event_t` struct (fields: `class_id`, `votes`, `avg_conf`, `max_conf`); added `topk_hop_t`; added `topk_history`, `topk_head`, `topk_count` fields; added `sim_mode`, `min_event_votes`, `last_patch_path` to both `mod_sst_obj` and `mod_sst_cfg` |
| `src/module/mod_sst.c` | All logic changes (see section below) |
| `demo/odaslive/parameters.c` | Added `parameters_lookup_int_default()`; parse `sst.sim_mode` and `sst.min_event_votes` from `.cfg` file |
| `demo/odaslive/parameters.h` | Declared `parameters_lookup_int_default()` |

#### `mod_sst.c` changes in detail

| Function | Change |
|---|---|
| `mod_sst_construct` | Alloc `topk_history[nTracks][ROLLING_HOPS]`, `topk_head[]`, `topk_count[]`; copy `sim_mode`, `min_event_votes` from config (with range validation); alloc `last_patch_path[nTracks][512]`; resolve `classifier_log_dir` to **absolute path** via `getcwd()` |
| `mod_sst_cfg_construct` | Default `sim_mode = 0`, `min_event_votes = 4` |
| `mod_sst_destroy` | Free `topk_history`, `topk_head`, `topk_count`, `last_patch_path` |
| `reset_track_slot` | Clear `topk_head`, `topk_count`, `last_patch_path[i][0] = '\0'` |
| `push_pot_to_track_buffer` (pot assignment) | 20% semantic boost when top-1 class matches track history |
| `push_pot_to_track_buffer` (hop trigger) | **Fixed cadence**: `(count − 96) % 48 == 0`; call `yamnet_classify_patch_topk()`; write to circular buffer; write `.bin` sidecar when `sim_mode == 1` |
| `compute_event` *(new)* | Reads circular buffer; finds mode top-1 class; returns `sst_event_t{class_id, votes, avg_conf, max_conf}` — `max_conf` is the peak single-hop top-1 confidence for the winner |
| `dump_track_buffers_to_json` | **Gate**: skip track if `topk_count < ROLLING_HOPS` or `ev.votes < min_event_votes`; emit `event_class_id`, `event_class_name`, `event_votes`, `event_avg_confidence`, **`event_max_confidence`**, `event_candidates`, `spectra_file` (absolute path), `topk_history`; **removed** legacy `bins[257]` and `fingerprint` fields |
| *call site* (D6 gate) | Wrapped in `if (timeStamp % ROLLING_HOPS == 0)` — JSON emits at 48 ms cadence, 1:1 with YAMNet evaluations |

### Config files — `sodas/`

Both `local_socket.cfg` and `local_socket1.cfg` gained two new `sst` keys:

```
sim_mode = 1;          # 0 = Pi, 1 = simulator (write .bin sidecars)
min_event_votes = 4;   # 4 of 6 hops must agree before emitting event
```

### Python simulator — `simulator/`

| File | Change summary |
|---|---|
| `analyzer.py` | Detection dict includes all `event_*` fields + `event_max_confidence`; `_resolve_spectra_path()` resolves relative `.bin` paths to absolute; `_derive_label()` uses `event_max_confidence` as primary confidence; `st.rerun()` moved outside spinner; surrogate-safe HTML write; **(session 2)** timestamp uses `timeStamp×0.008` (not `line_num×0.008`); `bins_count` checks `spectra_file` existence; `angle_threshold` default raised `10°→15°`; `_derive_label()` propagates `top_k_candidates` + `ambiguous` flag; YAMNet confidence threshold slider added to curation UI |
| `audio_reconstructor.py` | Added `import os`; new `reconstruct_from_spectra_file(path)` method; new `reconstruct_from_spectra_files(paths)` for stitching multiple hops into ~3 s audio |
| `dataset_manager.py` | Default `confidence_threshold` lowered `0.85 → 0.05` (spatial angular match confidence, not YAMNet class confidence) |
| `yamnet_dataset_curator.py` | Default `min_activity` lowered `0.3 → 0.01`; config-merge-on-load auto-updates existing files; **(session 2)** track-based WAV stitching (1 WAV per track); new `labels.csv` columns: `yamnet_votes`, `yamnet_ambiguous`, `top_k_candidates`; ambiguous samples routed to training with `ambiguous_topk` tag |
| `test_event_pipeline.py` *(new)* | Smoke-test: schema check, vote-gate check, `.bin` integrity, audio reconstruction, analyzer parse |

---

## JSON Output Format (new)

A single emitted event looks like:

```json
{
  "timeStamp": 123456,
  "src": [
    {
      "id": 1,
      "tag": "dynamic",
      "x": 0.72, "y": 0.45, "z": 0.52,
      "activity": 0.91,
      "type": "P",
      "frame_count": 384,

      "event_class_id": 0,
      "event_class_name": "Animal",
      "event_votes": 5,
      "event_avg_confidence": 0.847,
      "event_max_confidence": 0.912,

      "topk_history": [
        {
          "timestamp": 123312,
          "class_ids":   [0, 81, 82, 83, 1],
          "class_names": ["Animal","Bird","Bird vocalization","Chirp","Wild animals"],
          "confidences": [0.87, 0.73, 0.61, 0.44, 0.31]
        },
        ...
      ],

      "spectra_file": "/path/to/ClassifierLogs/patch_1_123360.bin"
    }
  ]
}
```

**Removed fields** (present in old logs, absent in new ones):
- `bins` — the 257-float single-frame magnitude array
- `fingerprint` — the 10-bucket averaged fingerprint
- `class_id`, `class_confidence`, `class_timestamp` — legacy single-hop fields
  (still parsed by analyzer.py for backward compatibility)

---

## Audio Math

```
hopSize          =  128 samples
fS               = 16 000 Hz
frame duration   =    8 ms  (128 / 16 000)

nFramesPerTrack  =   96 frames
buffer duration  =  768 ms  (96 × 8 ms)

YAMNet hop       =   48 frames overlap  →  384 ms per hop
ROLLING_HOPS     =    6 hops
rolling window   = ~2.3 s  (6 × 384 ms)

.bin sidecar size = 96 × 257 × 4 bytes = 98 304 bytes ≈ 96 KB per hop
6 hops stitched  ≈ 578 KB vs. ~1.4 MB inline JSON
```

---

## Running the Tests

```bash
# Build (must pass before running)
cd /home/azureuser/z_odas_newbeamform
cmake --build build -j$(nproc)

# Generate fresh event data
cd build
./bin/odaslive -c /home/azureuser/sodas/local_socket.cfg
# (run for at least 3 s with a real audio source)

# Smoke-test (auto-detects latest session log)
cd /home/azureuser/simulator
python3 test_event_pipeline.py

# Or point at a specific log and override the vote threshold:
python3 test_event_pipeline.py /path/to/sst_session_live.json --min-event-votes 4
```

### Test coverage

| Test | What it checks |
|---|---|
| Schema | `event_*` fields present; `bins`/`fingerprint` absent |
| Vote gate | `event_votes ≥ min_event_votes` for every emitted entry |
| `.bin` sidecar | Each `spectra_file` loads as exactly 96×257 float32 |
| Audio reconstruction | `reconstruct_from_spectra_file()` returns audio with `duration > 0` |
| Analyzer parse | `_parse_odas_output()` populates all new keys in the detection dict |

---

## Deployment Notes

| Setting | Pi / Edge | Simulator / Dev |
|---|---|---|
| `sim_mode` | `0` | `1` |
| `min_event_votes` | `4` (recommended) | `1` for tuning, `4` for accuracy |
| `.bin` files written | No | Yes — one per hop per track |
| JSON size per event | Small (no spectra) | Small + path to `.bin` |

> **Tip**: when tuning a new environment, set `min_event_votes = 1` to see all
> hops in the log, then raise it once YAMNet confidence is stable.

---

## Related Documents

| Document | Contents |
|---|---|
| `docs/CONFIDENCE_INVESTIGATION.md` | Full investigation log (2026-03-01): why confidence is low on wildlife sounds, K=1 vs K=3 vs K=5 analysis, avg vs max confidence, all bug fixes, and next-step proposals |
| `simulator/YAMNET_FINETUNING_README.md` | How to fine-tune YAMNet once a labelled dataset has been collected |
