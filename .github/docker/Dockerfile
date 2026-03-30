FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

# Build dependencies for AetherSDR CI
# Matches the packages installed in ci.yml and codeql.yml
RUN apt-get update && apt-get install -y --no-install-recommends \
    cmake \
    ninja-build \
    pkg-config \
    g++ \
    git \
    qt6-base-dev \
    qt6-multimedia-dev \
    qt6-websockets-dev \
    libgl1-mesa-dev \
    libasound2-dev \
    autoconf \
    automake \
    libtool \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*
