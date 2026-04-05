# AetherSDR

**A Linux-native client for FlexRadio Systems transceivers**

[![CI](https://github.com/ten9876/AetherSDR/actions/workflows/ci.yml/badge.svg)](https://github.com/ten9876/AetherSDR/actions/workflows/ci.yml)
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![Qt6](https://img.shields.io/badge/Qt-6-green.svg)](https://www.qt.io/)
[![Signed Commits](https://img.shields.io/badge/commits-GPG%20signed-brightgreen?logo=gnuprivacyguard)](https://github.com/ten9876/AetherSDR/commits/main)

AetherSDR brings FlexRadio operation to Linux without Wine or virtual machines. Built from the ground up with Qt6 and C++20, it speaks the SmartSDR protocol natively and aims to replicate the full SmartSDR experience.

**Current version: 0.8.3** | [Download](https://github.com/ten9876/AetherSDR/releases/latest) | [Discussions](https://github.com/ten9876/AetherSDR/discussions) | [What's New](https://github.com/ten9876/AetherSDR/releases)

> **Cross-platform downloads available:** Linux AppImage, macOS universal DMG, Windows installer and portable ZIP.
> Linux is the primary supported platform. macOS and Windows builds are provided as a courtesy.

![AetherSDR Screenshot](docs/screenshot-v5.png)

<p><i>Native. Open. Yours.</i></p>

---

## Highlights

- **GPU-accelerated rendering** — QRhi waterfall + FFT spectrum on GPU (OpenGL/Metal/D3D11), 71% CPU reduction, heat map FFT display
- **Multi-slice operation** — color-coded VFO overlays, independent TX assignment, diversity mode with ESC beamforming
- **Multi-panadapter** — up to 4 pans with 6 layout options, per-pan display controls, native VITA-49 waterfall tiles
- **Full RX/TX controls** — filter presets, AGC, DSP, EQ, mic/compression gauges, TX profiles, ATU, TUNE/MOX
- **Client-side noise reduction** — NR2 (spectral), RN2 (RNNoise neural), BNR (NVIDIA GPU AI denoiser)
- **CW decoder** — real-time Morse decode with auto pitch/speed detection and confidence coloring
- **SpotHub** — DX Cluster, RBN, WSJT-X, POTA, and FreeDV Reporter spots with density badges and auto-mode switch
- **DAX virtual audio** — 4 RX + 1 TX channels for WSJT-X, fldigi, VARA, JS8Call (Linux PulseAudio/PipeWire, macOS CoreAudio)
- **DAX IQ streaming** — raw I/Q to SDR apps at 24/48/96/192 kHz
- **SmartLink remote operation** — Auth0 login, TLS command channel, WAN UDP streaming with credential persistence
- **TCI server** — full TCI v2.0 protocol over WebSocket: CAT + audio + IQ + CW + spots in one connection
- **CAT control** — 4-channel rigctld TCP + virtual serial ports, CW macros for contest loggers
- **MIDI controller mapping** — Learn mode, 50+ parameters, named profiles
- **FlexControl USB tuning knob** — auto-detect, acceleration, configurable buttons
- **Serial PTT/CW keying** — USB-serial DTR/RTS for external keyers and foot switches
- **FreeDV RADE** — AI-based digital voice codec with client-side neural encoder/decoder
- **4o3a Tuner Genius XL** — relay control, autotune, 3x1 antenna switch, SWR/power gauges
- **Multi-Flex** — independent operation alongside SmartSDR/Maestro with clickable dashboard
- **Adaptive predistortion (APD)** — SmartSignal toggle and status display for FLEX-8000 series

---

## Supported Radios

Works with any FlexRadio transceiver running SmartSDR firmware v3.x or v4.x: FLEX-6400, 6400M, 6600, 6600M, 6700, 8400, 8400M, 8600, 8600M, and Aurora (AU-510, AU-520) series.

---

## Download

Pre-built binaries are available from [Releases](https://github.com/ten9876/AetherSDR/releases/latest):

| Platform | Download | Notes |
|----------|----------|-------|
| **Linux x86_64** | `AetherSDR-*-x86_64.AppImage` | Single file, no install needed. `chmod +x` and run. |
| **Linux ARM** | `AetherSDR-*-aarch64.AppImage` | Raspberry Pi, ARM laptops. `chmod +x` and run. |
| **macOS** | `AetherSDR-*-macOS-apple-silicon.dmg` | Apple Silicon (M1+). Intel Macs via Rosetta. Signed & notarized. |
| **Windows Installer** | `AetherSDR-*-Windows-x64-setup.exe` | Setup wizard with Start Menu shortcut and uninstaller. |
| **Windows Portable** | `AetherSDR-*-Windows-x64-portable.zip` | No install needed. Extract and run. |

---

## Building from Source

### Dependencies

Install all dependencies for a full-featured build. Optional packages are noted — the build succeeds without them but the corresponding features are disabled.

```bash
# Arch / CachyOS / Manjaro
sudo pacman -S qt6-base qt6-multimedia qt6-websockets qt6-serialport \
  qt6-shadertools cmake ninja pkgconf autoconf automake libtool \
  fftw portaudio hidapi qtkeychain-qt6

# Ubuntu 24.04+ / Debian / Linux Mint
sudo apt install qt6-base-dev qt6-base-private-dev qt6-multimedia-dev \
  qt6-websockets-dev qt6-serialport-dev qt6-shader-baker qt6-shadertools-dev \
  cmake ninja-build pkg-config autoconf automake libtool \
  libfftw3-dev portaudio19-dev libhidapi-dev qtkeychain-qt6-dev \
  libxkbcommon-dev gstreamer1.0-pulseaudio gstreamer1.0-plugins-base

# Fedora
sudo dnf install qt6-qtbase-devel qt6-qtbase-private-devel qt6-qtmultimedia-devel \
  qt6-qtwebsockets-devel qt6-qtserialport-devel qt6-qtshadertools-devel \
  cmake ninja-build autoconf automake libtool \
  fftw3-devel portaudio-devel hidapi-devel qtkeychain-qt6-devel

# macOS (Homebrew)
brew install qt@6 ninja cmake pkgconf autoconf automake libtool \
  fftw portaudio hidapi qtkeychain
```

<details>
<summary>What each dependency enables</summary>

| Package | Feature |
|---------|---------|
| qt6-base, qt6-multimedia | Core application (required) |
| qt6-base-private-dev | GPU-accelerated spectrum/waterfall (QRhi) |
| qt6-shadertools-dev | GPU shader compilation |
| qt6-websockets-dev | TCI server, FreeDV Reporter spots |
| qt6-serialport-dev | FlexControl, serial PTT/CW, MIDI controllers |
| libfftw3-dev | NR2 spectral noise reduction |
| portaudio19-dev | PortAudio audio backend |
| libhidapi-dev | StreamDeck, USB HID encoders (RC-28, PowerMate) |
| qtkeychain-qt6-dev | SmartLink credential persistence |

</details>

> **Linux Mint / Ubuntu note:** If PC audio devices show as "Dummy Output",
> install `gstreamer1.0-pulseaudio`. For PipeWire systems, also install `gstreamer1.0-pipewire`.

### Build & Run

```bash
git clone https://github.com/ten9876/AetherSDR.git
cd AetherSDR
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j$(nproc)
./build/AetherSDR
```

### Install (optional, Linux)

```bash
sudo cmake --install build
```

---

## Roadmap

- [ ] GPU-accelerated spectrum/waterfall via QRhi (#391)
- [ ] TCI WebSocket server for single-connection integration (#528)
- [ ] DAX audio channels on Windows (#87)
- [ ] CW ultimatic keyer mode (#416)
- [ ] Detachable/pop-out panadapter windows (#246)

See the full [issue tracker](https://github.com/ten9876/AetherSDR/issues) for all planned features.

---

## Contributing

PRs, bug reports, and feature requests welcome! See [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines.

**Development environment:** AetherSDR is developed using [Claude Code](https://claude.com/claude-code) as the primary development tool. We encourage contributors to use Claude Code for consistency. PRs must follow project conventions, pass CI, and include GPG-signed commits.

**Not a developer?** Click the lightbulb button in AetherSDR's title bar to create an AI-assisted bug report or feature request.

---

## Verifying Downloads

Linux and Windows binaries are GPG-signed. macOS artifacts are Apple notarized. Each release includes `.asc` signatures and `SHA256SUMS.txt`.

```bash
curl -sSL https://raw.githubusercontent.com/ten9876/AetherSDR/main/docs/RELEASE-SIGNING-KEY.pub.asc | gpg --import
gpg --verify AetherSDR-v1.0.0-x86_64.AppImage.asc AetherSDR-v1.0.0-x86_64.AppImage
```

See [docs/VERIFYING-RELEASES.md](docs/VERIFYING-RELEASES.md) for full instructions.

---

## License

AetherSDR is free and open-source software licensed under the [GNU General Public License v3](LICENSE).

*AetherSDR is an independent project and is not affiliated with or endorsed by FlexRadio Systems.*
