# Building the Chatak-ODAS fork

## System requirements

| Package | Min version | Purpose |
|---------|-------------|---------|
| CMake | 3.16 | Build system |
| GCC / Clang | C11 + C++14 | Compiling C/C++ sources |
| libfftw3-dev | 3.x | FFT (SSL / SSS modules) |
| libconfig-dev | 1.5 | `.cfg` file parsing |
| libasound2-dev | 1.1 | ALSA audio capture |
| TFLite runtime | 2.x | YAMNet inference (bundled) |

Install on Debian / Ubuntu:

```bash
sudo apt install -y cmake build-essential \
    libfftw3-dev libconfig-dev libasound2-dev
```

---

## TensorFlow Lite dependency

The TFLite C library is expected at `third_party/libtensorflowlite_c.so` (or the
path set by `TFLITE_LIB` in `CMakeLists.txt`).

If you have a pre-built `.so` from the main system TF build:

```bash
ln -s /home/azureuser/tensorflow/libtensorflowlite_c.so.2.17.1 \
      third_party/libtensorflowlite_c.so
```

Otherwise build TFLite from source:

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

`models/yamnet_core.tflite` (≈14 MB) is tracked via **Git LFS**.

To pull it after clone:

```bash
git lfs pull
```

If Git LFS is not available, download the model manually:

```bash
# Example — replace with actual hosted URL when published
wget -O models/yamnet_core.tflite \
    https://storage.googleapis.com/anamtya/yamnet_core.tflite
```

`models/yamnet_class_map.csv` maps class indices to human-readable labels and
**is** stored as a regular text file (no LFS needed).

---

## Verify the build

```bash
build/bin/odaslive --help
# Should print usage and supported config keys
```

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
