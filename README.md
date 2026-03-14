# AetherSDR Linux Native Client

A Linux-native SmartSDR-compatible client for FlexRadio Systems transceivers,
built with **Qt6** and **C++20**.

Current version: **0.1.8**

---

## Features

| Feature | Status |
|---|---|
| UDP discovery (port 4992) | ✅ |
| TCP command/control connection | ✅ |
| SmartSDR protocol parser (V/H/R/S/M) | ✅ |
| Multi-word status object parsing (`slice 0`, `display pan 0x...`) | ✅ |
| Slice model (frequency, mode, filter) | ✅ |
| Frequency dial — click top/bottom half to tune up/down | ✅ |
| Frequency dial — scroll wheel tuning | ✅ |
| Frequency dial — direct keyboard entry (double-click) | ✅ |
| GUI↔radio frequency sync (no feedback loop) | ✅ |
| Mode selector (USB/LSB/CW/AM/FM/DIG…) | ✅ |
| Panadapter VITA-49 UDP stream receiver | ✅ |
| Panadapter spectrum widget (FFT bins) | ✅ |
| Waterfall display (native VITA-49 tiles, PCC 0x8004) | ✅ |
| Panadapter dBm range auto-calibrated from radio | ✅ |
| Audio RX via VITA-49 PCC routing + Qt Multimedia | ✅ |
| RX applet — antenna select (RX/TX), filter presets, AGC mode+threshold | ✅ |
| RX applet — AF gain, audio pan (L/R balance), squelch | ✅ |
| RX applet — NB / NR / ANF DSP toggles | ✅ |
| RX applet — RIT / XIT with ± step buttons | ✅ |
| RX applet — tuning step size stepper (10 Hz – 10 kHz) | ✅ |
| RX applet — tune lock (slice lock/unlock) | ✅ |
| Freq dial — hover-column scroll-wheel tuning (per-digit) | ✅ |
| Spectrum — click-to-tune snaps to step size | ✅ |
| Spectrum — scroll-wheel tunes by step size | ✅ |
| AppletPanel — toggle-button row (ANLG, RX, TX, PHNE, P/CW, EQ) | ✅ |
| VITA-49 meter decode (PCC 0x8002) + MeterModel registry | ✅ |
| Analog S-Meter gauge (ANLG applet) with peak hold | ✅ |
| Tuner applet (4o3a TGXL) — Fwd Power/SWR gauges, relay bars, TUNE/OPERATE | ✅ |
| Tuner auto-detect — TUNE button hidden when no TGXL connected | ✅ |
| Fwd Power gauge auto-scales for barefoot (200 W) vs PGXL (2000 W) | ✅ |
| TX applet — Fwd Power/SWR gauges, RF Power/Tune Power sliders | ✅ |
| TX applet — TX profile dropdown (live from radio) | ✅ |
| TX applet — TUNE/MOX/ATU/MEM buttons + ATU status indicators | ✅ |
| TX applet — APD button with Active/Cal/Avail status inset | ✅ |
| TransmitModel — transmit state, ATU state, profile management | ✅ |
| P/CW applet — mic level gauge with peak hold (-40 to +10 dB, 3-zone) | ✅ |
| P/CW applet — compression gauge (reversed fill, peak hold with slow decay) | ✅ |
| P/CW applet — mic profile dropdown (live from radio) | ✅ |
| P/CW applet — mic source selector, mic level slider, +ACC toggle | ✅ |
| P/CW applet — PROC button + NOR/DX/DX+ 3-position slider + DAX toggle | ✅ |
| P/CW applet — MON button + monitor volume slider | ✅ |
| PHONE applet — AM Carrier level slider | ✅ |
| PHONE applet — VOX toggle + level slider, VOX delay slider | ✅ |
| PHONE applet — DEXP toggle + level slider | ⚠️ fw v1.4.0.0 rejects commands |
| PHONE applet — TX filter Low Cut / High Cut step buttons | ✅ |
| EQ applet — 8-band graphic equalizer (63 Hz – 8 kHz), ±10 dB per band | ✅ |
| EQ applet — independent RX / TX EQ views with ON toggle | ✅ |
| EQ applet — reset button (revert all bands to 0 dB) | ✅ |
| Audio TX (microphone → radio) | ⚠️ stub |
| Volume / mute control | ✅ |
| TX button | ✅ |
| Persistent window geometry | ✅ |

---

## Architecture

