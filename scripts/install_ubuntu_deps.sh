#!/usr/bin/env bash
set -euo pipefail

sudo apt-get update
sudo apt-get install -y \
  build-essential \
  cmake \
  can-utils \
  iproute2 \
  kmod \
  libgrpc++-dev \
  libprotobuf-dev \
  libxkbcommon-dev \
  libyaml-cpp-dev \
  ninja-build \
  pkg-config \
  protobuf-compiler \
  protobuf-compiler-grpc \
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
protoc --version
command -v grpc_cpp_plugin
