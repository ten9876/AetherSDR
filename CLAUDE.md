# AetherSDR — Project Context for Claude

## Project Goal

Replicate the **Windows-only FlexRadio SmartSDR client** (written in C#) as a
**Linux-native C++ application** using Qt6 and C++20. The aim is to mirror the
look, feel, and every function SmartSDR is capable of. The reference radio is a
**FLEX-8600 running firmware v1.4.0.0**.

## AI Agent Guidelines

When helping with AetherSDR:
- Prefer C++20 / Qt6 idioms (std::ranges, concepts if clean, Qt signals/slots over lambdas when possible)
- Keep classes small and single-responsibility
- Use RAII everywhere (no naked new/delete)
- Comment non-obvious protocol decisions with firmware version
- When suggesting code: show **diff-style** changes or full function/class if small
- Test suggestions locally if possible (assume Arch Linux build env)
- Never suggest Wine/Crossover workarounds — goal is native
- Flag any proposal that would break slice 0 RX flow
- If unsure about protocol behavior → ask for logs/wireshark captures first
- **Use `AppSettings`, never `QSettings`** — see "Settings Persistence" below
- **Read `CONTRIBUTING.md`** for full contributor guidelines, coding conventions,
  and the AI-to-AI debugging protocol (open a GitHub issue for cross-agent coordination)

## Build

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j$(nproc)
./build/AetherSDR
```

Dependencies (Arch): `qt6-base qt6-multimedia cmake ninja pkgconf autoconf automake libtool`

Current version: **0.7.18.6** (set in both `CMakeLists.txt` and `README.md`).

---

## CI/CD Workflow

### CI (GitHub Actions)

CI uses a Docker container (`ghcr.io/ten9876/aethersdr-ci:latest`) with all
build dependencies pre-installed. Build time is ~3.5 minutes consistently.

**When adding a new system dependency:**
1. Add the package to `.github/docker/Dockerfile`
2. Push the Dockerfile change — `docker-ci-image.yml` triggers automatically
3. Wait for the image to build (~3 min) before the next CI run can use it

### Git Aliases

**`git ship`** — Squashes all local commits ahead of origin/main into one,
creates a PR branch, pushes, opens a PR with auto-squash-merge enabled.
Use this to batch multiple commits into a single PR.

```bash
# Accumulate work locally, then ship once:
git commit -m "Fix A"
git commit -m "Fix B"
git ship   # squashes both into one PR
```

**`git release <version> [notes]`** — Triggers the release workflow which
bumps version in CMakeLists.txt/README.md/CLAUDE.md, creates a PR,
auto-merges, waits, then tags. Supports X.Y.Z and X.Y.Z.P versions.

```bash
git release 0.7.13 "Bug fixes and performance improvements"
```

### Branch Protection

- All commits to `main` require GPG signatures
- CI build must pass before merge
- Branches auto-delete after PR merge
- Enable "Automatically delete head branches" in repo settings

---

## Architecture Overview

```
src/
├── main.cpp
├── core/
│   ├── AppSettings         — XML settings persistence (~/.config/AetherSDR/), replaces QSettings
│   ├── RadioDiscovery      — UDP 4992 broadcast listener, emits radioDiscovered/Lost
│   ├── RadioConnection     — TCP 4992 command channel, V/H/R/S/M SmartSDR protocol
│   ├── CommandParser       — Stateless protocol line parser + command builder
│   ├── PanadapterStream    — VITA-49 UDP receiver: routes FFT, waterfall, audio, meters by PCC
│   ├── AudioEngine         — QAudioSink RX + QAudioSource TX, NR2/RN2 pipeline, Opus codec
│   ├── SpectralNR          — NR2: Ephraim-Malah MMSE-LSA spectral noise reduction (FFTW3)
│   ├── RNNoiseFilter       — RN2: Mozilla/Xiph RNNoise neural noise suppression
│   ├── CwDecoder           — Real-time CW decode via ggmorse, confidence scoring
│   ├── Resampler           — r8brain-free-src polyphase resampler wrapper (24k↔48k, etc.)
│   ├── RADEEngine          — FreeDV RADE v1 digital voice encoder/decoder
│   ├── SmartLinkClient     — Auth0 login + TLS command channel for remote operation
│   ├── WanConnection       — SmartLink WAN TCP+UDP streaming with NAT keepalive
│   ├── RigctlServer        — rigctld-compatible TCP server (4 channels)
│   ├── RigctlProtocol      — Hamlib rigctld protocol parser
│   ├── RigctlPty           — Virtual serial port (PTY) for CAT control
│   ├── PipeWireAudioBridge — Linux DAX: PulseAudio pipe modules (4 RX + 1 TX)
│   ├── VirtualAudioBridge  — macOS DAX: CoreAudio HAL plugin shared memory bridge
│   ├── SerialPortController— USB-serial PTT/CW keying (DTR/RTS out, CTS/DSR in)
│   ├── FlexControlManager  — FlexControl USB tuning knob (VID 0x2192, 9600 8N1)
│   ├── MidiControlManager  — MIDI controller mapping (RtMidi, Learn mode, 50+ params)
│   ├── MidiSettings        — Dedicated XML settings for MIDI bindings (~/.config/AetherSDR/midi.settings)
│   ├── OpusCodec           — Opus encode/decode for SmartLink WAN audio compression
│   ├── LogManager          — Per-module Qt Logging Categories with persistence
│   ├── SupportBundle       — Collect logs+settings+sysinfo into tar.gz/zip for bug reports
│   ├── MacMicPermission    — macOS AVFoundation mic permission request at startup
│   ├── FirmwareStager      — Download SmartSDR installer, extract .ssdr files
│   └── FirmwareUploader    — TCP upload of .ssdr firmware to radio
├── models/
│   ├── RadioModel          — Central state: owns connection, slices, panadapters, TX ownership
│   ├── SliceModel          — Per-slice state (freq, mode, filter, DSP, RIT/XIT, panId)
│   ├── PanadapterModel     — Per-panadapter state (center, bandwidth, dBm, antenna, WNB)
│   ├── MeterModel          — Per-slice meter registry + VITA-49 value conversion
│   ├── TransmitModel       — Transmit state, internal ATU, TX profile management
│   ├── EqualizerModel      — 8-band EQ state for TX and RX (eq txsc / eq rxsc)
│   ├── TunerModel          — 4o3a Tuner Genius XL state (relays, SWR, tuning)
│   ├── TnfModel            — Tracking notch filter management (add/remove/drag)
│   ├── UsbCableModel       — USB cable management (CAT/BCD/Bit/Passthrough)
│   ├── DaxIqModel          — DAX IQ stream state (4 channels, worker thread, PulseAudio pipes)
│   ├── BandSettings        — Per-band persistent settings
│   └── AntennaGeniusModel  — 4o3a Antenna Genius switch state
└── gui/
    ├── MainWindow          — Dark-themed QMainWindow, wires everything together
    ├── TitleBar            — Menu bar + PC Audio + master/HP volume + TX owner + Feature Request
    ├── ConnectionPanel     — Floating radio list + connect/disconnect popup
    ├── PanadapterStack     — Vertical QSplitter hosting N PanadapterApplets
    ├── SpectrumWidget      — FFT spectrum + scrolling waterfall + frequency scale
    ├── SpectrumOverlayMenu — Left-side DSP/display overlay on spectrum
    ├── VfoWidget           — VFO display: frequency, mode, filter, DSP tabs, passband
    ├── SupportDialog       — Per-module logging toggles, log viewer, AI-assisted bug reports
    ├── AppletPanel         — Toggle-button column of applet panels (VU, RX, TX, etc.)
    ├── SMeterWidget        — Analog S-Meter/Power gauge with peak hold, 3-tier power scale
    ├── RxApplet            — Full RX controls: antenna, filter, AGC, AF gain, pan, DSP, RIT/XIT
    ├── TxApplet            — TX controls: power gauges/sliders, profiles, ATU, TUNE/MOX
    ├── TunerApplet         — 4o3a TGXL tuner: gauges, relay bars, TUNE/OPERATE
    ├── PhoneCwApplet       — P/CW mic controls: level/compression gauges, mic profile, PROC/DAX/MON
    ├── PhoneApplet         — PHONE applet: VOX, AM carrier, DEXP, TX filter low/high
    ├── EqApplet            — 8-band graphic equalizer applet (TX/RX views)
    ├── CatApplet           — DIGI applet: CAT/DAX controls, rigctld, PTY, DAX enable, MeterSliders
    ├── AntennaGeniusApplet — 4o3a Antenna Genius port/band display
    ├── PanadapterApplet    — Display settings: AVG, FPS, fill, gain, black level, DAX rate
    ├── RadioSetupDialog    — Radio setup (9 tabs): Radio, Network, GPS, Audio, TX, etc.
    ├── MemoryDialog        — Memory channel manager with editable name column
    ├── ProfileManagerDialog— Global/TX/mic profile management
    ├── MidiMappingDialog   — MIDI controller binding manager (Learn mode, profiles)
    ├── SpotSettingsDialog   — Spot/DX cluster settings
    ├── SpotHubDialog        — Unified spot manager (6 tabs: DX Cluster, RBN, WSJT-X, POTA, Spot List, Display)
    ├── SpotDensityBadge     — Clickable spot count badge with expand-to-list popup
    ├── NetworkDiagnosticsDialog — SmartLink network diagnostics
    ├── MeterSlider         — Combined level meter + gain slider widget (DAX channels)
    ├── HGauge.h            — Shared horizontal gauge widget (header-only)
    ├── ComboStyle.h        — Shared dark combo box styling helper
    └── SliceColors.h       — Per-slice color assignments
```

### Data Pipelines

Multi-thread architecture — up to 11 threads depending on features enabled:
- **Main thread**: GUI rendering (paintEvent), RadioModel + all sub-models, user input
- **Connection thread**: RadioConnection (TCP 4992 I/O, kernel TCP_INFO RTT)
- **Audio thread**: AudioEngine (RX/TX audio, NR2/RN2 DSP, QAudioSink/Source)
- **Network thread**: PanadapterStream (VITA-49 UDP parsing, FFT/waterfall/meter demux)
- **ExtControllers thread**: FlexControl, MIDI, SerialPort (USB/serial I/O, RtMidi callbacks)
- **Spot thread**: DxCluster, RBN, WSJT-X, POTA, FreeDV spot clients
- **CwDecoder thread**: ggmorse decode loop (QThread::create, on-demand)
- **DAX IQ thread**: DaxIqModel worker (byte-swap + pipe I/O)
- **RADE thread**: RADEEngine neural encoder/decoder (on-demand, HAVE_RADE)
- **BNR thread**: NvidiaBnrFilter gRPC async I/O (std::thread, HAVE_BNR)
- **DXCC parse thread**: DxccColorProvider ADIF log parser (one-shot at startup)

```
┌─────────────────────────────────────────────────────────────────────┐
│                      NETWORK LAYER                                  │
│                                                                     │
│  Radio UDP 4992 ──→ RadioDiscovery ──→ ConnectionPanel    [MAIN]    │
│  Radio TCP 4992 ──→ RadioConnection ──→ RadioModel        [CONN]→[MAIN] │
│  Radio UDP 4991 ──→ PanadapterStream (VITA-49 demux)      [NETWORK] │
│  TGXL  TCP 9010 ──→ TgxlConnection ──→ TunerModel        [MAIN]    │
│  WAN   TLS 4992 ──→ WanConnection ──→ RadioModel          [MAIN]    │
│  WAN   UDP 4993 ──→ PanadapterStream                      [NETWORK] │
└─────────────────────────────────────────────────────────────────────┘
                              │
                    ┌─────────┴─────────┐
                    │  PanadapterStream  │  ◄── NETWORK THREAD
                    │  (VITA-49 demux)   │      QMutex guards stream IDs
                    └──┬──┬──┬──┬──┬────┘
                       │  │  │  │  │
    ┌──────────────────┘  │  │  │  └──────────────────┐
    ▼ [queued]            ▼  │  ▼ [queued]            ▼ [queued]
┌────────┐         ┌────────┐│┌────────┐        ┌──────────┐
│PCC 8003│         │PCC 8004│││PCC 8002│        │PCC 03E3/ │
│FFT bins│         │WF tiles│││ meters │        │0123 audio│
└───┬────┘         └───┬────┘│└───┬────┘        └────┬─────┘
    │                  │     │    │                   │
    ▼ MAIN             ▼     │    ▼ MAIN              ▼ AUDIO THREAD
SpectrumWidget   SpectrumWidget  MeterModel     AudioEngine
.updateSpectrum  .updateWfRow    .updateValues   .feedAudioData
    │                  │     │    │                   │
    │ (ring buffer)    │     │    ├─→ SMeterWidget    ├─→ NR2 (SpectralNR)
    │ + NB blanker     │     │    ├─→ TxApplet        ├─→ RN2 (RNNoiseFilter)
    ▼                  ▼     │    ├─→ TunerApplet     ├─→ BNR (NvidiaBnrFilter)
  paintEvent()    paintEvent │    └─→ StatusBar       ├─→ CwDecoder [MAIN]
  (~98% CPU)                 │                        ▼
                             │                   QAudioSink
                             │                   (speakers)
                             │
                      ┌──────┴──────┐
                      │ DAX streams │
                      └──────┬──────┘
                             ▼ MAIN
                     PipeWireAudioBridge ──→ Virtual Audio Devices
                     (PulseAudio pipes)     (WSJT-X, fldigi, etc.)

TX AUDIO PIPELINE:                          ◄── AUDIO THREAD
  QAudioSource (mic) ──→ AudioEngine.onTxAudioReady()
                              │
              ┌───────────────┼───────────────┐
              ▼               ▼               ▼
         Voice mode      DAX TX mode      RADE mode
              │               │               │
              ▼               ▼               ▼
         Opus encode    DaxBridge.feed    RADEEngine.feed
              │               │               │
              ▼               ▼               ▼
         VITA-49 pkt    VITA-49 pkt      OFDM modem
              │               │               │
              └───────┬───────┘               │
                      ▼ [queued to NETWORK]    ▼
              PanadapterStream         PanadapterStream
              .sendToRadio()           .sendToRadio()
              (QUdpSocket)             (QUdpSocket)
                      │                       │
                      ▼                       ▼
                 Radio UDP 4991          Radio UDP 4991

TCP COMMAND PIPELINE (bidirectional):
  GUI widget ──→ SliceModel.setXxx() ──→ emit commandReady("slice ...")  [MAIN]
                 TransmitModel          ──→ emit commandReady("xmit ...")
                 TunerModel             ──→ emit commandReady("tgxl ...")
                 EqualizerModel         ──→ emit commandReady("eq ...")
                 TnfModel               ──→ emit commandReady("tnf ...")
                        │
                        ▼
                 RadioModel.sendCmd(cmd)                                  [MAIN]
                   │ stores callback in m_pendingCallbacks
                   │ allocates sequence number
                   ▼ [QMetaObject::invokeMethod → queued to CONN thread]
              ┌─────────────────┐
              │ RadioConnection  │  ◄── CONNECTION THREAD
              │ writeCommand()   │      heap-allocated, moveToThread
              │ QTcpSocket       │      init() creates socket on thread
              │ ping RTT timer   │      measures RTT at socket read time
              └────────┬────────┘
                       │
              ┌────────┴────────┐
              ▼                 ▼
         QTcpSocket       WanConnection    ◄── WAN stays on MAIN (TLS)
              │                 │
              └────────┬────────┘
                       ▼
                     Radio

  Radio ──→ "S<handle>|<object> key=val ..."
              │
              ▼ CONNECTION THREAD
        RadioConnection.processLine()
              │ emits statusReceived, commandResponse
              ▼ [auto-queued signal to MAIN]
        RadioModel.onStatusReceived()                                    [MAIN]
              │
              ├─→ SliceModel.applyStatus()      ──→ GUI signals
              ├─→ PanadapterModel.applyStatus()  ──→ SpectrumWidget
              ├─→ TransmitModel.applyStatus()    ──→ TxApplet
              ├─→ TunerModel.applyStatus()       ──→ TunerApplet
              ├─→ EqualizerModel.applyStatus()   ──→ EqApplet
              ├─→ MeterModel.registerMeter()     ──→ meter definitions
              └─→ Multi-Flex client tracking     ──→ TitleBar badge

        RadioModel.onCommandResponse()                                   [MAIN]
              │ looks up callback by sequence number
              └─→ invokes callback on main thread (safe model access)

EXTERNAL CONTROL PIPELINES:                 ◄── EXTCONTROLLERS THREAD
  FlexControl (USB serial) ──→ FlexControlManager ──┐
  MIDI (RtMidi)            ──→ MidiControlManager  ──┼─→ [auto-queued signals]
  SerialPort (PTT/CW)     ──→ SerialPortController ──┘       │
                                                              ▼ MAIN THREAD
                                                     MainWindow dispatches:
                                                       ├─→ RadioModel (tune, TX, CW)
                                                       ├─→ SliceModel (freq, gain, DSP)
                                                       └─→ AudioEngine (mute, mic gain)
  TGXL (TCP 9010)          ──→ TgxlConnection      ──→ TunerModel relay adjust [MAIN]

SPOT PIPELINES:                             ◄── SPOT WORKER THREAD
  DX Cluster (telnet)  ─┐
  RBN (telnet)         ─┤
  WSJT-X (UDP mcast)  ─┼──→ SpotModel (batched 1/sec) ──→ SpectrumWidget spots
  POTA (HTTP polling)  ─┤     + DxccColorProvider (ADIF lookup)
  FreeDV (WebSocket)   ─┘
```

**Thread summary:**

| Thread | Components | CPU | Creation | Notes |
|--------|-----------|-----|----------|-------|
| **Main** | paintEvent, RadioModel, all sub-models, all GUI widgets | ~97% | Qt default | Dominated by QPainter waterfall blit |
| **Connection** | RadioConnection, QTcpSocket, kernel TCP_INFO RTT | ~0% | moveToThread | Heap-allocated, init() slot pattern |
| **Audio** | AudioEngine, NR2/RN2 DSP, QAudioSink/Source, TX encoding | ~1.5% | moveToThread | std::atomic flags, recursive_mutex for DSP lifecycle |
| **Network** | PanadapterStream, QUdpSocket, VITA-49 parsing, per-stream stats | ~0.3% | moveToThread | QMutex guards stream ID sets |
| **ExtControllers** | FlexControlManager, MidiControlManager, SerialPortController | ~0% | moveToThread | USB serial I/O, RtMidi, poll timers |
| **Spot** | DxCluster, RBN, WSJT-X, POTA, FreeDV clients | ~0% | moveToThread | Batched 1/sec forwarding |
| **CwDecoder** | ggmorse decode loop | ~0% | QThread::create | On-demand start/stop per CW mode |
| **DAX IQ** | DaxIqModel worker | ~0% | moveToThread | Byte-swap + pipe I/O |
| **DXCC** | DxccColorProvider ADIF parser | ~0% | moveToThread | One-shot at startup |
| **RADE** | RADEEngine neural encoder/decoder | ~0% | moveToThread | On-demand, HAVE_RADE |
| **BNR** | NvidiaBnrFilter gRPC async I/O | ~0% | std::thread | GPU container, HAVE_BNR |

**Cross-thread signals (auto-queued):**
- Connection → Main: statusReceived, messageReceived, commandResponse, pingRttMeasured
- Main → Connection: writeCommand (via QMetaObject::invokeMethod), connectToRadio, disconnectFromRadio
- Network → Main: spectrumReady, waterfallRowReady, meterDataReady, daxAudioReady
- Network → Audio: audioDataReady
- Audio → Network: txPacketReady (→ sendToRadio)
- Audio → Main: levelChanged, pcMicLevelChanged, nr2/rn2/bnrEnabledChanged
- Audio → CwDecoder: feedAudio (lock-free ring buffer)
- Main → Audio: setNr2/Rn2/BnrEnabled (via QMetaObject::invokeMethod)
- Main → Audio: startRxStream/stopRxStream (via helper methods)
- Main → Network: registerPanStream, setDbmRange (QMutex-protected setters)
- ExtControllers → Main: tuneSteps, buttonPressed, externalPttChanged, cwKeyChanged, paramAction
- Main → ExtControllers: setTransmitting, loadSettings, open/close (via QMetaObject::invokeMethod)
- CwDecoder → Main: textDecoded, statsUpdated (auto-queued)

**Design principle:** Everything except GUI rendering and model dispatch runs
on a dedicated worker thread. RadioModel owns all sub-models as value members
on the main thread — GUI accesses models directly with no pointer indirection.
Each worker thread has a single responsibility and communicates exclusively via
auto-queued signals. The main thread handles only paintEvent + model updates.

**Remaining bottleneck:** Main thread at ~97% is entirely QPainter waterfall
rendering (`p.drawImage()` blit). With waterfall closed, main thread drops to
2-3%. GPU offload (#391) is the path to resolving this.

---

## SmartSDR Protocol (Firmware v1.4.0.0)

### Message Types

| Prefix | Dir | Meaning |
|--------|-----|---------|
| `V` | Radio→Client | Firmware version |
| `H` | Radio→Client | Hex client handle |
| `C` | Client→Radio | Command: `C<seq>\|<cmd>\n` |
| `R` | Radio→Client | Response: `R<seq>\|<hex_code>\|<body>` |
| `S` | Radio→Client | Status: `S<handle>\|<object> key=val ...` |
| `M` | Radio→Client | Informational message |

### Status Object Names

Object names are **multi-word**: `slice 0`, `display pan 0x40000000`,
`display waterfall 0x42000000`, `interlock band 9`. The parser finds the split
between object name and key=value pairs by locating the last space before the
first `=` sign.

### Connection Sequence

1. TCP connect → radio sends `V<version>` then `H<handle>`
2. Subscribe: `sub slice all`, `sub pan all`, `sub tx all`, `sub atu all`,
   `sub meter all`, `sub audio all`
3. `client gui` + `client program AetherSDR` + `client station AetherSDR`
4. Bind UDP socket, send `\x00` to radio:4992 (port registration)
5. `client udpport <port>` (returns error 0x50001000 on v1.4.0.0 — expected)
6. `slice list` → if empty, create default slice (14.225 MHz USB ANT1)
7. `stream create type=remote_audio_rx compression=none` → radio starts sending
   VITA-49 audio to our UDP port

### Firmware v1.4.0.0 Quirks

- `client set udpport` returns `0x50001000` — use the one-byte UDP packet method
- `display panafall create` returns `0x50000016` — use `panadapter create`
- Slice frequency is `RF_frequency` (not `freq`) in status messages
- All VITA-49 streams use `ExtDataWithStream` (type 3, top nibble `0x3`)
- Streams are discriminated by **PacketClassCode** (PCC), NOT by packet type
- `slice m <freq> pan=<panId>` — the radio echoes `active=0/1` for slice
  switching as a side effect, even though the client didn't request it
- `radio set full_duplex_enabled` — accepted (R|0) but no status echo
- `audio_level` is the status key for AF gain (not `audio_gain`)
- `cw key immediate` returns `0x50001000` — **not supported** on fw v4.1.5.
  Must use netcw UDP stream (`stream create netcw`) for CW keying
- `transmit set break_in=1` returns `0x5000002D` — correct command is
  `cw break_in 1` (per FlexLib Radio.cs CWBreakIn property)
- The radio **never sends `mox=` in transmit status messages**. Do NOT use
  `isMox()` for TX state detection — it always returns false. Use
  `isTransmitting()` instead, which tracks the interlock state machine
  (READY → PTT_REQUESTED → TRANSMITTING → UNKEY_REQUESTED → READY)
- Three separate tune command paths all need interlock inhibit:
  `transmit tune 1` (barefoot), `tgxl autotune` (TGXL external tuner),
  `atu start` (internal ATU). Each goes through a different `commandReady`
  handler in RadioModel

---

## VITA-49 Packet Format

### Header (28 bytes)

Words 0–6 of the VITA-49 header. Key field: **PCC** in lower 16 bits of word 3.

### Packet Class Codes

| PCC | Content | Payload Format |
|------|---------|---------------|
| `0x8003` | FFT panadapter bins | uint16 big-endian, linear map to dBm |
| `0x8004` | Waterfall tiles | 36-byte sub-header + uint16 bins |
| `0x03E3` | RX audio (uncompressed) | float32 stereo, big-endian |
| `0x0123` | DAX audio (reduced BW) | int16 mono, big-endian |
| `0x8002` | Meter data | N × (uint16 meter_id, int16 raw_value), big-endian |

### FFT Bin Conversion

The radio encodes FFT bin values as **pixel Y positions** (0 = top/max_dbm,
ypixels-1 = bottom/min_dbm), NOT as 0-65535 uint16 range:

```
dBm = max_dbm - (sample / (y_pixels - 1.0)) × (max_dbm − min_dbm)
```

`min_dbm` / `max_dbm` come from `display pan` status messages (typically -135 / -40).
`y_pixels` comes from `display pan` status (must be tracked per-stream via
`PanadapterStream::setYPixels()`).

### FFT Frame Assembly

FFT data may span multiple VITA-49 packets. A 12-byte sub-header at offset 28
contains: `start_bin_index`, `num_bins`, `bin_size`, `total_bins_in_frame`,
`frame_index`. `PanadapterStream::FrameAssembler` stitches partial frames.

### Waterfall Tile Format

36-byte sub-header at offset 28:

| Offset | Type | Field |
|--------|------|-------|
| 0 | int64 | FrameLowFreq |
| 8 | int64 | BinBandwidth |
| 16 | uint32 | LineDurationMS |
| 20 | uint16 | Width |
| 22 | uint16 | Height |
| 24 | uint32 | Timecode |
| 28 | uint32 | AutoBlackLevel |
| 32 | uint16 | TotalBinsInFrame |
| 34 | uint16 | FirstBinIndex |

Payload: `Width × Height` uint16 bins (big-endian). Conversion:

```
intensity = static_cast<int16>(raw_uint16) / 128.0f
```

This yields an **arbitrary positive intensity scale** (NOT actual dBm).
Observed values: noise floor ~96–106, signal peaks ~110–115 on HF.
The waterfall colour range is calibrated to [104, 120] by default and is
**decoupled** from the FFT spectrum's dBm range.

### Audio Payload

- PCC 0x03E3: big-endian float32 stereo → byte-swap uint32, memcpy to float,
  scale to int16 for QAudioSink (24 kHz stereo)
- PCC 0x0123: big-endian int16 mono → byte-swap, duplicate to stereo

### Meter Data Payload (PCC 0x8002)

Payload is N × 4-byte pairs: `(uint16 meter_id, int16 raw_value)`, big-endian.
Value conversion depends on unit type (from FlexLib Meter.cs):

| Unit | Conversion |
|------|-----------|
| dBm, dB, dBFS, SWR | `raw / 128.0f` |
| Volts, Amps | `raw / 1024.0f` (v1.4.0.0) |
| degF, degC | `raw / 64.0f` |

### Meter Status (TCP)

Meter definitions arrive via TCP status messages with `#` as KV separator
(NOT spaces like other status objects). Format:
`S<handle>|meter 7.src=SLC#7.num=0#7.nam=LEVEL#7.unit=dBm#7.low=-150.0#7.hi=20.0`

The S-Meter is the "LEVEL" meter from source "SLC" (slice).

### Stream IDs (observed)

- `0x40000000` — panadapter FFT (same as pan object ID)
- `0x42000000` — waterfall tiles
- `0x04xxxxxx` — remote audio RX (dynamically assigned)
- `0x00000700` — meter data

---

## GUI Design

### Theme

Dark theme: background `#0f0f1a`, text `#c8d8e8`, accent `#00b4d8`, borders `#203040`.

### Layout

Three-pane horizontal splitter:
1. **ConnectionPanel** (left) — radio list, connect/disconnect
2. **Center** — SpectrumWidget (top: FFT 40%, bottom: waterfall 60%, frequency
   scale bar 20px), FrequencyDial below, mode selector, TX button, volume controls
3. **AppletPanel** (right, 260px fixed) — toggle-button row (ANLG, RX, TX, PHNE, P/CW, EQ),
   S-Meter gauge (toggled by ANLG), scrollable applet stack below

### SpectrumWidget

- FFT spectrum: exponential smoothing (α=0.35), dB grid every 20 dB, freq grid ~50 kHz
- Waterfall: 7-stop colour gradient (black→dark blue→blue→cyan→green→yellow→red)
- Overlays: filter passband (semi-transparent), slice center line (orange + triangle)
- Mouse: click-to-tune (snapped to step size), scroll-wheel tunes by step size
- Native waterfall tiles (PCC 0x8004) suppress FFT-derived waterfall rows

### FrequencyDial

- 9 digits: `XXX.XXX.XXX` (MHz.kHz.Hz)
- Click top/bottom half of digit to tune up/down by that place value
- Scroll wheel over digit tunes that specific digit
- Scroll wheel elsewhere tunes by step size
- Double-click for direct text entry
- Range: 0.001–54.0 MHz

### SMeterWidget (ANLG applet)

- Analog gauge: 180° arc, S0 (left) to S9+60 (right)
- S0–S9 white markings (6 dB per S-unit), S9+10/+20/+40/+60 red markings
- Needle with shadow, center dot, exponential smoothing (α=0.3)
- Peak hold marker (orange triangle) with decay (0.5 dB/50ms) and 10s hard reset
- Text readouts: S-units (top-left, cyan), source label (top-center), dBm (top-right)
- Scale mapping: S0–S9 occupies left 60% of arc, S9–S9+60 occupies right 40%
- Toggled by ANLG button (visible by default)

### RxApplet Controls

Header row → step stepper → filter presets → AGC mode+threshold →
AF gain + audio pan → squelch → NB/NR/ANF DSP toggles → RIT → XIT.

Step sizes: 10, 50, 100, 250, 500, 1000, 2500, 5000, 10000 Hz.

---

## Key Implementation Patterns

### Settings Persistence (AppSettings — NOT QSettings)

**IMPORTANT:** Do NOT use `QSettings` anywhere in AetherSDR. All client-side
settings are stored via `AppSettings` (`src/core/AppSettings.h`), which writes
an XML file at `~/.config/AetherSDR/AetherSDR.settings`. Key names use
PascalCase (e.g. `LastConnectedRadioSerial`, `DisplayFftAverage`). Boolean
values are stored as `"True"` / `"False"` strings.

```cpp
auto& s = AppSettings::instance();
s.setValue("MyFeatureEnabled", "True");
bool on = s.value("MyFeatureEnabled", "False").toString() == "True";
```

The only place `QSettings` appears is in `AppSettings.cpp` for one-time
migration from the old INI format.

### Radio-Authoritative Settings Policy

**The radio is always authoritative for any setting it stores.** AetherSDR
must never save, recall, or override radio-side settings from client-side
persistence. Only save client-side settings for things the radio does NOT
save.

**Radio-authoritative (do NOT persist client-side):**
- Frequency, mode, filter width (restored via GUIClientID session)
- Step size and step list (per-slice, per-mode — sent via `step=` / `step_list=` in slice status)
- AGC mode/threshold, squelch, DSP flags (NR, NB, ANF, etc.)
- Antenna assignments (RX/TX antenna per slice)
- TX power, tune power, mic settings
- Panadapter count and slice assignments (radio restores from GUIClientID)
- Any setting that appears in a `slice`, `transmit`, or `display pan` status message

**Client-authoritative (persist in AppSettings):**
- Panadapter layout arrangement (how pans are arranged on screen — 2v, 2h, etc.)
- Client-side DSP (NR2, RN2 — not known to radio)
- UI preferences (window geometry, applet visibility, UI scale)
- Display preferences (FFT fill color/alpha, waterfall color scheme)
- CWX panel visibility, keyboard shortcuts enabled
- Band stack display settings (dBm scale — per-pan display preference, not
  radio state). NOTE: bandwidth/center are radio-authoritative — do NOT
  persist or restore them (see #291).
- DX spot settings (colors, font size, opacity)

**Why:** When both the radio and client persist the same setting, they fight
on reconnect. The radio restores its value via GUIClientID, then the client
overwrites it with a stale saved value. This caused bugs with step size (#274),
filter offsets, and panadapter layout (#269). The radio's session restore is
always more current than our saved state.

### GUI↔Radio Sync (No Feedback Loops)

- `SliceModel` setters emit `commandReady(cmd)` → `RadioModel` sends to radio
- Radio status pushes update `SliceModel` via `applyStatus(kvs)`
- `MainWindow` uses `m_updatingFromModel` guard to prevent echoing model updates
  back to the radio
- `RxApplet` uses `QSignalBlocker` extensively when updating UI from model state

### Auto-Reconnect

`RadioModel` has a 3-second `m_reconnectTimer` for unexpected disconnects.
Disabled by `m_intentionalDisconnect` flag on user-initiated disconnect.

### SmartConnect

If the radio already has slices (`slice list` returns IDs), `RadioModel` fetches
them with `slice get <id>` rather than creating new ones.

### Optimistic Updates Policy

Some radio commands do not produce a corresponding status update from the radio
(e.g. `tnf remove`, `tnf set permanent=`). In these cases we update the local
model optimistically — applying the change immediately without waiting for
confirmation from the radio.

**Every time an optimistic update is added, file a GitHub issue** recommending
that FlexRadio add proper status feedback for that command. The radio should
always echo state changes via status messages so all connected clients stay in
sync. Optimistic updates are fragile — they break Multi-Flex (other clients
don't see the change) and can drift out of sync if the command silently fails.

Tag these issues with `protocol` and `upstream`. Include the exact command that
lacks status feedback and the expected status message format.

---

## Known Bugs

- **Tuner applet SWR capture**: The final SWR displayed after a TGXL autotune
  cycle is inaccurate. During tuning the SWR meter streams via VITA-49 UDP while
  relay status arrives via TCP — there is a race between tuning=0 (TCP) and the
  final settled SWR meter reading (UDP). Current approach tracks the minimum SWR
  seen during tuning, but this captures mid-search transients (~1.5x) rather than
  the actual settled result (~1.01–1.15). Needs investigation with timestamped
  meter logging to understand the exact arrival order of SWR values relative to
  the tuning=0 status change. See `TunerApplet::updateMeters()` and the
  `tuningChanged` lambda in `TunerApplet::setTunerModel()`.

---

## Multi-Panadapter Support (#152)

### Architecture

- **PanadapterModel** — per-pan state (center, bandwidth, dBm, antenna, WNB)
- **PanadapterStream** — routes VITA-49 FFT/waterfall by stream ID
- **PanadapterStack** — nested QSplitter hosting N PanadapterApplets
- **PanLayoutDialog** — visual layout picker (6 layouts for dual-SCU, 3 for single)
- **wirePanadapter()** — per-pan signal wiring (display controls, overlays, click-to-tune)
- **spectrumForSlice()** — routes slice overlays/VFOs to correct SpectrumWidget

### What Works
- All 6 layout options: single, 2v, 2h, 2h1 (A|B/C), 12h (A/B|C), 2x2 (A|B/C|D)
- Per-pan native waterfall tiles correctly routed by wfStreamId
- Per-pan FFT correctly routed by panStreamId
- Per-pan dBm scaling
- Per-pan display controls (AVG, FPS, fill, gain, black level, line duration, etc.)
- Per-pan xpixels/ypixels pushed on creation
- Click-to-tune via `slice m <freq> pan=<panId>` (SmartSDR protocol)
- Independent tuning per pan
- Client-side auto-black per-pan
- Layout picker persists choice in AppSettings

### VFO Frequency Sync — RESOLVED

The VFO widget was showing 14.1 MHz (default) on every launch instead of the
last-used frequency. We investigated 5 different approaches (deferred timers,
one-shot signals, direct label updates) before discovering the actual root cause:

**Root cause:** We never saved the `GUIClientID` UUID returned by the
`client gui` response. Every connect sent `client gui ` with an empty UUID,
so the radio treated us as a brand new client and gave us a fresh slice at
the default frequency. SmartSDR saves and reuses the UUID, so the radio
recognizes it and restores the previous session's slice state.

**Fix:** One line — `s.setValue("GUIClientID", body.trimmed())` in the
`client gui` response handler. The radio now restores freq, mode, filter,
antenna, and all slice settings automatically.

**Lesson:** The VFO widget code was never broken. The signal connections,
`syncFromSlice()`, `updateFreqLabel()` — all worked correctly. The data
they were reading was wrong because the radio was giving us a blank slate.

### SmartControl Research Results

We investigated `client gui <uuid>` as a way to mirror another client's session
(SmartConnect/SmartControl). Findings:

- **`client gui <other_client_uuid>`** — KICKS the other client off
  (`duplicate_client_id=1`). This is for SESSION PERSISTENCE, not binding.
- **`client bind client_id=<uuid>`** — for NON-GUI clients (Maestro hardware
  buttons) binding to a GUI client. Does NOT duplicate waterfall/spectrum.
- **SmartConnect** is a Maestro hardware feature — physical buttons control a
  GUI client's slice. NOT a GUI-to-GUI mirroring feature.
- **The FlexRadio protocol has NO mechanism to link two GUI clients together.**
  Multi-Flex = independent operation, period.
- Discovery packet contains `gui_client_programs`, `gui_client_stations`,
  `gui_client_handles` but NOT `gui_client_ids` (UUIDs).
- UUIDs are only available via `sub client all` → `client <handle> connected
  client_id=<uuid>` status messages.

See issue #146 (closed) for full analysis.

### Protocol Findings (from SmartSDR pcap capture)

**Click-to-tune:** SmartSDR uses `slice m <freq> pan=<panId>` — NOT
`slice tune <freq>`. The `pan=` parameter tells the radio which panadapter
the click occurred in. The radio routes the tune to the correct slice.
SmartSDR **never sends `slice set <id> active=1`** — active slice is managed
entirely client-side.

**Per-pan dimensions:** SmartSDR sends `display pan set <panId> xpixels=<W>
ypixels=<H>` immediately after creating each pan, using actual widget
dimensions. Without this, the radio defaults to xpixels=50 ypixels=20 and
FFT data is essentially empty. SmartSDR also resizes ypixels on both pans
when the layout changes.

**Keepalive:** SmartSDR sends `keepalive enable` on connect, then
`ping ms_timestamp=<ms>` every 1 second. The radio responds with `R<seq>|0|`.

**Display settings are per-pan:** Each pan has independent fps, average,
weighted_average, color_gain, black_level, line_duration. Commands use the
specific pan's ID: `display pan set 0x40000001 fps=25`. Waterfall settings
use the waterfall ID: `display panafall set 0x42000001 black_level=15`.

**Stream IDs:** FFT bins arrive with stream ID = pan ID (0x40xxxxxx, PCC
0x8003). Waterfall tiles arrive with stream ID = waterfall ID (0x42xxxxxx,
PCC 0x8004). These are DIFFERENT IDs — route waterfall by `wfStreamId()`,
FFT by `panStreamId()`.

### Protocol Notes for +PAN

- `panadapter create` — returns `0x50000015` (use `display panafall create` instead)
- `display panafall create x=100 y=100` — works, creates new pan + waterfall
- `display panafall create` (v2+ syntax) — returns `0x50000016` on fw v1.4.0.0
  ONLY when no pans exist. Works fine for creating ADDITIONAL pans.
- `radio slices=N panadapters=N` — these count DOWN (available slots, not in-use)
- FLEX-8600 (dual SCU): max 4 pans, max 4 slices

### Multi-Pan Implementation Pitfalls (lessons learned)

1. **`QString::toUInt("0x40000001", 16)` returns 0.** Qt's `toUInt` with
   explicit base 16 does NOT handle the `0x` prefix. Use base 0 (auto-detect).
   This silently broke all stream ID comparisons.

2. **`handlePanadapterStatus()` must dispatch by panId.** The `display pan`
   status object name contains the pan ID — pass it through. Never apply pan
   status to `activePanadapter()` unconditionally.

3. **Waterfall ID arrives AFTER pan creation.** The `display pan` status
   message contains `waterfall=0x42xxxxxx` but it arrives after the
   PanadapterModel is created. Connect `waterfallIdChanged` to
   `updateStreamFilters()` to register the wf stream when it arrives.

4. **Don't force-associate waterfalls to pans.** The radio's `display pan`
   status correctly sets `waterfallId` via `applyPanStatus`. Manual
   association logic assigns to the wrong pan (first-empty-slot race).

5. **Display overlay connections must be per-pan.** Wire each
   PanadapterApplet's overlay menu in `wirePanadapter()`, not globally in
   the constructor. Each overlay sends commands with its own panId/waterfallId.

6. **Push `xpixels`/`ypixels` to each new pan on creation.** The radio
   defaults to `xpixels=50 ypixels=20` which produces empty FFT bins.
   Send actual widget dimensions immediately after `panadapterAdded`.

7. **Never send `slice set <id> active=1`.** Active slice is managed
   entirely client-side. The radio bounces `active` between slices when
   two share a pan, creating infinite feedback loops. See pitfall #16.

8. **Use `slice m <freq> pan=<panId>` for cross-pan click-to-tune only.**
   For same-pan tuning (scroll wheel, click on active slice's pan), use
   `onFrequencyChanged()` → `slice tune <sliceId>`. See pitfall #18.
   `slice m` does NOT recenter the pan when crossing band boundaries.

9. **Band changes need `slice tune` + `slice m`.** `slice tune <id> <freq>`
   recenters the pan's FFT/waterfall on the new band. `slice m <freq>
   pan=<panId>` updates the VFO frequency. Both are needed for a complete
   cross-band change in multi-pan mode. In single-pan mode, use
   `onFrequencyChanged()` which handles everything.

10. **Band change handler must target the pan's slice, not `activeSlice()`.**
    Use `sl->panId() == applet->panId()` to find the correct slice for each
    pan's overlay. Falling back to `activeSlice()` causes all band changes
    to affect slice A.

11. **Band stack save must validate frequency vs band.** In multi-pan mode,
    the save handler's `activeSlice()` may return a slice on a different band.
    Use `BandSettings::bandForFrequency()` to verify the frequency belongs to
    the band before saving. Skip the save if they don't match (prevents
    cross-band contamination).

12. **Disconnect dying pan widgets before removal.** When a pan is removed
    (layout reduction), disconnect all signals from its SpectrumWidget and
    OverlayMenu to MainWindow BEFORE the widget is destroyed. This prevents
    all `wirePanadapter` lambdas from calling into dead objects. One-shot
    global fix — covers all current and future lambdas.

13. **Preamp (`pre=`) is shared antenna hardware.** When any `display pan`
    status contains `pre=`, apply it to ALL pans sharing the same antenna,
    not just the pan the status belongs to. Multi-Flex filtering must not
    block preamp updates — they are SCU-level state, not per-client.

14. **Filter polarity normalization.** The radio sometimes sends wrong-polarity
    filter offsets after session restore (e.g. `filter_lo=-2700 filter_hi=0`
    for DIGU). Normalize in `applyStatus()` based on mode: USB/DIGU/FDV must
    have `filterLo >= 0`, LSB/DIGL must have `filterHi <= 0`.

15. **`FWDPWR` meter source is `TX-` (with trailing dash), not `TX`.**
    Use `startsWith("TX")` for matching, not exact equality.

16. **Never send `slice set <id> active=1` — not even in single-pan mode.**
    The radio bounces `active` between slices when two share a pan, creating
    an infinite feedback loop (slice 0 active → we send active=1 for 0 →
    radio sets slice 1 active → we react → loop). SmartSDR manages active
    entirely client-side. The radio sets `active=` as a side-effect of
    `slice m` commands. Removed `s->setActive(true)` from `setActiveSlice()`
    entirely.

17. **`activePanChanged` must sync ALL slice-dependent UI.** In multi-pan mode,
    `setActiveSlice()` does NOT fire on pan click (pitfall #7). So step size,
    CW decode, applet panel controls, and overlay menu slice binding must all
    be synced in the `activePanChanged` handler. Use `setActivePanApplet()`
    to rewire CW decoder connections.

18. **Click-to-tune must not switch slices within the same pan.** The
    `frequencyClicked` handler should only switch active slice when clicking
    on a DIFFERENT pan. When multiple slices share a pan, always tune the
    current active slice via `onFrequencyChanged()` → `slice tune`. Switching
    to the other slice on each scroll event causes both VFOs to move.

19. **+RX must target the button's own pan, not `m_activePanId`.** The
    `addRxClicked` signal must carry the panId from `SpectrumOverlayMenu`.
    Use `RadioModel::addSliceOnPan(panId)` with explicit panId in the
    `slice create pan=<panId>` command.

20. **`setActivePanApplet()` rewires CW decoder.** When the active pan
    changes, disconnect `textDecoded`/`statsUpdated`/pitch/speed signals
    from the old applet and reconnect to the new one. The CW decoder is
    a singleton — its output must follow the active pan.

---

## Multi-Client (Multi-Flex) Support

When another client (SmartSDR, Maestro) is already connected to the radio,
AetherSDR must operate as an independent Multi-Flex client. Key implementation
details:

### Problem
The radio broadcasts ALL status messages to ALL connected clients via `sub xxx all`
subscriptions. Without filtering, AetherSDR would:
1. Process FFT/waterfall VITA-49 packets from the other client's panadapter
   (different dBm scaling → all-red waterfall)
2. Apply `display pan` status updates from the other client's panadapter
   (zoom/scale changes replicated across clients)
3. Track and control the other client's slices (tuning in sync)

### Solution — Three-layer filtering by `client_handle`

Each slice, panadapter, and waterfall carries a `client_handle` field that
identifies which client owns it. However, `client_handle` is NOT present in
every status update — it only appears in certain "full status" messages.

**Layer 1 — Slice ownership (`handleSliceStatus`)**:
- When `client_handle` appears, record the slice ID in `m_ownedSliceIds`
- Reject slices owned by other clients; remove any SliceModel we already created
- For subsequent updates without `client_handle`, check against `m_ownedSliceIds`

**Layer 2 — Panadapter/waterfall status (`onStatusReceived`)**:
- Only claim `display pan` / `display waterfall` objects matching our `client_handle`
- Ignore status updates for other clients' panadapters

**Layer 3 — VITA-49 UDP packet filtering (`PanadapterStream`)**:
- `setOwnedStreamIds(panId, wfId)` sets accepted stream IDs
- FFT packets (PCC 0x8003) and waterfall packets (PCC 0x8004) with non-matching
  stream IDs are silently dropped

### Timing Issue
Early slice status messages arrive WITHOUT `client_handle`. AetherSDR creates
SliceModels for all slices initially, then removes the other client's when
`client_handle` is received. `MainWindow::onSliceRemoved()` re-wires the GUI
to the remaining owned slice.

### Slice Creation
When `slice list` returns IDs but none belong to us (`m_slices.isEmpty()` after
filtering), we call `createDefaultSlice()` to create our own independent slice
and panadapter. The radio assigns these to our `client_handle`.

---

## Known Quirks / Lessons Learned

- `QMap<K,V>` needs `#include <QMap>` in headers — forward-declaration in
  `qcontainerfwd.h` leaves the field as incomplete type
- `static constexpr` class members are private by default; file-scope
  `constexpr` copies needed for free functions in the same .cpp
- Qt6: `Qt::AA_UseHighDpiPixmaps` removed — do not use
- Qt6: `QMenu::addAction(text, obj, slot, shortcut)` deprecated — use
  `addAction(text)` + `setShortcut()` + `connect()` separately
- Filter width presets are mode-aware: LSB/DIGL/CWL use negative low offset,
  CW centers 200 Hz above, others use 0 to high

---

## What's Implemented (v0.6.0)

- UDP radio discovery and TCP command/control
- SmartSDR V/H/R/S/M protocol parsing
- Panadapter VITA-49 FFT spectrum display with dBm calibration
- Native VITA-49 waterfall tiles (PCC 0x8004) with colour mapping
- VITA-49 meter data decode (PCC 0x8002) with unit-aware conversion
- MeterModel: meter definition registry from TCP `#`-separated status messages
- Analog S-Meter gauge (ANLG applet): needle, peak hold, S-unit + dBm readout
- Audio RX (float32 stereo + int16 mono) via VITA-49 → QAudioSink
- Volume / mute control with RMS level meter
- Full RX applet: antennas, filter presets, AGC, AF gain, pan, squelch,
  NB/NR/ANF, RIT/XIT, tuning step stepper, tune lock
- Frequency dial: click, scroll, keyboard, direct entry
- Spectrum: click-to-tune, scroll-to-tune, filter passband overlay
- Multi-slice support: color-coded markers, independent TX assignment,
  clickable slice badges, +RX button, close/lock buttons, off-screen indicators
- ARRL band plan overlay on FFT (color-coded CW/DATA/PHONE with license classes)
- Spot frequency markers with hover tooltips
- AppletPanel: toggle-button row (ANLG, RX, TUNE, TX, PHNE, P/CW, EQ)
- Tuner applet (4o3a TGXL): Fwd Power/SWR gauges, C1/L/C2 relay bars,
  TUNE (autotune) and OPERATE/BYPASS/STANDBY buttons
- Tuner auto-detect: hidden when no TGXL, appears on amplifier subscription
- Fwd Power gauge 3-tier auto-scale: barefoot (0–120 W), Aurora (0–600 W), PGXL (0–2000 W)
- TX applet: Fwd Power/SWR gauges, RF Power/Tune Power sliders,
  TX profile dropdown, TUNE/MOX/ATU/MEM buttons, ATU status indicators, APD
- TransmitModel: transmit state, internal ATU state, TX profile management
- P/CW applet: mic level gauge (-40 to +10 dB, 3-zone cyan/yellow/red) with
  peak-hold marker, compression gauge (reversed red fill with slow decay)
- P/CW applet: mic profile dropdown, mic source selector, mic level slider,
  +ACC toggle, PROC/NOR-DX-DX+, DAX toggle, MON + monitor volume
- PHONE applet: AM Carrier slider, VOX toggle + level, VOX delay, DEXP toggle +
  level (non-functional on fw v1.4.0.0), TX filter Low/High Cut step buttons
- EQ applet: 8-band graphic equalizer (63 Hz – 8 kHz), ±10 dB vertical sliders,
  independent RX/TX views, ON toggle, reset button (revert all bands to 0 dB)
- EqualizerModel: TX/RX EQ state, parses `eq txsc`/`eq rxsc` status, emits commands
- HGauge shared header: reusable horizontal gauge widget with three-zone fill,
  peak-hold markers, and reversed fill mode
- **NR2 spectral noise reduction**: Ephraim-Malah MMSE-LSA with OSMS floor tracking,
  FFTW3 optimized with background wisdom generation + radix-2 fallback
- **RN2 neural noise suppression**: Mozilla/Xiph RNNoise bundled, AVX2/SSE4.1/generic
  runtime dispatch, r8brain-free-src polyphase resampling (24k↔48k)
- **CW decoder**: real-time Morse decode via ggmorse, auto pitch/speed detection,
  confidence-colored text (green/yellow/orange/red), auto-show in CW mode
- **RADE digital voice**: FreeDV Radio Autoencoder (bundled with auto-downloaded Opus),
  client-side neural encoder/decoder, DIGU/DIGL passthrough, sync/SNR signals
- **r8brain-free-src resampling**: professional polyphase resampler replacing all
  hand-rolled sample rate conversion (AudioEngine, RNNoiseFilter, RADEEngine)
- PC audio toggle button (radio line out vs PC speakers)
- Audio TX: PC mic → Opus stereo 10ms → VITA-49 on remote_audio_tx (voice),
  DAX TX → uncompressed float32 on dax_tx (digital modes).
  Client-side PC mic gain (0-100%), client-side mic level metering with
  VU ballistics, VOX support via met_in_rx=1
- 48kHz audio fallback for devices that don't support 24kHz
- TNF management: add/drag/right-click/width/depth, permanent vs temporary
- CAT control: 4-channel rigctld TCP + PTY virtual serial ports
- FM duplex: CTCSS, repeater offset, REV toggle
- XVTR transverter band support with context-aware frequency entry
- **SmartLink remote operation**: Auth0 login, TLS command channel, VITA-49 UDP
  streaming (FFT, waterfall, audio, meters) via `client udp_register` protocol,
  5-second UDP ping keepalive for NAT pinhole maintenance
- Multi-Flex support (independent operation alongside SmartSDR/Maestro)
- Firmware upload from Linux (.ssdr files)
- Radio setup dialog (9 tabs): Radio, Network, GPS, Audio, TX, Phone/CW, RX, Filters, XVTR
- VFO widget: mode-aware passband positioning (flips side for LSB/DIGL/CWL)
- **Linux DAX virtual audio**: PulseAudio pipe modules (module-pipe-source/sink),
  4 RX + 1 TX virtual devices visible in PulseAudio/PipeWire, MeterSlider
  combo widget (level meter + gain slider), per-channel gain, persistent settings
- **DAX TX audio routing**: mode-aware gating (mic vs DAX vs RADE), autostart
- **Volume boost**: AF gain slider extends to 200% (+6 dB) with software gain
- **Client-side waterfall auto-black**: measures noise floor from tiles, replaces
  radio's auto_black which targeted SmartSDR's different rendering engine
- **Memory name editing**: double-click Name column in memory dialog
- **3-tier TX power meters**: auto-detect barefoot/Aurora/PGXL from max_power_level,
  scales TxApplet gauge, TunerApplet gauge, and SMeterWidget Power arc (#116)
- **Windows Inno Setup installer**: proper setup.exe with Start Menu, desktop icon,
  uninstaller — alongside portable ZIP
- **Serial port PTT/CW keying**: USB-serial adapter DTR/RTS output for PTT and
  CW key, CTS/DSR input polling for foot switch and paddle, paddle swap, auto-open
- **CW auto-tune**: Once and Loop buttons in VFO widget CW tab, sends
  `slice auto_tune` for radio-side signal detection
- **CW passband fix**: filter centered on carrier, radio BFO handles pitch offset
- **CW decode overlay toggle**: on/off in Radio Setup → Phone/CW
- **Opus compressed audio**: SmartLink WAN audio compression with Auto/None/Opus
  toggle in Radio Setup → Audio, hot-swap without reconnect
- **Per-module diagnostic logging**: Help → Support dialog with 15 toggleable
  categories, log viewer, Send to Support button with auto-collected bundle
- **RADE per-slice isolation**: RADE mode only affects the activated slice,
  other slices' audio plays normally (PR #131)
- **Network quality fix**: packet sequence tracking keyed by stream ID not PCC
- **Memory editing**: all columns editable via double-click
- **Audio device persistence**: output/input device selection saved across restarts
- TX button (sends `xmit 1` / `xmit 0`)
- Persistent window geometry and display settings
- **Multi-Flex indicator**: green "multiFLEX" badge in title bar when other
  clients are connected, with hover tooltip listing station names (#185)
- **Multi-Flex client tracking**: `sub client all` subscription for real-time
  connect/disconnect detection (#185)
- **FDX (Full Duplex) toggle**: status bar indicator with optimistic update
  (radio accepts but doesn't echo status) (#188)
- **TNF global toggle fix**: TNF indicator now sends command to radio (#184)
- **Crash on exit fix**: QPointer for VfoWidget close/lock buttons prevents
  double-free during Qt widget tree teardown; proper MainWindow destructor
  stops NR2/RN2/RADE before destruction (#167)
- **Client-side DSP persistence**: NR2/RN2 state saved on exit and restored
  on next launch (#167)
- **Volume slider sync**: fixed `audio_level` status key (was `audio_gain`),
  sliders now track Maestro/SmartControl and profile changes (#161)
- **DAX channel persistence**: saved/restored across restarts (#180)
- **Profile manager UX**: selecting a profile populates the name field for
  easy re-save (#177)
- **SQL disable in digital modes**: button dimmed with distinct disabled style
  in DIGU/DIGL, squelch saved/restored on mode switch (#192)
- **Configurable quick-mode buttons**: right-click to assign any mode, SSB
  toggles USB↔LSB, DIG toggles DIGU↔DIGL (#191)
- **RTTY mark/space lines**: dashed M/S frequency lines on panadapter in
  RTTY mode, real-time tracking (#189)
- **RIT/XIT offset lines**: dashed RIT (slice color) and XIT (red) lines on
  panadapter showing actual RX/TX frequencies (#199)
- **Band plan overlay toggle**: View menu checkbox to show/hide ARRL band
  plan overlay (#193)
- **UI scaling**: View → UI Scale (75%–200%) via QT_SCALE_FACTOR (#194)
- **PA temp/voltage precision**: XX.X°C and XX.XX V in status bar (#195)
- **Show TX in Waterfall**: waterfall freezes during TX when disabled,
  multi-pan aware (only TX pan freezes) (#207)
- **Network MTU setting**: Radio Setup → Network → Advanced (#202)
- **RTTY mark default from radio**: reads radio's value on connect instead
  of hardcoding 2125 (#200)
- **Station name**: configurable in Radio Setup → Radio tab (#182)
- **VFO slider value labels**: AF gain, SQL, AGC-T show numeric values (#198)
- **Fill slider label fix**: display panel Fill slider updates label (#206)
- **XVTR panel**: 2×4 grid with auto-grow for configured bands (#204)
- **macOS mic permission**: proper AVAuthorizationStatus check with diagnostic
  logging (#157)
- **NR2/RN2 button sync**: overlay ↔ VFO ↔ RX applet all stay in sync
- **NR2 freeze fix**: spectrum overlay NR2 toggle now uses wisdom-gated
  background thread (was freezing UI on first enable) (#214)
- **NR2/RN2 persistence**: client-side DSP state saved on exit, restored
  on launch (#167)
- **VFO TX badge toggle**: click to assign OR unassign TX (#213)
- **TGXL OPERATE disables TUNE/ATU/MEM**: TX applet buttons dimmed when
  external tuner is in OPERATE mode, re-enabled in BYPASS/STANDBY (#197)
- **Reconnect dialog**: "Radio disconnected — Waiting for reconnect" on
  unexpected disconnect with Disconnect button, auto-close on reconnect (#209)
- **Fast disconnect detection**: discovery stale timeout reduced to 5s,
  force-disconnect on radio loss instead of waiting for TCP timeout (#209)
- **Repeating reconnect timer**: retries TCP connect every 5s instead of
  single-shot, alongside discovery-based auto-connect (#209)
- **Heartbeat indicator**: title bar circle flashes green on each TCP ping
  response, blinks red/grey after 3 missed beats
- **Keepalive ping**: sends `keepalive enable` + 1s ping timer (matches
  FlexLib), drives heartbeat for local/routed/SmartLink connections
- **Low Bandwidth Connect**: connection panel checkbox sends
  `client low_bw_connect` to reduce FFT/waterfall data for VPN/LTE links
- **PA inhibit during TUNE**: opt-in safety feature disables ACC TX output
  before tune, restores after completion, protects external amplifiers (#156)
- **Exciter power fix with PGXL**: FWDPWR meter now filtered by source "TX"
  so amplifier meter doesn't overwrite exciter reading (#181)
- **Step size persistence**: tuning step saved/restored across restarts (#211)
- **VFO slider value labels**: AF gain, SQL, AGC-T show numeric values (#198)
- **DVK (Digital Voice Keyer)**: 12-slot recording/playback panel with F1-F12
  hotkeys, REC/STOP/PLAY/PREV buttons, elapsed timer with progress bars,
  right-click context menu (rename, clear, delete, import/export WAV),
  inline name editing, WAV upload/download via TCP (#19)
- **DVK mode-aware availability**: DVK enabled only in voice modes (USB/LSB/
  AM/SAM/FM/NFM/DFM), CWX enabled only in CW/CWL. Auto-close on mode switch.
  Mutual exclusion between CWX and DVK panels.
- **FFT/waterfall horizontal alignment fix**: removed hardcoded xpixels=1024
  that overwrote correct widget dimensions, fixing signal position mismatch
  between FFT spectrum and native waterfall tiles (#279)
- **FFT dBm calibration**: bin conversion now uses actual ypixels from radio
  status (bins are pixel Y positions, not 0-65535 range). Tracks y_pixels
  from radio status updates for correct dBm scaling.
- **Title bar speaker mute fix**: mute button now controls local PC audio
  engine in addition to radio line out (#259)
- **VFO mute indicator**: speaker icon on VFO tab bar toggles 🔊/🔇 to
  reflect mute state. Right-click speaker tab to toggle mute directly (#283)
- **4-pane splitter fix**: CWX+DVK+PanStack+AppletPanel layout corrected —
  applet panel was invisible due to wrong stretch/size indices (#281)
- **VOX support**: creates `remote_audio_tx` stream on connect, streams mic
  audio to radio during RX for VOX detection (`met_in_rx=1`). Separate
  accumulator keeps VOX path independent from DAX TX (#253)
- **Profile load xpixels fix**: detect when radio resets x/y_pixels to
  defaults during profile load and auto re-push correct widget dimensions (#289)
- **Band stack radio-authoritative fix**: removed bandwidth and center from
  band stack save/restore — both are radio-authoritative per FlexLib API.
  Fixes FFT/waterfall misalignment on band change (#291)
- **ESC (Enhanced Signal Clarity)**: diversity beamforming controls in VFO
  audio tab. Polar display (phase=angle, gain=radius), horizontal phase
  slider (0–360° in 5° steps, radians to radio), vertical gain slider
  (0.0–2.0), ESC toggle button. Real-time ESC signal strength meter bar
  from radio SLC/ESC meter. Protocol matches SmartSDR pcap: `esc=on/off`,
  phase in radians, DiversityChild guard per FlexLib. ESC panel visible
  only on diversity parent slice, hidden on child. Requires DIV_ESC
  license (SmartSDR+). (#20, #38)
- **Band change panadapter scroll fix**: band changes now use
  `tuneAndRecenter()` instead of `onFrequencyChanged()` which sent
  `autopan=0`, preventing the radio from scrolling to the new band
- **NVIDIA NIM BNR (Background Noise Removal)**: GPU-accelerated neural
  audio denoising via self-hosted Docker container. gRPC bidirectional
  streaming (48kHz mono float32, 10ms chunks). Async worker thread for
  all gRPC I/O, jitter buffer (50ms priming) for smooth playback.
  Intensity slider (0–100%) in overlay DSP panel. BNR button in VFO DSP
  tab and overlay menu, 3-way mutual exclusion with NR2/RN2.
  Radio Setup → Audio: container management panel (autostart, start/stop,
  status indicator). Requires NVIDIA RTX 4000+ GPU + Docker. (#288)
- **Band zoom buttons**: S (segment_zoom) and B (band_zoom) at bottom-left
  of waterfall, matching SmartSDR protocol from pcap capture
- **XVTR crash fix**: QPointer<PanadapterApplet> prevents SEGV when
  panadapter destroyed during XVTR band switch
- **CW decoder fix**: initial m_panApplet wasn't wired through
  setActivePanApplet(), CW decode output went nowhere on first pan
- **SpotHub**: unified spot management dialog (Settings → SpotHub) with 6 tabs:
  DX Cluster (telnet), RBN (telnet), WSJT-X (UDP multicast decode), POTA
  (HTTP polling api.pota.app), Spot List (sortable table with band filters),
  Display (spot rendering settings). All spot sources run on a dedicated
  worker thread with 1/sec batched forwarding. Features: spot density badges
  with click-to-expand popup, per-source color coding, deduplication,
  configurable lifetimes, SNR-based alpha for WSJT-X, WSJT-X decode filters
  (CQ/CQ POTA/Calling Me), log file history per source.
- **FreeDV Reporter spots**: real-time spot source via Socket.IO v4
  WebSocket to qso.freedv.org. View-only auth, station state tracking,
  spots from freq_change and rx_report events. QWebSocket with manual
  Engine.IO/Socket.IO framing (no external dependencies). (#349)
- **USB Cable management**: Radio Setup → USB Cables tab for configuring
  USB-serial adapters plugged into the radio's rear USB ports. Supports
  CAT, BCD, Bit (8-row grid), and Passthrough cable types. UsbCableModel
  tracks cables via status messages, QStackedWidget property panels. (#40)
- **FlexControl USB tuning knob**: auto-detect VID 0x2192 / PID 0x0010,
  9600 8N1 serial protocol with D/U rotation (acceleration 02–06) and
  3 configurable buttons (tap/double/hold). Radio Setup → Serial tab
  config. Gated by HAVE_SERIALPORT. (#25)
- **MIDI controller mapping**: class-compliant USB MIDI controller support
  via RtMidi. 50+ mappable parameters (RX, TX, Phone/CW, EQ, Global).
  MIDI Learn mode (select param, move knob, binding created). Dedicated
  Settings → MIDI Mapping dialog with device selector, binding table,
  activity indicator, and named profiles. Bindings stored in dedicated
  ~/.config/AetherSDR/midi.settings XML file. Soft dependency (HAVE_MIDI).
  (#355)
- **Panadapter click-to-spot**: right-click context menu to add spots
  with callsign, comment, lifetime, and optional DX cluster forwarding.
  Right-click existing spots for tune, copy, QRZ lookup, remove. Spot
  frequency snaps to tuning step size. (#36)
- **Per-slice record/play**: Record (⏺) and Play (▶) buttons on VFO
  flag. Radio-managed recording with pulsing indicator. Play disabled
  until recording exists. TX playback via MOX for voice keyer. (#164)
- **DAX IQ streaming**: raw I/Q data from radio DDC to SDR apps (SDR#,
  GQRX, GNU Radio) via PulseAudio virtual capture devices. 4 channels
  at 24/48/96/192 kHz. DaxIqModel + dedicated worker thread for byte-swap
  and pipe I/O. DIGI applet controls + overlay pan routing. (#124)
- **Opus codec independent of RADE**: HAVE_OPUS separated from HAVE_RADE
  so SmartLink compressed audio works without RADE. System libopus
  fallback via pkg-config. Windows setup-opus.ps1 script. (#375)
- **Applet panel collapse**: ☰ hamburger in status bar toggles panel
  visibility. Custom painted +PAN spectrum icon. Persisted. (#178)
- **Drag-reorderable applets**: drag ⋮⋮ grip title bars to reorder
  applets in the panel. QDrag framework with persistent order. View →
  Reset Applet Order. (#335)
- **Modeless dialogs**: SpotHub, Radio Setup, and MIDI Mapping dialogs
  no longer block the main window. Duplicate prevention via QPointer.
- **AppSettings atomic save**: write to .tmp, validate XML, rename.
  Backup recovery from .bak. Count guard prevents truncated saves.
  Key validation skips invalid XML element names.
- **BNR crash fix**: r8brain resampler buffer overflow — maxBlockSamples
  increased to 16384 for BNR's variable-size output. (#376)
- **RadioModel shutdown fix**: disconnect signals before member
  destruction to prevent use-after-free (found via ASAN).
- **Spot label deconfliction**: proper stacking across all levels
  before overflow to cluster badges.
- **GPG release signing**: sign-release.yml workflow signs Linux
  AppImage and source archives with detached .asc signatures.
  SHA256SUMS.txt included. macOS uses Apple codesign. (#397, #398)
- **Commit signing enforced**: branch protection requires GPG-signed
  commits. CONTRIBUTING.md documents setup. Ed25519 keys.
- **NR2/RN2/BNR DSP switch crash fix**: enabled flag now set AFTER
  object construction to prevent SEGV when audio arrives during
  mode switch (e.g. BNR→NR2).
- **FlexControl coalescing**: rapid encoder steps batched into single
  TCP command every 20ms, eliminating UI lag (#379)
- **FlexControl menu wired**: Settings → FlexControl opens Radio
  Setup Serial tab instead of "not implemented" (#380)
- **TNF crash fix**: QPointer guards dangling SpectrumWidget pointer
  in rebuildTnfMarkers lambda on pan removal. Duplicate tnf create
  commands fixed via disconnect-before-connect (#381)
- **ToggleMox/ToggleTune fix**: parse `mox=` from radio transmit
  status so isMox() returns correct state for FlexControl toggle (#382)
- **VFO lock icon sync**: connect lockedChanged in VfoWidget::setSlice
  so lock icon updates from any source (#384)
- **KDE/Cinnamon scroll fix**: accumulate angleDelta to normalize
  high-resolution scroll deltas across desktop environments (#390, #405)
- **ESC gain slider fix**: added vertical handle/groove QSS rules
  to kSliderStyle so thumb renders grey not black (#394)
- **Configurable band plan size**: View → Band Plan submenu with
  Off/Small(6pt)/Medium(10pt)/Large(12pt)/Huge(16pt) (#406)
- **Visual keyboard shortcut manager**: View → Configure Shortcuts dialog
  with painted ANSI keyboard layout, color-coded by category, ~45
  bindable actions, conflict detection, persistent bindings (#239)
- **Click-and-drag VFO tuning**: drag inside filter passband to tune,
  snapped to step size. Filter edge drag takes priority (#404)
- **Go to Frequency shortcut**: G key opens VFO direct entry field
- **Space PTT hold-to-transmit**: app-level event filter, works
  regardless of widget focus, proper TX state sync
- **RxApplet NR2 fix**: NR button 3-state cycle now actually enables
  NR2 instead of only syncing the visual state (#329)
- **Split pan fix**: TX slice created on same pan as RX slice in
  multi-pan mode, CW split offsets 1 kHz up (#328)

## What's NOT Yet Implemented

- RADE status indicator in VFO widget (sync/SNR display, #88)
- RADE on Windows (#87)
- DAX on Windows (virtual audio devices, #87)
- SmartLink NAT hole-punching (for radios without UPnP/port forwarding)
- SmartLink WAN auto-reconnect
- SmartLink jitter buffer for high-latency connections
- GPU-accelerated spectrum/waterfall via QRhi (#391)
