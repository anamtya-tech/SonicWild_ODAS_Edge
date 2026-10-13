# YAMNet Confidence Investigation — 2026-03-01

## Context

We ran the **wolf–frog–elephant** scene: three clearly-separated animal sound
clips (one per animal, no background noise, no overlap).  This is the simplest
possible test for our pipeline.  Despite clean ODAS tracking, the dataset
curator saved **0 samples** and the reported YAMNet confidence was low (~0.13).
This document records the full investigation, the root causes found, the fixes
applied, and proposals for moving forward.

### Status in current branch

The fixes described here are integrated in the current `makeshDev` codebase,
including timestamp handling with `timeStamp`, absolute/robust spectra path
handling, confidence reporting improvements, and JSON/output hardening.

---

## Experiment Setup

| Item | Value |
|---|---|
| Scene | `wolf_frog_ele` — 3 sources, cleanly separated in time |
| ODAS config | `local_socket.cfg`, `sim_mode=1`, `min_event_votes=1` |
| K (top-K per hop) | 5 |
| Rolling hops | 6 |
| Label strategy | ODAS event voting |

---

## Findings

### F1 — Base YAMNet simply does not know these animals

This is the most important finding. **The low confidence is correct and honest.**

```
Raw per-hop top-1 confidence distribution (all 2209 hops):
  [0.0 – 0.1):   628 hops  (28.4%)
  [0.1 – 0.2):  1444 hops  (65.4%)   ← 93% of hops below 0.2
  [0.2 – 0.3):    67 hops   (3.0%)
  [0.3 – 0.5):     0 hops   (0.0%)
  [0.5 – 1.0):    70 hops   (3.2%)   ← almost exclusively "Whale vocalization"
```

YAMNet was trained on a large but general YouTube corpus.  Wolves, frogs, and
African elephants are **out-of-distribution**.  The 70 high-confidence detections
(`Whale vocalization`, conf ≈ 0.63) are the model "reaching" for the nearest
known sound class to elephant rumbles.

> **Implication**: the goal of this experiment should not be to get high
> classification accuracy from the base model.  It should be to collect labelled
> `.bin` patches that we can use to **fine-tune** the model.  Low confidence is
> a signal to collect more data, not a pipeline bug.

---

### F2 — Increasing K from 1 to 3/5 makes confidence worse, not better

We simulated K=1, K=3, K=5 re-voting on the same `topk_history` data:

| K | Mean winner conf | Median | Who wins (top-3 classes) |
|---|---|---|---|
| **K=1** | **0.175** | 0.149 | Music 338, Burping 276, Vehicle 195 |
| K=3 | 0.131 | 0.117 | Music 454, Vehicle 195, **Alarm 105** |
| K=5 | 0.132 | 0.121 | Music 547, Vehicle 195, Radio 175 |

With K=3/5, lower-ranked candidates (ranks 2–5 have lower confidence than rank 1)
get pooled into the vote.  They either:
- dilute the winner's average confidence, or
- produce enough cross-hop votes to overtake the real top-1 winner with a
  spurious class like "Alarm".

**K=1 is the right setting for low-confidence out-of-distribution sounds.**
K>1 is useful when the model knows the sound class well and you want semantic
neighbours in the vote pool (e.g., "Bird" + "Bird vocalization" + "Chirp" all
supporting the same event).  For wildlife sounds that YAMNet doesn't know,
it just adds noise.

> **Decision**: keep K=5 in the firmware so `topk_history` preserves the full
> top-5 for the visualizer and for post-hoc analysis, but the voting loop in
> `compute_event()` already uses all K — this is fine for confident sounds.
> The issue is purely about the **reported** confidence value.

---

### F3 — Averaging confidence across hops underreports the peak signal

`event_avg_confidence` averages the winner's confidence across **all** its
appearances in the pool (every rank, every hop).  If the class appeared once at
rank 1 (conf=0.3) and twice at rank 3 (conf=0.07), avg = 0.15 — hiding the
real 0.3 peak.

```
Metric comparison on 987 events:
  event_avg_confidence:  mean=0.132  median=0.121  max=0.337
  event_max_confidence:  mean=0.194  median=0.183  max=0.633
```

The `max_conf` (best single-hop top-1 confidence for the winner) is ~50% higher
on average and gives a more honest view of "how confident was the model at its
best moment for this sound".