```
src/
├── main.cpp
├── core/
│   ├── RadioDiscovery.h/.cpp    # UDP broadcast listener (port 4992)
│   ├── RadioConnection.h/.cpp   # TCP command channel + heartbeat
│   ├── CommandParser.h/.cpp     # Stateless line parser/builder
│   ├── PanadapterStream.h/.cpp  # VITA-49 UDP receiver — FFT + audio routing by PCC
│   └── AudioEngine.h/.cpp       # Qt Multimedia audio sink (push-fed by PanadapterStream)
├── models/
│   ├── RadioModel.h/.cpp        # Central radio state, owns connection
│   ├── SliceModel.h/.cpp        # Per-slice receiver state
│   ├── MeterModel.h/.cpp        # Meter definition registry + value conversion
│   ├── TransmitModel.h/.cpp     # Transmit state, ATU, TX profiles
│   └── EqualizerModel.h/.cpp   # 8-band EQ state (TX + RX)
└── gui/
    ├── MainWindow.h/.cpp        # Main application window
    ├── FrequencyDial.h/.cpp     # Custom 9-digit frequency widget
    ├── ConnectionPanel.h/.cpp   # Radio list + connect/disconnect
    ├── SpectrumWidget.h/.cpp    # Panadapter display (FFT bins)
    ├── AppletPanel.h/.cpp       # Toggle-button applet container
    ├── SMeterWidget.h/.cpp      # Analog S-Meter gauge
    ├── RxApplet.h/.cpp          # Full RX controls applet
    ├── TxApplet.h/.cpp          # TX controls applet (power, ATU, profiles)
    ├── TunerApplet.h/.cpp       # TGXL tuner applet
    ├── PhoneCwApplet.h/.cpp     # P/CW mic controls applet
    ├── PhoneApplet.h/.cpp       # PHONE applet (VOX, AM carrier, TX filter)
    ├── EqApplet.h/.cpp          # 8-band graphic equalizer applet
    └── HGauge.h                 # Shared horizontal gauge widget (header-only)
```

### Data flow

```
                   ┌─────────────────────┐
 UDP bcast (4992)  │   RadioDiscovery    │
 ──────────────────▶   (QUdpSocket)      │──▶ ConnectionPanel (GUI)
                   └─────────────────────┘

 TCP (4992)        ┌─────────────────────┐
 ──────────────────▶   RadioConnection   │──▶ RadioModel ──▶ SliceModel ──▶ GUI
 Commands ◀────────   (QTcpSocket)       │
                   └─────────────────────┘

 UDP VITA-49       ┌─────────────────────┐   FFT bins (PCC 0x8003)
 ──────────────────▶  PanadapterStream   │──────────────────────────▶ SpectrumWidget
  (port 4991)      │  routes by PCC      │   waterfall  (PCC 0x8004)      ↑
                   │                     │──────────────────────────────────┘
                   │                     │   meter data (PCC 0x8002)
                   │                     │──────────────────────────▶ MeterModel ──▶ SMeterWidget
                   │                     │   audio PCM  (PCC 0x03E3)
                   └─────────────────────┘──────────────────────────▶ AudioEngine
                                                                           │
                                                                           ▼
                                                                      QAudioSink
```

---

## Building

### Dependencies

```bash
# Arch / CachyOS
sudo pacman -S qt6-base qt6-multimedia cmake ninja pkgconf

# Ubuntu 24.04+
sudo apt install qt6-base-dev qt6-multimedia-dev cmake ninja-build pkg-config
```

### Configure & build

```bash
git clone https://github.com/ten9876/AetherSDR.git
cd AetherSDR
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j$(nproc)
./build/AetherSDR
```

---

## SmartSDR Protocol Notes (firmware v1.4.0.0)

### Message format

| Prefix | Direction | Example |
|--------|-----------|---------|
| `V`    | Radio→Client | `V1.4.0.0` — firmware version |
| `H`    | Radio→Client | `H479F0832` — assigned client handle |
| `C`    | Client→Radio | `C1\|sub slice all` — command |
| `R`    | Radio→Client | `R1\|0\|` — response (0 = OK) |
| `S`    | Radio→Client | `S479F0832\|slice 0 RF_frequency=14.100000 mode=USB` |
| `M`    | Radio→Client | `M479F0832\|<encoded message>` |

### Status object parsing

Status object names are **multi-word**: `"slice 0"`, `"display pan 0x40000000"`,
`"interlock band 9"`. The parser finds the split point by locating the last space
before the first `=` sign in the status body — this correctly separates the object
name from the key=value pairs.

### Connection sequence (SmartConnect / standalone)

