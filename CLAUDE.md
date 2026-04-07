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

### Autonomous Agent Boundaries

AI agents (including AetherClaude/pi-claude) may autonomously fix:
- **Bugs with clear root cause** — persistence missing, guard missing, crash fix
- **Protocol compliance** — matching SmartSDR behavior confirmed by pcap/FlexLib
- **Build/CI fixes** — missing dependencies, platform compat

AI agents must **NOT** autonomously change:
- **Visual design** — colors, fonts, layout, theme (user preferences ≠ project direction)
- **UX behavior** — how controls work, what clicks do, keyboard shortcuts
- **Architecture** — adding new threads, changing signal routing, new dependencies
- **Feature scope** — adding features beyond what the issue describes
- **Default values** — changing defaults that affect all users based on one report

When in doubt, the agent should implement the fix and note in the PR that
design decisions need maintainer review. The project maintainer (Jeremy/KK7GWY)
is the sole authority on visual design and UX direction.

## C++ Style Guide

- **No `goto`** — use early returns, break, or restructure the logic
- **No raw `new`/`delete`** — use `std::unique_ptr`, `std::make_unique`, or Qt parent ownership
- **No `#define` macros for constants** — use `constexpr` or `static constexpr`
- **Braces on all control flow** — even single-line `if`/`else`/`for`/`while`
- **`auto` sparingly** — use explicit types unless the type is obvious from context (e.g. `auto* ptr = new Foo` is fine, `auto x = foo()` is not)
- **Naming**: classes `PascalCase`, methods/variables `camelCase`, constants `kPascalCase`, member variables `m_camelCase`
- **Platform guards**: use `#ifdef Q_OS_WIN` / `Q_OS_MAC` / `Q_OS_LINUX`, not `_WIN32` or `__APPLE__`
- **Don't remove code you didn't add** — if rebasing, ensure upstream changes are preserved. Review the diff before submitting.
- **Atomic parameters for cross-thread DSP** — main thread writes via `std::atomic`, audio thread reads. Never hold a mutex in the audio callback for parameter updates.
- **Error handling**: log with `qCWarning(lcCategory)`, don't throw exceptions

## Build

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j$(nproc)
./build/AetherSDR
```

Dependencies (Arch): `qt6-base qt6-multimedia cmake ninja pkgconf autoconf automake libtool`

Current version: **0.8.5** (set in both `CMakeLists.txt` and `README.md`).

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

### Branch Protection

- All commits to `main` require GPG signatures
- CI build must pass before merge
- CODEOWNERS requires project owner review on all PRs
- Branches auto-delete after PR merge

---

## Architecture Overview

Key source directories: `src/core/` (protocol, audio, DSP), `src/models/`
(RadioModel, SliceModel, etc.), `src/gui/` (MainWindow, SpectrumWidget, applets).
Use `ls src/core/`, `ls src/models/`, `ls src/gui/` to explore.

**Key classes:**
- `RadioModel` — central state, owns connection + all sub-models
- `AudioEngine` — RX/TX audio, NR2/RN2/NR4/BNR DSP pipeline
- `SpectrumWidget` — GPU-accelerated FFT spectrum + waterfall (QRhiWidget)
- `MainWindow` — wires everything together, signal routing hub
- `PanadapterStream` — VITA-49 UDP parsing, routes FFT/waterfall/audio/meters

**Threading:** 11 threads — see `docs/architecture-pipelines.md` for full
thread diagram, data flow ASCII art, cross-thread signal map, and GPU rendering notes.

**Design principle:** RadioModel owns all sub-models on the main thread.
Worker threads communicate exclusively via auto-queued signals. Never hold
a mutex in the audio callback.

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
- Slice frequency is `RF_frequency` (not `freq`) in status messages
- Streams are discriminated by **PacketClassCode** (PCC), NOT by packet type
- `audio_level` is the status key for AF gain (not `audio_gain`)
- The radio **never sends `mox=` in transmit status messages**. Use
  `isTransmitting()` (interlock state machine), NOT `isMox()`
- Three separate tune command paths all need interlock inhibit:
  `transmit tune 1`, `tgxl autotune`, `atu start`
- `cw key immediate` not supported — use netcw UDP stream for CW keying
- `transmit set break_in=1` wrong — correct: `cw break_in 1`

### VITA-49 Packet Format

See `docs/vita49-format.md` for full packet format, PCC codes, FFT bin
conversion, waterfall tile format, audio payload, and meter data parsing.

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

### Radio-Authoritative Settings Policy

**The radio is always authoritative for any setting it stores.** AetherSDR
must never save, recall, or override radio-side settings from client-side
persistence. Only save client-side settings for things the radio does NOT save.

**Radio-authoritative (do NOT persist):** frequency, mode, filter, step size,
AGC, squelch, DSP flags, antennas, TX power, panadapter count/assignments.

**Client-authoritative (persist in AppSettings):** layout arrangement, client-side
DSP (NR2/RN2/NR4), UI preferences, display preferences, spot settings.

**Why:** When both persist the same setting, they fight on reconnect. The radio's
GUIClientID session restore is always more current than our saved state.

### GUI↔Radio Sync (No Feedback Loops)

- Model setters emit `commandReady(cmd)` → `RadioModel` sends to radio
- Radio status pushes update models via `applyStatus(kvs)`
- Use `m_updatingFromModel` guard or `QSignalBlocker` to prevent echo loops

### Auto-Reconnect

`RadioModel` has a 3-second `m_reconnectTimer` for unexpected disconnects.
Disabled by `m_intentionalDisconnect` flag on user-initiated disconnect.

### Optimistic Updates Policy

Some radio commands lack status echo (e.g. `tnf remove`). Update the local
model optimistically. **File a GitHub issue** tagged `protocol` + `upstream`
for each missing status echo — optimistic updates break Multi-Flex.

---

## Known Bugs

- **Tuner applet SWR capture**: race between tuning=0 (TCP) and final SWR
  meter (UDP). See `TunerApplet::updateMeters()`.

---

## Multi-Panadapter Support

**Architecture:** PanadapterModel (per-pan state), PanadapterStream (VITA-49
routing by stream ID), PanadapterStack (QSplitter), wirePanadapter() (per-pan
signal wiring), spectrumForSlice() (overlay routing).

**Key protocol facts:**
- Click-to-tune: `slice m <freq> pan=<panId>` — NOT `slice tune`
- Never send `slice set <id> active=1` — managed client-side only
- Push `xpixels`/`ypixels` on pan creation (radio defaults to 50×20)
- FFT stream ID = pan ID (0x40xx), waterfall stream ID = waterfall ID (0x42xx)

**Implementation pitfalls:** See `docs/multi-pan-pitfalls.md` for 20 numbered
lessons learned (Qt parsing, status dispatch, waterfall timing, feedback loops,
filter polarity, preamp sharing, band changes, etc.).

---

## Multi-Client (Multi-Flex) Support

Filter all status and VITA-49 packets by `client_handle` — three layers:
1. **Slice ownership**: track `m_ownedSliceIds` from `client_handle` field
2. **Panadapter status**: only claim `display pan`/`display waterfall` matching our handle
3. **VITA-49 UDP**: `setOwnedStreamIds(panId, wfId)` drops non-matching packets

Early status messages arrive WITHOUT `client_handle`. Create SliceModels for
all initially, remove other clients' when handle arrives.
