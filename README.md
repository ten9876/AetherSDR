<p align="center">
  <img src="docs/logo-circle.png" alt="AetherSDR Logo" width="200">
</p>

# AetherSDR

**A Linux-native client for FlexRadio Systems transceivers**

[![CI](https://github.com/ten9876/AetherSDR/actions/workflows/ci.yml/badge.svg)](https://github.com/ten9876/AetherSDR/actions/workflows/ci.yml)
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![Qt6](https://img.shields.io/badge/Qt-6-green.svg)](https://www.qt.io/)

AetherSDR brings FlexRadio operation to Linux without Wine or virtual machines. Built from the ground up with Qt6 and C++20, it speaks the SmartSDR protocol natively and aims to replicate the full SmartSDR experience.

**Current version: 0.4.9** | [Download](https://github.com/ten9876/AetherSDR/releases/latest) | [Discussions](https://github.com/ten9876/AetherSDR/discussions)

> **Cross-platform downloads available:** Linux AppImage, macOS universal DMG, and Windows ZIP.
> Linux is the primary supported platform. macOS and Windows builds are provided as a courtesy
> but are unsupported and low priority until post-v1.0.

![AetherSDR Screenshot](docs/screenshot-v4.png)

---

## Supported Radios

Tested with the **FLEX-8600** running v4.1.5 software. Should work with other FlexRadio models that use the SmartSDR protocol (FLEX-6000 series, FLEX-8000 series).

---

## Features

### Multi-Slice Operation
- Color-coded slice markers on spectrum and waterfall (A=cyan, B=magenta, C=green, D=yellow)
- Click inactive slice badge to switch focus
- Independent TX assignment — TX stays on its slice until explicitly moved
- Clickable TX badge in VFO overlay (red = active TX, grey = click to assign)
- Close (✕) and lock (🔒) buttons floating beside the VFO overlay
- +RX button creates new slices on the current panadapter
- Right-click context menu to close slices
- Off-screen slice indicators with frequency display

### Panadapter & Waterfall
- Real-time FFT spectrum and scrolling waterfall display
- Draggable FFT/waterfall split, bandwidth zoom, pan
- Draggable filter passband edges
- dBm scale with drag-to-adjust, time scale on waterfall
- ARRL band plan overlay on FFT display (color-coded CW/DATA/PHONE segments with license classes)
- Spot frequency markers with hover tooltips (QRP calling, beacons, SSTV, etc.)
- Floating VFO widget with S-meter, frequency, and quick controls
- Band selector with ARRL band plan defaults
- Display sub-menu: AVG, FPS, FFT fill (opacity + color), weighted average
- Waterfall controls: gain, black level (+ auto), scroll rate
- Native VITA-49 waterfall tiles with automatic FFT fallback during TX
- FFT and waterfall rendering fully decoupled
- All overlays consume mouse/wheel events (no accidental VFO tuning)

### Receiver Controls
- Full RX controls: antenna, filter presets, AGC, AF gain, pan, squelch
- All DSP modes: NB, NR, NR2 (spectral), ANF, NRL, NRS, RNN, NRF, ANFL, ANFT, APF
- **NR2 spectral noise reduction** — Ephraim-Malah MMSE-LSA with OSMS floor tracking (contributed by @EI6JGB)
- FFTW3 optimized FFTs with automatic radix-2 fallback and background wisdom generation
- DSP level sliders (0-100) for all supported features
- Mode-specific controls (CW pitch-centered filters, RTTY mark/shift, DIG offset, FM duplex)
- RIT / XIT with step buttons
- Per-mode filter presets and tuning step sizes
- Dynamic mode list from radio (supports FDVU, FDVM, and future modes)

### Transmit Controls
- TX power and tune power sliders
- TX/mic profile management
- TUNE, MOX, ATU, and MEM buttons
- ATU status indicators and APD control
- P/CW applet: mic level/compression gauges, mic source selector, processor, monitor
- PC audio TX via DAX stream — auto-starts when mic source = PC
- Mic metering with RX monitoring (met_in_rx)
- PHONE applet: VOX, AM carrier, TX filter
- 8-band graphic equalizer (RX and TX)

### Metering
- Analog S-Meter gauge with peak hold
- TX power, SWR, mic level, and compression gauges
- 4o3a Tuner Genius XL (TGXL) applet with relay bars and autotune

### FM Duplex
- CTCSS tone encode (41 standard tones)
- Repeater offset with simplex/up/down direction
- REV (reverse) toggle

### Tracking Notch Filters (TNF)
- Right-click spectrum or waterfall to add TNF at any frequency
- +TNF button creates a notch at the center of the filter passband
- Drag TNF markers to reposition
- Right-click TNF to adjust width (50/100/200/500 Hz) and depth (Normal/Deep/Very Deep)
- Permanent TNFs (green) survive radio power cycles; temporary (yellow) are session-only
- TNF markers render across both spectrum and waterfall with grab handles
- Global TNF enable/disable synced with radio

### CAT Control & Integration
- 4-channel Hamlib rigctld TCP server (ports 4532-4535), one per slice (A-D)
- Virtual serial ports (PTY) at `/tmp/AetherSDR-CAT-A` through `-D`
- PTT auto TX-switch: keying a channel moves TX to that channel's slice
- Autostart options for rigctld and TTY
- Supports: get/set frequency, get/set mode, PTT, split, dump_state

### SmartLink Remote Operation (beta)
- Log in with FlexRadio SmartSDR+ account (email/password via Auth0)
- Radio auto-discovered via SmartLink relay server
- Full TCP command channel over TLS — tune, change modes, all controls work remotely
- UDP streaming (FFT, waterfall, audio) in progress

### Connectivity
- Auto-discovery of radios on the local network (UDP broadcast)
- Manual connection for routed networks (cross-subnet, VLANs)
- Saved routed radios auto-probe and reconnect on launch
- SmartLink for internet remote access
- Auto-reconnect on connection loss
- Auto-connect to last used radio on launch

### Audio
- Radio audio outputs: line out gain/mute, headphone gain/mute, front speaker mute
- PC audio input/output device selection with live switching
- Full settings in Radio Setup → Audio tab

### Radio Setup
- Full settings dialog (9 tabs): Radio, Network, GPS, Audio, TX, Phone/CW, RX, Filters, XVTR
- Per-band TX settings: RF power, tune power, PTT inhibit, interlock routing
- TX profile management
- XVTR transverter configuration
- **Firmware update** — upload .ssdr files directly from Linux (no Windows required)
- Network diagnostics, memory channels, spot settings

### General
- Click-to-tune and scroll-wheel tuning on spectrum
- Right-click context menu on spectrum and waterfall
- Persistent display settings: WNB, RF gain, black level saved across sessions
- Multi-Flex support (independent operation alongside SmartSDR/Maestro)
- XML settings persistence (SSDR-compatible format)
- Persistent window layout and display preferences
- Cross-platform: Linux (primary), macOS, Windows
- Desktop integration (`.desktop` file, icon, `cmake --install`, AUR package)
- PC audio TX via DAX stream (mic TX also supported)
- Security hardened: redacted logs, restricted file permissions

---

## Download

Pre-built binaries are available from [Releases](https://github.com/ten9876/AetherSDR/releases/latest):

| Platform | Download | Notes |
|----------|----------|-------|
| **Linux x86_64** | `AetherSDR-*-x86_64.AppImage` | Single file, no install needed. `chmod +x` and run. |
| **Linux ARM** | `AetherSDR-*-aarch64.AppImage` | Raspberry Pi, ARM laptops. `chmod +x` and run. |
| **macOS** | `AetherSDR-*-macOS-universal.dmg` | Intel + Apple Silicon. Drag to Applications. |
| **Windows** | `AetherSDR-*-Windows-x64.zip` | Extract and run `AetherSDR.exe`. |

Linux is the primary development platform and receives the most testing. macOS and Windows
builds are provided as a convenience — they compile from the same codebase but are not
actively tested or supported. Bug reports are welcome but fixes for macOS/Windows issues
will be low priority until after v1.0.

---

## Building from Source

### Dependencies (Linux)

```bash
# Arch / CachyOS / Manjaro
sudo pacman -S qt6-base qt6-multimedia cmake ninja pkgconf

# Ubuntu 24.04+ / Debian / Linux Mint
sudo apt install qt6-base-dev qt6-multimedia-dev cmake ninja-build pkg-config \
  gstreamer1.0-pulseaudio gstreamer1.0-plugins-base

# Fedora
sudo dnf install qt6-qtbase-devel qt6-qtmultimedia-devel cmake ninja-build

# macOS (Homebrew)
brew install qt@6 ninja cmake
```

> **Linux Mint / Ubuntu note:** If PC audio devices show as "Dummy Output" or empty,
> install the GStreamer PulseAudio plugin: `sudo apt install gstreamer1.0-pulseaudio`.
> For PipeWire-based systems (Mint 22+, Ubuntu 24.04+), also install `gstreamer1.0-pipewire`.

### Build & Run

```bash
git clone https://github.com/ten9876/AetherSDR.git
cd AetherSDR
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j$(nproc)
./build/AetherSDR
```

The application will automatically discover FlexRadio transceivers on your local network.

### Install (optional, Linux)

Install the binary, desktop entry, and icon system-wide:

```bash
sudo cmake --install build
```

This places `AetherSDR` in `/usr/local/bin`, the `.desktop` file in the app launcher, and the icon in the system icon theme.

---

## Roadmap

- [ ] SmartLink UDP streaming (FFT, waterfall, audio over WAN)
- [ ] DAX audio channels — PipeWire virtual devices for digital mode apps (FreeDV, WSJT-X, fldigi, JS8Call)
- [ ] Multi-slice support
- [ ] Band stacking registers
- [ ] Spot / DX cluster integration
- [ ] CW keyer and memory support
- [ ] Keyboard shortcuts and hotkeys

See the full [issue tracker](https://github.com/ten9876/AetherSDR/issues) for 45+ tracked features and enhancements.

---

## Contributing

PRs, bug reports, and feature requests welcome! See [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines.

**Not a developer?** You can still contribute great feature requests using AI. See the [AI-Assisted Feature Requests](CONTRIBUTING.md#ai-assisted-feature-requests) section in our contributing guide — it walks you through using Claude.ai to turn your idea into a well-structured request that's easy for us to implement.

The codebase is modular — each subsystem (core protocol, models, GUI widgets) can be worked on independently. Check [Issues](https://github.com/ten9876/AetherSDR/issues) for current tasks.

---

## License

AetherSDR is free and open-source software licensed under the [GNU General Public License v3](LICENSE).

*AetherSDR is an independent project and is not affiliated with or endorsed by FlexRadio Systems.*