```
TCP connect to radio:4992
  ← V<version>
  ← H<handle>               (client handle assigned)
  → C|sub slice all
  → C|sub tx all
  → C|sub atu all
  → C|sub meter all
  → C|client gui            (required before panadapter/slice creation)
  → C|client program AetherSDR
  [bind UDP socket, send registration packet to radio:4992]
  → C|client set udpport=<port>   (returns 50001000 on v1.4.0.0 — expected)
  → C|slice list
    ← R|0|0                 (SmartConnect: existing slice IDs)
    → C|slice get 0
  ← S|...|slice 0 RF_frequency=14.100000 ...   (subscription push)
  ← S|...|display pan 0x40000000 center=14.1 bandwidth=0.2 min_dbm=-135 max_dbm=-40 ...
  → C|stream create type=remote_audio_rx compression=none
    ← R|0|<stream_id>       (radio starts sending VITA-49 audio to our UDP port)
```

### Firmware v1.4.0.0 quirks

- **`client set udpport`** returns error `0x50001000` ("command not supported").
  UDP port registration must use the one-byte UDP packet method: bind a local UDP
  socket, send a single `\x00` byte to `radio:4992`, and the radio learns our
  IP:port from the datagram source address.
- **`display panafall create`** returns `0x50000016` on this firmware — use
  `panadapter create` instead.
- **Slice frequency** is reported as `RF_frequency` (not `freq`) in status messages.
- **Panadapter bins** are **unsigned uint16**, linearly mapped:
  `dbm = min_dbm + (sample / 65535.0) × (max_dbm - min_dbm)`.
  The `min_dbm` / `max_dbm` values are broadcast in the `display pan` status message.
- **VITA-49 packet type**: all FlexRadio streams (panadapter, audio, meters, waterfall)
  use `ExtDataWithStream` (type 3, top nibble `0x3`). Audio is **not** `IFDataWithStream`.
  Streams are discriminated by **PacketClassCode** (lower 16 bits of VITA-49 word 3):
  - `0x03E3` — remote audio uncompressed (float32 stereo, big-endian)
  - `0x0123` — DAX audio reduced-BW (int16 mono, big-endian)
  - `0x8003` — panadapter FFT bins (uint16, big-endian)
  - `0x8004` — waterfall tiles (36-byte sub-header + Width×Height uint16 bins)
  - `0x8002` — meter data (N × uint16 id + int16 raw_value)
- **Waterfall tile bins** are **unsigned uint16** (big-endian), interpreted as signed
  `int16` and divided by 128 to get an arbitrary intensity scale (not dBm directly).
  Observed noise floor ~96–106, signal peaks ~110–115.  Colour-mapped client-side
  with a focused range (default [104, 120]) for good contrast.
- **Waterfall tile sub-header** (36 bytes at offset 28): `int64 FrameLowFreq`,
  `int64 BinBandwidth`, `uint32 LineDurationMS`, `uint16 Width`, `uint16 Height`,
  `uint32 Timecode`, `uint32 AutoBlackLevel`, `uint16 TotalBinsInFrame`,
  `uint16 FirstBinIndex`.
- **Audio payload byte order**: float32 samples are big-endian; byte-swap the raw
  `uint32` then `memcpy` to `float` before scaling to `int16` for QAudioSink.
- **`stream create type=remote_audio_rx`** is the correct v1.4.0.0 command to start
  RX audio. `audio set` / `audio client` do not exist and return `0x50000016`.
- **VITA-49 stream IDs** (observed from FLEX-8600 v1.4.0.0):
  - `0x40000000` — panadapter FFT (PCC `0x8003`), same value as the pan object ID
  - `0x42000000` — waterfall tiles (PCC `0x8004`)
  - `0x04xxxxxx` — remote audio RX (PCC `0x03E3`), dynamically assigned
  - `0x00000700` — meter data (PCC `0x8002`)

### GUI↔radio frequency sync

`SliceModel` setters (`setFrequency`, `setMode`, etc.) emit `commandReady`
immediately, which `RadioModel` routes to `RadioConnection::sendCommand`.
`MainWindow` uses an `m_updatingFromModel` guard flag to prevent echoing
model-driven dial updates back to the radio.

---

## Changelog

### v0.1.8
- EQ applet: 8-band graphic equalizer (63 Hz – 8 kHz) with vertical sliders
  (±10 dB), independent RX and TX views, ON toggle, and reset button (revert
  all bands to 0 dB with custom-drawn undo arrow icon)
- EqualizerModel: state model for TX and RX EQ bands, parses `eq txsc` / `eq rxsc`
  status objects, emits `eq txsc`/`eq rxsc` commands for enable and per-band gain
