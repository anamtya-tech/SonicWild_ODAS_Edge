# ODAS Configuration Reference

This document covers the **Chatak-specific config keys** added on top of the
upstream ODAS `.cfg` format.  For upstream keys (microphone geometry, SSL
parameters, sink/source ports, etc.) see the
[ODAS wiki](https://github.com/introlab/odas/wiki).

---

## Using the runtime templates

Config files contain machine-specific absolute paths and are therefore **not
committed directly** to the repo.  Instead, parameterised templates live in
`config/runtime/*.cfg.template`.

### One-time setup

```bash
./scripts/setup_runtime.sh            # uses ODAS_DIR=$PWD, output → ~/sodas/
# or override paths:
ODAS_DIR=/opt/SonicWild_ODAS_Edge \
CHATAK_GUI_DIR=/opt/ChatakGUI \
ODAS_WORKING_DIR=/data/odas_runtime \
./scripts/setup_runtime.sh
```

This writes `~/sodas/local_socket.cfg` (and `local_socket1.cfg`,
`remote_socket.cfg`) with all `${ODAS_DIR}` placeholders substituted.

---

## Template placeholders

| Placeholder | Default | Description |
|-------------|---------|-------------|
| `${ODAS_DIR}` | repo root | Absolute path to this repo (used for model path and log dir) |
| `${CHATAK_GUI_DIR}` | `~/ChatakGUI` | Path to the ChatakGUI process (live audio socket sink) |

---

## Dynamic localization band (`raw.bandpass`)

```cfg
raw: {
    bandpass = "/home/chatak/ChatakGUI/config/bandpass.cfg";
};
```

The resampler and SSL use this same file. Its format is two integer cutoffs in Hz:

```ini
lowHz=1000
highHz=2000
```

SSL loads the file at construction and checks for changes on each enabled audio
frame. Nanosecond modification times and file identity detect rapid edits and
atomic replacements; replacing a temporary file with `rename`/`os.replace` is
recommended to avoid reading a partially written configuration.

SSL enforces a fixed **1000 Hz minimum**, regardless of the user's lower cutoff:
`effectiveLowHz = max(lowHz, 1000)`. This replaces the older approximately 1200 Hz
fixed mask while retaining low-frequency rejection. After PHAT and cross-spectrum calculation, SSL zeros both real and imaginary
components outside the selected band, before interpolation/correlation. Bin
centers must lie within the effective inclusive Hz limits: lower bin = `ceil(effectiveLowHz*N/fS)`,
upper bin = `floor(highHz*N/fS)`, using the SSL FFT size and its input spectra's
sample rate. At 16 kHz with a 512-point FFT, 1000-2000 Hz keeps bins 32-64.
Selecting 0-2000 Hz also keeps bins 32-64; selecting 1500-2000 Hz keeps bins 48-64.
If `highHz <= 1000`, SSL accepts the setting but zeros all localization bins
and outputs zero positions, scores and peak spectra. This does not immediately
clear SST tracks or classification history. Localization resumes on a subsequent
valid band with usable frequencies above the minimum.

Valid settings require `0 <= lowHz < highHz <= fS/2`. Bands above the minimum
must contain at least one FFT bin.
Missing, malformed, duplicate or out-of-range settings are reported on stderr.
SSL refuses startup without a valid initial band; a failed live update retains
the last valid band. Successful loads print the Hz and bin limits.

This mask affects only localization cross-spectra, not the input spectra used
for beamformed spectra, separation or classification. Those paths still receive
the resampler-filtered audio. STFT/SST objects are not rebuilt; existing queues,
tracking and classification histories are not flushed. SSL and the resampler
reload independently, so this is not a frame-synchronized band switch.
The resampler's existing startup and whole-second update-detection behavior is
unchanged by this SSL patch.

### Regression test

```bash
cmake -S . -B build
cmake --build build --target odaslive test_ssl_bandpass -j2
ctest --test-dir build -R '^ssl_bandpass$' --output-on-failure
```

The test exercises the SSL frame path, both mask edges, startup-file loading,
rapid updates, atomic replacement, minimum-frequency enforcement, disabled-band
output clearing/recovery, invalid-update retention and unchanged input
spectra. It does not validate acoustic direction accuracy.

---

## Custom `sst` config keys

These keys go inside the `sst { ... }` block of your `.cfg` file.

### `sim_mode`

```cfg
sst: {
    sim_mode = true;   # default: false
};
```

When `true`, ODAS reads audio from the TCP socket (`raw` source block) instead
of the ALSA microphone.  Use with `scripts/vm_socket_emit.py` to replay
pre-recorded `.raw` files.

---

### `min_event_votes`

```cfg
sst: {
    min_event_votes = 4;   # code default: 4
};
```

Number of YAMNet inference frames that must agree on the top-K class before a
track-end event is emitted.  Increasing this reduces false-positive events at
the cost of missing short sounds.

Notes:
- Runtime templates may intentionally set `min_event_votes = 1` for dataset collection.
- Valid range is `1..6`; invalid values fall back to `4`.

---

### `classifier_log_dir`

```cfg
sst: {
    classifier_log_dir = "/home/azureuser/sodas/ClassifierLogs";
};
```

Directory where per-session output files are written:

| File | Contents |
|------|----------|
| `sst_session_live.json` | Streaming JSON events (appended per event) |
| `sst_classify_events.json` | Final event list written on shutdown |
| `<track_id>.bin` | Raw mel-spectrogram frames for the track (offline re-training) |

---

### `raw.model_path` (in the `raw` source block)

```cfg
raw: {
    model_path = "/abs/path/SonicWild_ODAS_Edge/models/elephant0";
};
```

Directory containing `yamnet_core.tflite` and `yamnet_class_map.csv`.
Both files must be present.

---

## ReSpeaker USB 4-Mic Array channel mapping

The ReSpeaker USB device exposes 6 channels.  Only channels 2–5 (0-indexed)
carry microphone audio.  Channels 0 and 5 are reference signals.

```cfg
interface: {
    type = "soundcard";
    soundcard = "hw:2,0";   # adjust card index with `aplay -l`
    channels = 6;
    samplerate = 16000;
    framesize = 128;
    # ODAS uses channels listed in the mic geometry config:
    #   config/odaslive/respeaker_usb_4_mic_array.cfg
};
```

The microphone geometry file sets up the four active capsule positions in a
circular array (radius ≈ 4.6 cm).

---

## Example: full local_socket.cfg structure

```cfg
# Audio input
interface: { ... };

# Processing chain
ssl: { ... };          # Sound Source Localisation
sst: {
    sim_mode           = false;
    min_event_votes    = 3;
    classifier_log_dir = "${ODAS_DIR}/ClassifierLogs";
};
sss: { ... };          # Sound Source Separation

# Outputs
potential_output: { type = "socket"; ... };   # SSL potentials
tracked_output:   { type = "socket"; ... };   # SST tracks (JSON)
raw:              { model_path = "${ODAS_DIR}/models"; };
```

Run with:

```bash
build/bin/odaslive -c ~/sodas/local_socket.cfg
```