**Fix applied**: added `max_conf` field to `sst_event_t`; emits
`event_max_confidence` in JSON alongside `event_avg_confidence`.  Python
`_derive_label()` now uses `event_max_confidence` as the primary reported
confidence.

---

### F4 — `spectra_file` paths were relative, resolving against the wrong CWD

ODAS wrote `"./ClassifierLogs/patch_5_1425.bin"` — relative to the ODAS build
directory (`/home/azureuser/z_odas_newbeamform/build/`).  Python's
`os.path.exists()` evaluated it relative to the Streamlit CWD
(`/home/azureuser/simulator/`) → file not found → audio reconstruction returned
`None` → **every sample was silently skipped**.

```
Session file lives at:
  /home/azureuser/z_odas_newbeamform/build/ClassifierLogs/sst_session_live.json

spectra_file as written:   "./ClassifierLogs/patch_5_1425.bin"
Resolved incorrectly:      /home/azureuser/simulator/ClassifierLogs/patch_5_1425.bin
Resolved correctly:        /home/azureuser/z_odas_newbeamform/build/ClassifierLogs/patch_5_1425.bin
```

**Fixes applied**:

1. *C (permanent fix)*: `classifier_log_dir` now set via `getcwd()` at init
   time, so new runs write **absolute paths** directly into the JSON.

2. *Python (backwards-compat fix)*: `_resolve_spectra_path()` helper tries the
   parent of the session file's directory first (= ODAS build dir), then the
   session dir, then CWD.  This handles both old relative-path logs and new
   absolute-path logs.

---

### F5 — `min_activity = 0.3` in the YAMNet curator filtered valid events

The curator config defaulted `min_activity = 0.3` to skip quiet/idle tracks.
In simulation, where source levels are carefully rendered, many tracks had
`activity` values below 0.3 (especially at the edges of a sound event) and were
silently dropped.

**Fix applied**: lowered to `min_activity = 0.01` — only filters genuinely dead
tracks (activity ~= 0).  Also added a config-merge-on-load so existing curator
config files with the old 0.3 value are auto-updated.

---

### F6 — `confidence_threshold = 0.85` in DatasetManager filtered everything

`DatasetManager.save_matches_to_dataset()` filtered on the **spatial angular
match confidence** (cosine of the angular error between detected and ground-truth
direction) using a threshold of 0.85.

```
spatial_confidence = cos(angular_error_deg)
cos(30°) = 0.866  ← just above threshold
cos(32°) = 0.848  ← below threshold, silently dropped
```

With a 30–45° angular threshold in the UI, many valid ground-truth matches were
below 0.85 and dropped.  The label said "987 low-confidence samples skipped"
but those were spatially matched events — just not within 15° of the source.

**Fix applied**: default lowered to `0.05`.  The slider in the UI lets teams
raise it if they want to enforce strict directional accuracy.

> **Note**: "confidence" in `DatasetManager` is the **spatial matching score**
> (how closely ODAS pointed at the right direction), not the YAMNet class
> confidence.  These are two different quantities.  The UI label should be
> updated to make this clearer (tracked as a TODO below).

---

### F7 — HTML report write crashed with `UnicodeEncodeError` (surrogates)

`json-c` can produce strings with invalid UTF-8 sequences in class names
containing emoji or non-ASCII characters.  Python reads them as surrogate pairs
internally, then refuses to write them back with the strict `utf-8` codec.

**Fix applied**: `html_content` is round-tripped through
`encode('utf-8', errors='surrogatepass').decode('utf-8', errors='replace')`
before the file write.  `json.dump` now uses `ensure_ascii=True` to escape all
non-ASCII as `\uXXXX`.

---

### F8 — Streamlit infinite loading spinner

`st.rerun()` was called **inside** `with st.spinner(...)`.  Streamlit re-renders
on `rerun()` — the spinner context was still open on every subsequent render,
creating an infinite loading loop.

**Fix applied**: set a `session_state` flag inside the spinner block, then call
`st.rerun()` **after** the `with` block exits.

---

### F9 — Timestamp derived from line number, not ODAS hop counter *(2026-03-01, session 2)*

This was the root cause of only 1 sample being saved in a 35-second, 3-source run.

The parser in `_parse_odas_output()` computed detection timestamps as:

```python
line_timestamp = (line_num - 1) * 0.008  # WRONG
```

