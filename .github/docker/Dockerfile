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
    libgl1-mesa-dev \
    libasound2-dev \
    libfftw3-dev \
    portaudio19-dev \
    autoconf \
    automake \
    libtool \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Pre-build Opus with OSCE/DRED support to avoid media.xiph.org dependency
# at CI time. The neural net model is pre-downloaded from our GitHub release
# (mirrored from xiph.org) and placed where autogen.sh expects it, so it
# skips the network download entirely. (#473)
RUN mkdir -p /tmp/opus-build && cd /tmp/opus-build \
    && curl -L -o opus.zip https://github.com/xiph/opus/archive/940d4e5af64351ca8ba8390df3f555484c567fbb.zip \
    && mkdir src && cd src && python3 -c "import zipfile; zipfile.ZipFile('/tmp/opus-build/opus.zip').extractall()" \
    && mv opus-*/* . && rmdir opus-* \
    && curl -L -o opus_data-4ed9445b96698bad25d852e912b41495ddfa30c8dbc8a55f9cde5826ed793453.tar.gz \
       https://github.com/ten9876/AetherSDR/releases/download/v0.7.16/opus_data-4ed9445b96698bad25d852e912b41495ddfa30c8dbc8a55f9cde5826ed793453.tar.gz \
    && ./autogen.sh \
    && ./configure --with-pic --enable-osce --enable-dred --disable-shared --disable-doc --disable-extra-programs \
    && make -j$(nproc) \
    && mkdir -p /opt/opus-cache \
    && cp -a . /opt/opus-cache/opus-src \
    && cd / && rm -rf /tmp/opus-build
