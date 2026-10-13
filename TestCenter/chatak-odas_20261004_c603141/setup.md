# Setup Guide — Chatak-ODAS

Step-by-step instructions to build and run the repo from scratch on a new machine (x86-64 or Raspberry Pi / ARM).

---

## 1. System packages

Install all apt dependencies in one shot:

First, check your OS and version:

```bash
cat /etc/os-release
# KEY fields: NAME, VERSION_ID, VERSION_CODENAME
```

Then install the packages for your distro:

**Debian 13 "Trixie" / Raspberry Pi OS Trixie (2025+)**

```bash
sudo apt update
sudo apt install -y \
    git cmake build-essential \
    libfftw3-dev \
    libconfig-dev \
    libasound2-dev \
    libpulse-dev \
    libjson-c-dev \
    pkg-config
```

> On Trixie the 64-bit `time_t` transition is complete. `libasound2-dev` pulls
> in `libasound2t64` automatically — no manual intervention needed.

**Debian 12 "Bookworm" / Raspberry Pi OS Bookworm (released 2023+)**

```bash
sudo apt update
sudo apt install -y \
    git cmake build-essential \
    libfftw3-dev \
    libconfig-dev \
    libasound2-dev \
    libpulse-dev \
    libjson-c-dev \
    pkg-config
```

> On Bookworm, `libasound2-dev` pulls in `libasound2t64` automatically due to
> the 64-bit time_t transition. You do not need to do anything extra.

**Debian 11 "Bullseye" / Raspberry Pi OS Bullseye (older Pi images)**

```bash
sudo apt update
sudo apt install -y \
    git cmake build-essential \
    libfftw3-dev \
    libconfig-dev \
    libasound2-dev \
    libpulse-dev \
    libjson-c-dev \
    pkg-config
```

> Same package names — Bullseye is fully supported, no differences needed.

**Ubuntu 22.04 / 24.04 (x86-64 development machine)**

```bash
sudo apt update
sudo apt install -y \
    git cmake build-essential \
    libfftw3-dev \
    libconfig-dev \
    libasound2-dev \
    libpulse-dev \
    libjson-c-dev \
    pkg-config
```

To confirm the ALSA dev package actually installed correctly on any distro:

```bash
dpkg -l libasound2-dev
# Should show "ii" (installed) in the first column
pkg-config --modversion alsa
# Should print a version like 1.2.11 or 1.2.14
```

---

## 2. Clone the repo (with submodules)

```bash
git clone --recurse-submodules https://github.com/anamtya-tech/chatak-odas.git
cd chatak-odas
```

If you already cloned without `--recurse-submodules`, init the submodule manually:

```bash
git submodule update --init --recursive
```

This pulls in `third_party/cJSON` (required by the build).

---

## 3. TensorFlow Lite C library

The build requires:
- Headers: `tensorflow/lite/c/c_api.h`
- Shared library: `libtensorflowlite_c.so`

### Raspberry Pi / aarch64 — bundled, no extra steps needed

The repo ships a pre-built aarch64 `.so` (TF 2.17.0, 3.5 MB, stored in Git LFS)
at `third_party/tflite/aarch64/`. CMakeLists.txt detects `aarch64` automatically
and uses it — you do not need to install or build anything extra.

After `git clone --recurse-submodules`, install git-lfs and pull the LFS object:

```bash
sudo apt install -y git-lfs
git lfs install
git lfs pull
```

