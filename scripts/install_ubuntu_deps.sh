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
  ninja-build \
  pkg-config \
  protobuf-compiler \
  protobuf-compiler-grpc

cmake --version
g++ --version
ninja --version
protoc --version
grpc_cpp_plugin --version || true
