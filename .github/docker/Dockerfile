FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

# Build dependencies for AetherSDR CI
# Must include everything CMakeLists.txt can find_package() for
RUN apt-get update && apt-get install -y \
    cmake \
    ninja-build \
    pkg-config \
    g++ \
    git \
    patch \
    curl \
    make \
    python3 \
    qt6-base-dev \
    qt6-multimedia-dev \
    qt6-websockets-dev \
    qt6-serialport-dev \
    qt6-base-private-dev \
    qt6-shader-baker \
    qt6-shadertools-dev \
    libgl1-mesa-dev \
    libasound2-dev \
    libfftw3-dev \
    libhidapi-dev \
    qtkeychain-qt6-dev \
    portaudio19-dev \
    autoconf \
    automake \
    libtool \
    libssl-dev \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Pre-cache Opus neural net model to avoid media.xiph.org dependency at
# CI time. The model is mirrored on our GitHub release. CI workflows
# place this file where Opus's autogen.sh expects it so it skips the
# network download. (#473)
RUN mkdir -p /opt/opus-model \
    && curl -L -o /opt/opus-model/opus_data-4ed9445b96698bad25d852e912b41495ddfa30c8dbc8a55f9cde5826ed793453.tar.gz \
       https://github.com/ten9876/AetherSDR/releases/download/v0.7.16/opus_data-4ed9445b96698bad25d852e912b41495ddfa30c8dbc8a55f9cde5826ed793453.tar.gz