With the 48ms JSON gate active (`ROLLING_HOPS = 6`), the session file has one
line per 48ms of audio — not one line per 8ms hop.  A 35-second run produces
~690 lines.  The formula mapped line 690 → `689 × 0.008 = 5.5 s`, compressing
the entire session into a 5.5-second window.  Ground truth time windows for
**Frog** (15–20 s) and **Elephant** (27–30 s) were completely unreachable; only
the Wolfhowl clip (5–10 s GT window, partially overlapping 3.5–5.5 s compressed
range) produced a partial match.

```
GT window  |  Correct seconds  |  Compressed seconds  |  Reachable?
-----------+-------------------+----------------------+------------
Wolfhowl   |   5.0 – 10.0 s   |   0.0 –  5.5 s       |  Partial ✅
Frog       |  15.0 – 20.0 s   |   —                  |  No  ❌
Elephant   |  27.0 – 30.0 s   |   —                  |  No  ❌
```

The ODAS JSON already contains the correct `timeStamp` field (cumulative hop
counter).  The fix is to multiply it by 8 ms/hop:

```python
line_timestamp = time_stamp * 0.008  # CORRECT: hop_count × 8ms/hop
```

**After fix**: all three GT windows are correctly found.

---

### F10 — `bins_count` always 0 in analysis JSON *(2026-03-01, session 2)*

The serialised match objects in `_save_analysis()` computed `bins_count` as:

```python
'bins_count': len(m['detection']['bins'])
```

The `bins` field is the **legacy** inline 257-float array that was removed from
the firmware in session 1 (replaced by the `.bin` sidecar).  It is always an
empty list now, so `bins_count` was always `0`.

**Fix applied**: `bins_count` now checks whether a valid `spectra_file` path exists:

```python
'bins_count': (1 if m['detection'].get('spectra_file') and
               os.path.exists(m['detection']['spectra_file']) else 0)
```

This correctly reports `1` for every detection that produced a `.bin` sidecar.

---

### F11 — Angular threshold 10° too tight for ODAS localisation jitter *(2026-03-01, session 2)*

Several Frog-source detections were measured at ~10.0° from the GT azimuth —
exactly at the old default threshold of `angle_threshold_deg = 10.0`.  Due to
floating-point precision in the azimuth distance calculation, these sat either
just inside or just outside the threshold depending on the specific frame.

For a planar microphone array running DOA, ±10° is not a large localisation
error; real deployments see 5–20° jitter routinely.  The threshold was raised
to `15.0°` to absorb normal ODAS jitter without sacrificing source discrimination
(sources are ≥45° apart in all current scenes).

**Fix applied**: `CONFIG['angle_threshold_deg']` changed from `10.0` → `15.0`;
UI slider default updated accordingly.

---

## Bug Fix Summary

| # | Component | Bug | Fix |
|---|---|---|---|
| F4 | `mod_sst.c` | `spectra_file` relative path breaks on Python side | `getcwd()` absolute path; Python `_resolve_spectra_path()` fallback |
| F4 | `analyzer.py` | `.bin` files not found → 0 audio samples | `_resolve_spectra_path()` tries ODAS build dir first |
| F5 | `yamnet_dataset_curator.py` | `min_activity=0.3` drops valid events | Lowered to `0.01`; config-merge-on-load for existing files |
| F6 | `dataset_manager.py` | `confidence_threshold=0.85` drops all spatial matches | Lowered to `0.05` |
| F7 | `analyzer.py` | `UnicodeEncodeError` on HTML report write | surrogate-safe encode; `ensure_ascii=True` on JSON dump |
| F8 | `analyzer.py` | Infinite Streamlit spinner | `st.rerun()` moved outside `with st.spinner()` block |
| F9 | `analyzer.py` | Timestamp = `(line_num-1)×8ms` compresses 35 s → 5.5 s | Changed to `timeStamp×8ms`; Frog/Elephant GT windows now reachable |
| F10 | `analyzer.py` | `bins_count` always 0 (reads empty legacy `bins[]`) | Changed to check `spectra_file` existence |
| F11 | `analyzer.py` | `angle_threshold=10°` too tight for ODAS jitter | Widened to `15°`; sources still ≥45° apart in all scenes |

---

## Code Changes (2026-03-01)

