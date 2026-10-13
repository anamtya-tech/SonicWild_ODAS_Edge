# Building SonicWild_ODAS_Edge

## System requirements

| Package | Min version | Purpose |
|---------|-------------|---------|
| CMake | 3.16 | Build system |
| GCC / Clang | C11 + C++14 | Compiling C/C++ sources |
| libfftw3-dev | 3.x | FFT (SSL / SSS modules) |
| libconfig-dev | 1.5 | `.cfg` file parsing |
| libasound2-dev | 1.1 | ALSA audio capture |
| libpulse-dev | 13+ | PulseAudio source support |
| libjson-c-dev | 0.13+ | JSON messaging support |
| TFLite runtime | 2.x | YAMNet inference (bundled) |

Install on Debian / Ubuntu:

```bash
sudo apt install -y cmake build-essential \
    libfftw3-dev libconfig-dev libasound2-dev \
    libpulse-dev libjson-c-dev
```

---

## TensorFlow Lite dependency

On `aarch64/arm64`, CMake auto-detects and links the bundled runtime at:

- `third_party/tflite/aarch64/libtensorflowlite_c.so`

On `x86-64`, CMake expects a system install under `/usr/local`:

- headers under `/usr/local/include/tensorflow/...`
- library at `/usr/local/lib/libtensorflowlite_c.so`

If needed, build TFLite C from source:

```bash
git clone https://github.com/tensorflow/tensorflow.git tf_src
cd tf_src
bazel build --config=opt //tensorflow/lite/c:libtensorflowlite_c
```

---

## Build

```bash
# from repo root
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

The resulting binary is `build/bin/odaslive`.

### Useful CMake options

| Option | Default | Description |
|--------|---------|-------------|
| `CMAKE_BUILD_TYPE` | `Release` | Set `Debug` for `-g -O0` |
| `TFLITE_LIB` | auto-detect | Override TFLite `.so` path |
| `ODAS_DISABLE_INSTALL` | OFF | Skip `make install` target |

---

## YAMNet model

Model profiles are stored under `models/` (for example `models/elephant0/`,
`models/elephant1/`, `models/pure/`).  Each profile directory must contain:

- `yamnet_core.tflite`
- `yamnet_class_map.csv`

Set `raw.model_path` in your runtime cfg to the selected profile directory.

---

## Verify the build

```bash
build/bin/odaslive --help
# Should print usage and supported config keys
```

Optional runtime sanity check:

```bash
./scripts/setup_runtime.sh
grep -n "model_path" ~/sodas/local_socket.cfg
```

Ensure `raw.model_path` points to a valid model profile directory.

Quick sanity test with a pre-recorded file:

```bash
# Terminal 1 — run ODAS in simulation mode
build/bin/odaslive -c ~/sodas/local_socket.cfg

# Terminal 2 — stream a 6-channel raw audio file
python3 scripts/vm_socket_emit.py \
    --audio ~/sodas/liveSession_*/render.raw \
    --port 10000
```

Expected output: JSON events on stdout (or to the configured sink socket).

---

## Cross-compilation (ARM / Raspberry Pi)

1. Install an ARM toolchain and a target-arch TFLite `.so`.
2. Set the CMake toolchain file:

```bash
cmake -B build-arm \
    -DCMAKE_TOOLCHAIN_FILE=cmake/arm-linux-gnueabihf.cmake \
    -DTFLITE_LIB=/path/to/arm/libtensorflowlite_c.so
cmake --build build-arm -j$(nproc)
```

The repository does not currently ship a toolchain file; adapt from the
[CMake cross-compile docs](https://cmake.org/cmake/help/latest/manual/cmake-toolchains.7.html).