- EQ status arrives automatically on connect (no `sub eq all` needed on fw v1.4.0.0)
- PHONE applet: AM Carrier level slider, VOX toggle + level slider, VOX delay
  slider, DEXP toggle + level slider (non-functional on fw v1.4.0.0), TX filter
  Low Cut / High Cut step buttons with inset frequency displays
- TransmitModel: extended with VOX enable/level/delay, AM carrier level, DEXP
  on/level, TX filter low/high commands and status parsing
- P/CW applet: mic level horizontal gauge (-40 to +10 dB) with three-zone
  colouring (cyan/yellow/red) and peak-hold white marker, fed by VITA-49
  MIC and MICPEAK meters (PCC 0x8002, source COD-)
- P/CW applet: compression gauge (reversed fill, red bar) with client-side
  peak hold and slow decay (0.5 dB/update), fed by COMPPEAK meter
- P/CW applet: mic profile dropdown populated live from `profile mic list=`
  status, loads profiles via `profile mic load "<name>"`
- P/CW applet: mic source selector (MIC/BAL/LINE/ACC/PC), mic level slider,
  +ACC accessory mixing toggle
- P/CW applet: PROC (speech processor) toggle, NOR/DX/DX+ 3-position slider,
  DAX toggle
- P/CW applet: MON (sideband monitor) toggle + monitor volume slider
- TransmitModel: extended with mic selection, mic level, mic ACC, speech
  processor enable/level, DAX, sideband monitor, monitor gain, and mic
  profile list/current state; command methods for all new controls
- MeterModel: added MIC/MICPEAK/COMPPEAK cached indices and instantaneous
  mic level tracking; new micMetersChanged signal with 4 parameters
- RadioModel: raw profile status parsing for `profile mic` (handles profile
  names containing spaces); `mic list` command on connect
- HGauge: three-zone fill (cyan → yellow → red) with configurable yellowStart,
  peak-hold white vertical marker, reversed fill mode for compression

### v0.1.7
- TX applet: Forward Power (0–120 W) and SWR (1.0–3.0) horizontal gauges fed
  by radio meter data (VITA-49 PCC 0x8002)
- TX applet: RF Power and Tune Power sliders with live sync to radio
  (`transmit set rfpower=`, `transmit set tunepower=`)
- TX applet: TX profile dropdown populated live from `profile tx list=` status,
  loads profiles via `profile tx load "<name>"`
- TX applet: TUNE button (toggles `transmit tune 1/0`, red while tuning),
  MOX button (`xmit 0/1`), ATU button (`atu start`), MEM toggle
  (`atu set memories_enabled=`)
- TX applet: ATU status indicators — Success/Byp/Mem row and Active/Cal/Avail
  inset with APD button
- TransmitModel: state model for transmit parameters, internal ATU state, and
  TX profile management with command emission
- HGauge extracted to shared header (`src/gui/HGauge.h`) — reused by both
  TunerApplet and TxApplet
- RadioModel: routes `transmit`, `profile tx`, and `atu` status to TransmitModel;
  ATU status dual-routed to both TransmitModel and TunerModel (when TGXL present)

### v0.1.6
- Tuner applet for 4o3a Tuner Genius XL (TGXL): Forward Power and SWR
  horizontal gauges, C1/L/C2 relay position bars, TUNE and OPERATE buttons
- TUNE button turns red during autotune, shows realtime SWR, flashes result
- 3-state OPERATE button: cycles OPERATE (green) → BYPASS (orange) → STANDBY
- Auto-detect: TUNE button and applet hidden when no TGXL connected; appears
  automatically when TGXL detected via amplifier subscription
- Forward Power gauge auto-scales: barefoot radio (0–200 W) vs PGXL amplifier
  (0–2000 W), detected from amplifier model field
- Compact 2-column layout: relay bars (70%) + buttons (30%) in one row
- TunerModel: state model for TGXL (handle, operate, bypass, tuning, relays)
  with command emission via amplifier API (`tgxl set`, `tgxl autotune`)
- RadioModel: amplifier status dispatch — routes TunerGeniusXL to TunerModel,
  detects power amplifiers (PGXL) separately
- **Known bug**: SWR result captured during autotune does not match the actual
  settled SWR (see Known Bugs section)

### v0.1.5
- VITA-49 meter data decode (PCC 0x8002): N × (uint16 meter_id, int16 raw_value)
  pairs with unit-aware conversion (dBm/128, Volts/1024, degF/64)
- MeterModel: meter definition registry from TCP status messages (parses
  `#`-separated `index.key=value` tokens from FlexLib-format meter status)
