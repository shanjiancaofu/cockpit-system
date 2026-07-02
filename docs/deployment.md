# Jetson Deployment

cockpit-system follows the same build/install/package separation used by the referenced zelos
projects:

```text
source repository -> build directory -> staging directory -> release archive -> target root
```

The source repository remains in the developer workspace. The Jetson runtime root is
`/cockpit-system`; source code and CMake intermediate files are never installed there.

## Runtime Layout

```text
/cockpit-system/
├── current -> releases/0.1.0
├── releases/
│   └── 0.1.0/
│       ├── bin/
│       ├── lib/
│       └── share/
├── config/config.yaml
├── models/whisper/ggml-small.bin
├── data/
├── logs/
└── run/
```

Programs and libraries are versioned under `releases`; configuration, models, data, and logs are
shared across upgrades. `current` is switched atomically during installation or rollback.

## Release Build

Create a Release build. Whisper is optional:

```bash
cmake -S . -B build-release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_COCKPIT_UI=ON \
  -DBUILD_WHISPER_CPP_ASR=ON \
  -DWHISPER_CPP_DIR=/home/ffz/code/third_party/whisper.cpp \
  -DWHISPER_CPP_MODEL_PATH=/home/ffz/code/third_party/whisper.cpp/models/ggml-small.bin
cmake --build build-release
ctest --test-dir build-release --output-on-failure
```

On Jetson, enable the whisper.cpp CUDA backend with the CMake option supported by the checked-out
whisper.cpp revision, currently `-DGGML_CUDA=ON`.

## Package

```bash
BUILD_DIR=build-release bash scripts/package.sh
```

The script creates:

```text
stage/cockpit-system-<version>-<system>-<arch>/
dist/cockpit-system-<version>-<system>-<arch>.tar.gz
```

The package contains runtime binaries, optional whisper/GGML libraries, a configuration template,
systemd units, deployment scripts, build metadata, and checksums. The Whisper model is not bundled.

## Install

Copy the archive and model to the Jetson, then run:

```bash
tar -xzf cockpit-system-*.tar.gz
cd cockpit-system-*
sudo bash deploy/install.sh
sudo systemctl enable --now cockpit.target
```

Install the model separately:

```bash
sudo install -m 0644 ggml-small.bin /cockpit-system/models/whisper/ggml-small.bin
```

Update `/cockpit-system/config/config.yaml` with the deployed model path.

## Health Check And Rollback

```bash
sudo bash deploy/healthcheck.sh
sudo bash deploy/rollback.sh 0.1.0
sudo systemctl restart cockpit.target
```

For a non-root installation simulation, use a temporary root and skip systemd:

```bash
COCKPIT_ROOT=/tmp/cockpit-system-test INSTALL_SYSTEMD=false bash deploy/install.sh
```

## Dependency Policy

The release bundles project-built whisper.cpp/GGML shared libraries when enabled. Qt, ALSA, gRPC,
protobuf, yaml-cpp, GStreamer, and platform libraries are supplied by the Jetson OS image to avoid
mixing incompatible system ABIs.
