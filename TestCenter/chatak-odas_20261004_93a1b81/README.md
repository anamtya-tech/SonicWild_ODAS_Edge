# ODAS — Chatak YAMNet Fork

**ODAS with embedded YAMNet TFLite sound-event classifier and Chatak bioacoustics pipeline.**

This is a fork of [introlab/odas](https://github.com/introlab/odas) that adds an
on-device YAMNet sound-event classification layer directly inside the ODAS SST
(Sound Source Tracking) module.  It is the firmware component of the *Chatak*
wildlife acoustic-monitoring system.

---

## What's different from upstream ODAS

| Feature                                         |ODAS| Previous fork                            | This fork |
| --- | --- | --- | --- |
| Sound source localisation (SSL)                 | ✅ | ✅ unchanged                            | ✅ unchanged |
| Sound source tracking (SST)                     | ✅ | ✅ + event pipeline                     | ✅ + event pipeline |
| Sound separation / post-filter                  | ✅ | ✅ unchanged                            | ✅ unchanged |
| **YAMNet TFLite classifier**                    | ❌ | ✅ ``src/yamnet/``                      | ✅ + output dequantization + confidence clamp |
| **Top-K voting over track lifetime**            | ❌ | ✅ ``src/module/mod_sst.c``             | ✅ unchanged |
| ``.bin``** sidecar files** (raw spectra)        | ❌ | ✅ written per event                    | ✅ + absolute-path handling |
| **48 ms JSON output gate**                      | ❌ | ✅ ``ROLLING_HOPS=6``                   | ✅ unchanged |
| **Chatak-ID connector**                         | ❌ | ✅ ``src/connector/con_chatak_id.c``    | ✅ unchanged |
| **Simulation mode** (``sst.sim_mode``)          | ❌ | ✅ replay ``.raw`` audio                | ✅ unchanged |
| **Model path from config** (``raw.model_path``) | ❌ | ⚠️ partially hardcoded in older runs    | ✅ config-driven |
| **Track JSON integrity + escaping**             | ❌ | ❌                                      | ✅ ``src/sink/snk_tracks.c``       |
| **YAMNet label sanitization**                   | ❌  | ❌                                     | ✅ ``src/yamnet/yamnet_classifier.cpp`` |
| ReSpeaker USB 4-Mic Array support               | part | ✅ ``config/odaslive/``                | ✅ ``config/odaslive/`` |

---

## Repository layout

```
.
├── CMakeLists.txt            ← modified (adds yamnet, TFLite, cJSON)
├── config/
│   ├── odaslive/             ← mic array geometry configs (upstream)
│   └── runtime/              ← *.cfg.template for ReSpeaker deployment
├── demo/odaslive/            ← modified (main.cpp, objects, params)
├── docs/
│   ├── CONFIDENCE_INVESTIGATION.md
│   └── event_pipeline.md
├── include/
│   ├── odas/                 ← upstream headers + Chatak additions
│   └── yamnet/               ← YAMNet C API headers
├── models/
│   ├── elephant0/            ← profile model directory
│   ├── elephant1/            ← profile model directory
│   └── pure/                 ← profile model directory
├── scripts/
│   ├── setup_runtime.sh      ← instantiate cfg templates → ~/sodas/
│   └── vm_socket_emit.py     ← stream a .raw file to ODAS over TCP
├── src/
│   ├── yamnet/               ← YAMNet C++ wrapper
│   ├── module/mod_sst.c      ← SST + event pipeline
│   └── connector/con_chatak_id.c
└── third_party/cJSON/        ← submodule
```

---

## Quick start

```bash
# 1. Clone (with submodules)
git clone --recurse-submodules https://github.com/anamtya-tech/SonicWild_ODAS_Edge.git
cd SonicWild_ODAS_Edge

# 2. Install build dependencies (Debian/Ubuntu)
sudo apt install -y cmake libfftw3-dev libconfig-dev libasound2-dev

# 3. Build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# 4. Instantiate runtime configs into ~/sodas/
./scripts/setup_runtime.sh

# 5. Pick model profile in runtime cfg (example)
# raw.model_path = "/abs/path/SonicWild_ODAS_Edge/models/elephant0"

# 6. Run with the ReSpeaker USB 4-Mic Array
build/bin/odaslive -c ~/sodas/local_socket.cfg
```

See [BUILDING.md](BUILDING.md) for TFLite dependency details.
See [CONFIGURATION.md](CONFIGURATION.md) for all custom config keys.

---

## Event pipeline summary

```
Microphone (6-ch, 16 kHz)
  └─► SSL → SST (Kalman tracker)
              ├─► every 48 ms: YAMNet inference on beamformed spectra
              │     top-K votes accumulated over track lifetime
              ├─► on track-end: winning class emitted as JSON event
              │     → Unix socket → ChatakGUI / Simulator
              └─► .bin sidecar written (raw spectra for offline re-training)
```

Full design notes: [docs/event_pipeline.md](docs/event_pipeline.md)

---

## Related repos

| Repo | Purpose |
|------|---------|
| [anamtya-tech/yamnet](https://github.com/anamtya-tech/yamnet) | YAMNet model training, export, standalone C++ reference implementation |
| [anamtya-tech/simulator](https://github.com/anamtya-tech/simulator) | Python Streamlit pipeline — scene rendering, ODAS analysis, dataset curation |
| [DaveGamble/cJSON](https://github.com/DaveGamble/cJSON) | JSON library (submodule at `third_party/cJSON`) |
| [introlab/odas](https://github.com/introlab/odas) | Upstream ODAS (MIT) — preserved as `upstream` remote |

### `src/yamnet/` — relationship to `anamtya-tech/yamnet`

The C++ wrapper in `src/yamnet/` originated from `yamnet/integration/` but has
evolved independently for ODAS embedding:
- Uses **TFLite C API** (`TfLiteInterpreterCreate`) rather than the C++ API — better portability on embedded targets
- Adds **TopK inference** (`ClassifyPatchTopK`) for the top-K voting pipeline
- Include path scoped to `"yamnet/yamnet_classifier.h"`

To sync the model file from the yamnet repo:
```bash
cp ~/yamnet/integration/yamnet_core.tflite models/yamnet_core.tflite
```

## Upstream

The current fork tracks [introlab/odas](https://github.com/introlab/odas) (MIT license).
The upstream remote is preserved so fixes can be merged:

```bash
git fetch upstream
git merge upstream/master
```

## License

ODAS and this fork are released under the [MIT](LICENSE) license.

---

## Original ODAS description

ODAS stands for Open embeddeD Audition System — a library for sound source
localisation, tracking, separation and post-filtering, coded in C for
portability on low-cost embedded hardware.
The [ODAS wiki](https://github.com/introlab/odas/wiki) describes the upstream
build procedure.

# Papers


* F. Grondin, D. Létourneau, C. Godin, J.-S. Lauzon, J. Vincent, S. Michaud, S. Faucher, F. Michaud, [ODAS: Open embeddeD Audition System](https://www.frontiersin.org/article/10.3389/frobt.2022.854444), Frontiers in Robotics and AI, Volume 9, 2022 

* F. Grondin and F. Michaud, [Lightweight and Optimized Sound Source Localization and Tracking Methods for Opened and Closed Microphone Array Configurations](https://arxiv.org/pdf/1812.00115), Robotics and Autonomous Systems, 2019 

## Current version highlights (makeshDev)

- YAMNet output handling is hardened for quantized models (`kTfLiteInt8`/`kTfLiteUInt8`) via proper dequantization in `src/yamnet/yamnet_classifier.cpp`.
- Top-K confidences are clamped to `[0, 1]` before event aggregation in `src/module/mod_sst.c`.
- `raw.model_path` is read from runtime config instead of relying on hardcoded host paths (`demo/odaslive/parameters.c`, `demo/odaslive/main.cpp`).
- Track sink JSON output integrity is improved with string escaping and safer sidecar path handling (`src/sink/snk_tracks.c`).
- YAMNet class labels are sanitized before emission (trim/quote cleanup) in `src/yamnet/yamnet_classifier.cpp`.
