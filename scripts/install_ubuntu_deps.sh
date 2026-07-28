#!/usr/bin/env bash
set -euo pipefail

sudo apt-get update
sudo apt-get install -y \
  build-essential \
  alsa-utils \
  clang-format \
  clang-tidy \
  cmake \
  can-utils \
  iproute2 \
  kmod \
  gstreamer1.0-plugins-base \
  gstreamer1.0-plugins-good \
  gstreamer1.0-tools \
  libgrpc++-dev \
  libasound2-dev \
  libgstreamer1.0-dev \
  libgstreamer-plugins-base1.0-dev \
  libprotobuf-dev \
  libxkbcommon-dev \
  libyaml-cpp-dev \
  ninja-build \
  pkg-config \
  protobuf-compiler \
  protobuf-compiler-grpc \
  pre-commit \
  qml6-module-qtqml-workerscript \
  qml6-module-qtquick \
  qml6-module-qtquick-controls \
  qml6-module-qtquick-layouts \
  qml6-module-qtquick-templates \
  qml6-module-qtquick-window \
  qt6-base-dev \
  qt6-declarative-dev

cmake --version
g++ --version
ninja --version