Then go straight to [Section 5 — Build](#5-build). `cmake` will print:

```
-- Using bundled aarch64 TFLite: .../third_party/tflite/aarch64
```

If Git LFS is not available on your network, copy the `.so` manually from
another machine:

```bash
# On the Pi, from another machine or USB stick:
cp libtensorflowlite_c.so \
    /path/to/chatak-odas/third_party/tflite/aarch64/libtensorflowlite_c.so
```

---

### x86-64 dev machine — install system-wide

The bundled `.so` is aarch64-only. On x86-64 you need the library under
`/usr/local/`:

**Option A — Use a pre-built binary (fastest)**

If you have a matching `libtensorflowlite_c.so` from another x86-64 build (e.g.
the Azure VM that cross-compiled the Pi version):

```bash
sudo cp /path/to/libtensorflowlite_c.so /usr/local/lib/
sudo ldconfig
sudo mkdir -p /usr/local/include/tensorflow/lite/c
sudo cp /path/to/c_api.h  /usr/local/include/tensorflow/lite/c/
sudo cp /path/to/common.h /usr/local/include/tensorflow/lite/c/
```

**Option B — Build TFLite from source on the machine**

Takes ~10 min on a modern x86-64 machine:

```bash
sudo apt install -y wget default-jdk-headless
wget -q https://github.com/bazelbuild/bazelisk/releases/latest/download/bazelisk-linux-amd64 \
    -O /usr/local/bin/bazel
sudo chmod +x /usr/local/bin/bazel

git clone --depth 1 --branch v2.17.0 \
    https://github.com/tensorflow/tensorflow.git ~/tensorflow
cd ~/tensorflow

bazel build --config=opt //tensorflow/lite/c:libtensorflowlite_c

sudo mkdir -p /usr/local/include/tensorflow
sudo cp -r tensorflow/lite /usr/local/include/tensorflow/lite
sudo cp bazel-bin/tensorflow/lite/c/libtensorflowlite_c.so \
    /usr/local/lib/libtensorflowlite_c.so
sudo ldconfig
```

---

## 4. YAMNet model file

The `.tflite` model is stored in `models/` via **Git LFS**. Pull it after cloning:

```bash
sudo apt install -y git-lfs   # if not already installed (see Section 3)
git lfs pull
```

If Git LFS is not available, download the model manually:

```bash
wget -O models/yamnet_core.tflite \
    https://storage.googleapis.com/anamtya/yamnet_core.tflite
```

`models/yamnet_class_map.csv` is a plain text file — no LFS needed.

---

## 5. Build

```bash
# From repo root
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

Successful output ends with:

```
[100%] Linking CXX executable bin/odaslive
[100%] Built target odaslive
```

The binary is at `build/bin/odaslive`.

### If cmake cannot find TFLite headers (x86-64 only)

On **aarch64 the bundled headers are used automatically** — no action needed.

On **x86-64**, CMakeLists.txt looks under `/usr/local/`. If your TFLite install
is elsewhere, symlink it:

```bash
sudo ln -s /your/tflite/include/tensorflow /usr/local/include/tensorflow
sudo ln -s /your/tflite/libtensorflowlite_c.so /usr/local/lib/libtensorflowlite_c.so
sudo ldconfig
```

---

## 6. Runtime config

Generate the working-directory configs (written to `~/sodas/` by default):

```bash
./scripts/setup_runtime.sh
```

This expands the `config/runtime/*.cfg.template` files and writes them to the
**output directory** (`~/sodas/` by default). That directory is the runtime
working directory — it is separate from the repo and contains:

| Path | What it is |
|---|---|
| `~/sodas/local_socket.cfg` | The config file `odaslive` reads at startup |
| `~/sodas/ClassifierLogs/` | Where ODAS writes `.bin` spectral sidecars and session JSON during a run |

The templates embed the repo path and GUI path so that `odaslive` can find the
model files and emit events to the right sockets. Running the script is how those
absolute paths get baked in.

You can override the defaults:

```bash
./scripts/setup_runtime.sh \
    --odas-dir /path/to/chatak-odas \   # where the repo lives (default: auto-detected)
    --output-dir /path/to/sodas         # where to write the .cfg files (default: ~/sodas)
```

> **No rebuild needed.** The script only writes `.cfg` files — it does not
> touch compiled code. After re-running it, just restart `odaslive`.

---

## 7. Run

```bash
# Live mic (ReSpeaker USB 4-Mic Array)
build/bin/odaslive -c ~/sodas/local_socket.cfg

# Replay a pre-recorded raw audio file (simulation mode)
# Terminal 1 — start ODAS
build/bin/odaslive -c ~/sodas/local_socket.cfg

# Terminal 2 — stream audio
python3 scripts/vm_socket_emit.py \
    --audio /path/to/render.raw \
    --port 10000
```

---

## 8. Cross-compilation (ARM / Raspberry Pi)

Build on an x86-64 host, run on the Pi.

1. Install the ARM toolchain on the host:

   ```bash
   sudo apt install -y gcc-aarch64-linux-gnu g++-aarch64-linux-gnu
   ```

2. Build (or obtain) an `aarch64` `libtensorflowlite_c.so` and matching headers.

3. Run cmake with a toolchain file:

   ```bash
   cmake -B build-arm \
       -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-linux-gnu.cmake \
       -DCMAKE_BUILD_TYPE=Release
   cmake --build build-arm -j$(nproc)
   ```

   > The repo does not currently ship a toolchain file. A minimal example:
   >
   > ```cmake
   > # cmake/aarch64-linux-gnu.cmake
   > set(CMAKE_SYSTEM_NAME Linux)
   > set(CMAKE_SYSTEM_PROCESSOR aarch64)
   > set(CMAKE_C_COMPILER   aarch64-linux-gnu-gcc)
   > set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
   > set(CMAKE_FIND_ROOT_PATH /usr/aarch64-linux-gnu)
   > set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
   > set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
   > set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
   > ```

4. Copy `build-arm/bin/odaslive`, `build-arm/lib/libodas.so`, and
   `libtensorflowlite_c.so` to the Pi, then run:

   ```bash
   export LD_LIBRARY_PATH=/path/to/libs:$LD_LIBRARY_PATH
   ./odaslive -c ~/sodas/local_socket.cfg
   ```

---

## Quick dependency checklist

| Dependency | apt package | Purpose |
|---|---|---|
| cmake ≥ 3.16 | `cmake` | Build system |
| gcc/g++ (C11 + C++14) | `build-essential` | Compiler |
| libfftw3 | `libfftw3-dev` | FFT (SSL/SSS) |
| libconfig | `libconfig-dev` | `.cfg` parsing |
| ALSA | `libasound2-dev` | Audio capture |
| PulseAudio | `libpulse-dev` | PulseAudio source |
| json-c | `libjson-c-dev` | JSON messaging |
| TFLite C API | *(bundled for aarch64; build from source on x86-64 — see §3)* | YAMNet inference |
| cJSON | *(git submodule)* | JSON in odaslive |

---

## Common errors

### `fatal error: tensorflow/lite/c/c_api.h: No such file or directory`

**On aarch64 (Pi):** the bundled headers should be present in `third_party/tflite/include/`. If missing, run `git lfs pull` (and `sudo apt install -y git-lfs` first if needed).

**On x86-64:** TFLite headers are not installed system-wide. Follow **Section 3** above.

### `fatal error: tensorflow/lite/core/c/c_api.h: No such file or directory`

The bundled header set is incomplete — run `git pull` to get the latest repo which includes the full transitive header closure.

### `cannot find -ltensorflowlite_c`

**On aarch64 (Pi):** the bundled `.so` is missing from `third_party/tflite/aarch64/`. Run `git lfs pull`.

**On x86-64:** the `.so` is missing from `/usr/local/lib/`. Copy it there and run `sudo ldconfig`.

### `pkg_check_modules … REQUIRED fftw3f` fails

Install `libfftw3-dev` (provides `fftw3f.pc`).

### `pkg_check_modules … REQUIRED libpulse-simple` fails

Install `libpulse-dev`.

### `git submodule` — cJSON missing

Run `git submodule update --init --recursive` from the repo root.