### `z_odas_newbeamform/include/odas/module/mod_sst.h`
```c
// Added max_conf field to sst_event_t
typedef struct sst_event_t {
    int   class_id;
    int   votes;
    float avg_conf;   // mean confidence across all top-K appearances of winner
    float max_conf;   // best single-hop top-1 confidence for the winner  ← NEW
} sst_event_t;
```

### `z_odas_newbeamform/src/module/mod_sst.c`

1. `mod_sst_construct`: `classifier_log_dir` now resolved to absolute path via
   `getcwd()` at construction time.
2. `compute_event`: tracks `best_max_conf` alongside `best_avg_conf`; sets
   `ev.max_conf`.
3. `dump_track_buffers_to_json`: emits `"event_max_confidence"` key in JSON.

### `simulator/analyzer.py`

1. `_parse_odas_output`: captures `session_base_dir`; calls
   `_resolve_spectra_path()` for every `spectra_file`.
2. `_resolve_spectra_path` *(new)*: tries parent-of-session-dir → session-dir
   → CWD; handles both old relative and new absolute paths.
3. `_derive_label`: uses `event_max_confidence` (falls back to
   `event_avg_confidence`) as the reported confidence.
4. HTML report write: surrogate sanitisation + `errors='replace'`.
5. `st.rerun()` moved outside spinner.

### `simulator/dataset_manager.py`
- Default `confidence_threshold` lowered: `0.85` → `0.05`.

### `simulator/yamnet_dataset_curator.py`
- Default `min_activity` lowered: `0.3` → `0.01`.
- `_load_or_create_config`: merges defaults on load; force-updates `min_activity`
  if on-disk value is `>= 0.3`.

---

## Code Changes (2026-03-01, session 2)

### `simulator/analyzer.py`

1. `_parse_odas_output` *(timestamp fix, F9)*: line
   ```python
   line_timestamp = (line_num - 1) * 0.008
   ```
   replaced by:
   ```python
   line_timestamp = time_stamp * 0.008  # hop_count × 8ms/hop
   ```
2. `_save_analysis` *(bins_count fix, F10)*: `len(det['bins'])` replaced by
   `spectra_file` existence check.
3. `CONFIG['angle_threshold_deg']` *(F11)*: `10.0` → `15.0`; UI slider default
   follows.