- Analog S-Meter gauge widget (SMeterWidget): 180° arc with S0–S9 white and
  S9+10/+20/+40/+60 red markings, smoothed needle, peak hold with decay
  (0.5 dB/50ms) and 10-second hard reset, S-unit + dBm text readouts
- AppletPanel: ANLG button toggles S-Meter visibility; button order now
  ANLG, RX, TX, PHNE, P/CW, EQ; shared gradient title bar across all applets
- S-Meter wired to MeterModel "LEVEL" meter from slice source (SLC)

### v0.1.4
- Waterfall display: decode native VITA-49 waterfall tiles (PCC 0x8004) with
  36-byte tile sub-header and uint16 bin payload
- Waterfall colour map: multi-stop gradient (black → blue → cyan → green →
  yellow → red) with focused intensity range for good signal-to-noise contrast
- Waterfall tile values decoded as int16/128 intensity scale; colour range
  tuned to observed noise floor (~104) and signal peaks (~120)
- RadioModel: capture `display waterfall` status, configure waterfall via
  `display panafall set` (auto_black, black_level, color_gain)
- SpectrumWidget: separate waterfall colour range from FFT spectrum dBm range
- PanadapterStream: route PCC 0x8004 to new `waterfallRowReady` signal;
  suppress FFT-derived waterfall rows when native tiles are arriving

### v0.1.3
- RX applet: complete header row (slice badge, tune-lock, RX/TX antenna dropdowns,
  filter-width indicator, QSK toggle)
- RX applet: AGC mode dropdown + AGC-T threshold slider (single row)
- RX applet: AF gain slider; audio pan slider (L/R balance, double-click to re-centre)
- RX applet: squelch on/off + level; NB / NR / ANF DSP toggles
- RX applet: RIT and XIT on/off with painted-triangle ◀ ▶ step buttons
- RX applet: tuning step stepper (10 Hz – 10 kHz) with ◀ ▶ buttons
- Frequency dial: scroll-wheel over a digit now tunes that digit regardless of step size
- Spectrum: click-to-tune snaps to nearest step-size multiple
- Spectrum: scroll-wheel tunes slice frequency by step size per notch
- AppletPanel: replaced QTabWidget with toggle-button column — multiple applets
  can be shown or hidden independently
- SliceModel: added `agc_threshold`, `audio_pan` commands and status-message parsing
- SliceModel: corrected tune-lock to `slice lock <id>` / `slice unlock <id>`
  (was incorrectly using `slice set locked=`)

### v0.1.2
- Fix audio streaming: route VITA-49 packets by PacketClassCode (PCC), not by
  packet type — all FlexRadio streams use `ExtDataWithStream` (type 3)
- Start RX audio with `stream create type=remote_audio_rx compression=none`
  (replaces non-existent `audio set`/`audio client` commands)
- Decode big-endian float32 stereo audio payload correctly for QAudioSink
- Refactor `AudioEngine`: remove its own UDP socket; `PanadapterStream` owns
  port 4991 and pushes decoded PCM via `feedAudioData()`
- Fix double `configurePan` on connect (guard flag moved to `onConnected()`)

### v0.1.1
- Fix status parsing for multi-word object names (`slice 0`, `display pan 0x...`)
- GUI↔radio frequency sync with feedback-loop guard
- Click-to-tune (frequency dial top/bottom halves) and scroll-wheel tuning
- Mode selector (USB/LSB/CW/AM/FM/DIG…) synced to radio

### v0.1.0
- UDP radio discovery (port 4992)
- TCP command/control connection with SmartSDR V/H/R/S/M protocol
- Panadapter VITA-49 UDP stream receiver and FFT spectrum display
- Live dBm range calibration from `display pan` status messages

---

## Known Bugs

- [ ] **Tuner SWR capture inaccurate**: TGXL autotune result SWR displayed on the
  TUNE button does not match the actual settled SWR (shows ~1.5x instead of
  ~1.01–1.15). Race between VITA-49 meter data (UDP) and tuning=0 status (TCP).
- [ ] **Meter scaling needs review**: Level meters across all gauges (S-meter, TX
  power, mic level, compression) may have scaling/range issues that need
  comparison with SmartSDR reference.

---

## Next Steps

- [ ] Slice filter passband shading on the spectrum
- [ ] Multi-slice support (slice tabs or overlaid markers)
- [ ] Audio TX (microphone → radio, full VITA-49 framing)
- [ ] Band stacking / band map

---

## Contributing

PRs welcome. See the modular architecture above — each subsystem is independent
and can be developed/tested in isolation.