4. `_derive_label` *(top-K propagation)*: returns `top_k_candidates` (full
   ranked list from `event_candidates[]`) and `ambiguous` flag (True when #2
   candidate has same `hop_votes` as #1).
5. `_apply_yamnet_classifications` *(top-K propagation)*: stores
   `top_k_candidates` and `ambiguous` on every match dict.

### `simulator/yamnet_dataset_curator.py`

1. `curate_from_analysis()` routing: ambiguous samples always go to **training**
   (not unknown) — GT label is what matters for fine-tuning; `ambiguous_topk`
   tag added to `curation_reason`.
2. `_save_samples()` metadata: three new CSV columns:
   - `yamnet_votes` — number of hops that agreed on the winning class
   - `yamnet_ambiguous` — True when top-2 candidates tied on hop votes
   - `top_k_candidates` — pipe-separated `Name(Nv,conf)` string of top-5
     candidates, e.g. `Music(3v,0.14)|Frog(3v,0.22)|...`
3. Track-based stitching already in place (prior session): groups `.bin` files
   by `track_id` → single WAV per track, not per `.bin` file.

### `simulator/analyzer.py` — YAMNet confidence threshold slider

- Dataset Curation Settings expander: replaced the legacy spatial-confidence
  slider with a **YAMNet confidence threshold** slider (0.0–1.0, default 0.75,
  step 0.05).  Persists to `curator_config.json` on change and is applied
  before `curate_from_analysis()` runs.
- Semantics: *save sample when `yamnet_conf < threshold`* — i.e. save what the
  base model doesn't know, skip what it already gets right.

### `z_odas_newbeamform/src/module/mod_sst.c` — 48 ms JSON output gate

Previously `dump_track_buffers_to_json()` was called on every 8 ms hop,
emitting stale (unchanged) classifications 5 of every 6 lines.  Changed to:

```c
if (obj->enable_classifier_output &&
    (obj->in1->timeStamp % ROLLING_HOPS == 0)) {
    dump_track_buffers_to_json(...);
}
```

Effect: 6× fewer JSON lines, each 1:1 with a fresh YAMNet evaluation.  A 35 s
run now produces ~690 lines instead of ~4 150.  **Rebuilt**: `[100%] Built
target odaslive`.

---

## Updated JSON Event Format

```json
{
  "event_class_id": 132,
  "event_class_name": "Music",
  "event_votes": 2,
  "event_avg_confidence": 0.068,
  "event_max_confidence": 0.183,
  "event_candidates": [
    { "class_id": 132, "class_name": "Music",             "hop_votes": 2, "avg_confidence": 0.068 },
    { "class_id": 131, "class_name": "Whale vocalization","hop_votes": 1, "avg_confidence": 0.041 }
  ],
  "topk_history": [ ... ],
  "spectra_file": "/home/azureuser/z_odas_newbeamform/build/ClassifierLogs/patch_5_1425.bin"
}
```

Key change: `spectra_file` is now an **absolute path** in all runs after
2026-03-01.  Older log files have relative paths that Python resolves
automatically.

---

## Proposals

### P1 — Fine-tune YAMNet on collected patches (highest priority)

The base model will never give high confidence on wolves, frogs, or elephants.
This is by design — we are using the data collection run to build the training
set.

**Steps:**
1. Run the simulator with `min_event_votes=1` (already set) to collect maximum
   patches.
2. In the Analyzer, use **"Ground truth only"** label strategy — label = spatial
   alignment to scene source (not YAMNet's guess).
3. Curate into the `yamnet_train_001` dataset (audio WAVs + labels.csv).
4. Fine-tune using `YAMNET_FINETUNING_README.md`.
5. Re-run inference with the fine-tuned model; expect confidence to jump to
   0.7+ for known classes.

### P2 — Use `event_max_confidence` as the primary sorting/display metric

`event_avg_confidence` is useful for multi-hop consensus analysis but dilutes
the peak signal.  `event_max_confidence` better answers: *"how confident was
the model at its best moment for this detection?"*

The UI "Classification Distribution" table and HTML report should display
`event_max_confidence` in the confidence column (already updated in
`_derive_label`).

### P3 — Keep K=5, vote only on top-1 per hop for confidence-scarce sounds

Current behaviour: K=5 candidates per hop are pooled for voting.  This is
correct — it lets semantically related classes (e.g., "Bird" + "Bird
vocalization") accumulate votes together.

However, for sounds the model doesn't know (confidence < 0.15), K>1 adds noise.
Consider adding a **confidence-adaptive K** rule in `compute_event`:

```
if (hop.confidences[0] < CONF_LOW_THRESHOLD)   use only top-1 from this hop
else                                            use top-K
```

This preserves semantic pooling when the model is confident and falls back to
strict top-1 when it is guessing.  `CONF_LOW_THRESHOLD = 0.15` is a reasonable
starting point.

### P4 — ~~Rename UI label for `confidence_threshold` in DatasetManager~~ ✅ DONE

The legacy spatial-confidence slider was **replaced** with a YAMNet confidence
threshold slider (0.0–1.0, default 0.75, step 0.05) in the Dataset Curation
Settings expander.  Semantics: *save sample when YAMNet confidence is below
this value* — captures what the model doesn't know yet.  Persists to
`curator_config.json`.

### P5 — Increase `min_event_votes` to 2+ for production data collection

Currently `min_event_votes=1` fires on every fully-warm buffer.  For clean
training data, raise to 2 or 3: the same class must appear as top-1 in at least
2 of 6 hops.  This eliminates transient noise events while still capturing
sounds that the model is uncertain about.

---

## Next Experiment Checklist

```
[x] Timestamp bug fixed — detections now span the full 35s run
[x] bins_count now reflects .bin sidecar presence correctly
[x] Angular threshold widened to 15° to absorb localisation jitter
[x] 48ms JSON gate active — 6× fewer lines, all fresh classifications
[x] Top-K candidates + ambiguous flag propagated to labels.csv
[x] YAMNet confidence threshold slider in UI (default 0.75, persists)
[x] Track-based WAV stitching — 1 WAV per track, not per .bin file

[ ] Run fresh ODAS simulation → expect ~4-6 WAVs for wolf_frog_ele
[ ] Verify labels.csv has yamnet_votes / yamnet_ambiguous / top_k_candidates
[ ] Collect ≥50 samples per class across multiple runs
[ ] Proceed to fine-tuning (see YAMNET_FINETUNING_README.md)
```
