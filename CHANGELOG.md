# Changelog

All notable changes to AetherSDR are documented in this file.
Format follows [Keep a Changelog](https://keepachangelog.com/).

## [v0.7.12] — 2026-03-29

### New Features

**GPG Release Signing (#397, #398)**
- Linux AppImage and source archives are GPG-signed with detached `.asc` signatures
- SHA256SUMS.txt generated and signed for each release
- macOS artifacts signed via Apple codesign + notarization (unchanged)
- Public key published at `docs/RELEASE-SIGNING-KEY.pub.asc` and keys.openpgp.org
- Verification guide at `docs/VERIFYING-RELEASES.md`

**Commit Signing**
- All commits to `main` require GPG signatures (branch protection enforced)
- Contributor setup guide in CONTRIBUTING.md

**Configurable Band Plan Size (#406)**
- View → Band Plan submenu: Off, Small (6pt), Medium (10pt), Large (12pt), Huge (16pt)
- Strip height scales with font size
- Replaces the previous on/off checkbox

**Visual Keyboard Shortcut Manager (#239)**
- View → Configure Shortcuts opens a visual keyboard map dialog
- Full ANSI keyboard layout with keys color-coded by action category
- Click any key to assign/change/clear its binding
- ~45 bindable actions across 12 categories (Frequency, Band, Mode, TX, Audio, Slice, Filter, Tuning, DSP, AGC, Display, RIT/XIT)
- Conflict detection with reassign prompt
- Filterable action table with search and category filter
- Reset to defaults (per-key or all)
- Bindings persist across restarts via AppSettings
- Replaces hardcoded keyboard shortcuts with fully customizable bindings

**Click-and-Drag VFO Tuning (#404)**
- Click inside the filter passband and drag left/right to tune the VFO
- Frequency snaps to step size during drag
- Filter edge drag (resize) takes priority within ±5px grab zone

**Go to Frequency (G key)**
- Press G to open the VFO direct frequency entry field
- Pre-fills with current frequency, selected for easy overtype

**Space PTT Hold-to-Transmit**
- Hold Space to transmit, release to return to RX (true momentary PTT)
- Works regardless of which UI widget has focus
- Properly syncs TX state with TX applet and status bar

### Bug Fixes

**NR2/RN2/BNR Crash on DSP Mode Switch**
- SEGV in SpectralNR::process() when switching from BNR to NR2
- Root cause: enabled flag was set before the DSP object was constructed; audio arriving during the transition called process() on a null object
- Fix: set enabled flag AFTER construction succeeds; clear flag BEFORE destruction

**FlexControl UI Lag (#379)**
- Each encoder step sent a separate TCP command, flooding the radio
- Fix: coalesce rapid encoder steps into a single command every 20ms

**FlexControl Menu Stub (#380)**
- Settings → FlexControl showed "not implemented"
- Fix: wired to open Radio Setup dialog on the Serial tab

**TNF Crash on +TNF Click (#381)**
- SIGBUS on macOS when clicking +TNF after a panadapter layout change
- Root cause: rebuildTnfMarkers lambda captured raw SpectrumWidget pointer that became dangling when the pan was removed
- Fix: capture QPointer instead; also fixed duplicate `tnf create` commands per click

**FlexControl ToggleMox/ToggleTune Stuck in TX (#382)**
- Pressing the FlexControl button a second time didn't toggle TX off
- Root cause: `applyTransmitStatus()` never parsed `mox=` from radio status, so `isMox()` always returned false
- Fix: parse `mox=` key in transmit status updates

**VFO Lock Icon Not Updating (#384)**
- Lock icon on VFO overlay didn't update when toggled via FlexControl or RxApplet
- Root cause: VfoWidget::setSlice() connected 30+ SliceModel signals but was missing `lockedChanged`
- Fix: added the missing signal connection

**Mouse Wheel 8x Step on KDE/Cinnamon (#390, #405)**
- Tuning steps were 8x the selected step size on Linux Mint, Cinnamon, and KDE Plasma
- Root cause: these desktops send high-resolution angleDelta (960 per notch instead of 120)
- Fix: accumulate angleDelta and normalize to 120 units per step

**ESC Gain Slider Black Thumb (#394)**
- ESC gain slider thumb was invisible (black) on macOS and Windows
- Root cause: kSliderStyle only defined horizontal handle rules; ESC gain slider is vertical
- Fix: added vertical groove and handle QSS rules

**RxApplet NR2 Button Not Working (#329)**
- Cycling the RX panel NR button to NR2 didn't enable noise reduction
- Root cause: the `nr2CycleToggled` handler only synced the VFO button visual but never called `enableNr2WithWisdom()`
- Fix: NR2 now actually enables when cycled from the RX panel, matching VFO and overlay buttons

**Split Slice on Wrong Pan (#328)**
- In multi-pan mode, clicking SPLIT could create the TX slice on the wrong panadapter
- Root cause: split used `m_activePanId` (global) instead of the RX slice's actual pan
- Fix: use `rxSlice->panId()` for the `slice create` command
- Bonus: CW split now offsets 1 kHz up (standard convention), other modes 5 kHz

## [v0.7.11] — 2026-03-29


### New Features

**Panadapter Click-to-Spot (#36)**
- Right-click on the panadapter to create a spot marker with callsign, comment, and configurable lifetime
- Optionally forward spots to your connected DX cluster
- Right-click on existing spots: Tune, Copy Callsign, QRZ Lookup, Remove
- Spot frequency snaps to tuning step size

**Per-Slice Record & Play (#164)**
- Record (⏺) and Play (▶) buttons on each VFO flag, matching SmartSDR placement
- Record button pulses red while recording
- Play disabled until a recording exists (radio-managed)
- TX playback: press MOX then Play for a built-in voice keyer

**DAX IQ Streaming (#124)**
- Raw I/Q data from the radio's DDC to external SDR apps (SDR#, GQRX, GNU Radio)
- 4 IQ channels at 24/48/96/192 kHz via PulseAudio virtual capture devices
- DIGI applet: per-channel rate dropdown, level meters, On/Off toggle
- Spectrum overlay: IQ channel routing selector per panadapter
- Dedicated worker thread for byte-swap and pipe I/O at high sample rates

**Applet Panel Collapse (#178)**
- ☰ hamburger icon in the status bar toggles the right applet panel
- Spectrum/waterfall expands to full width when panel is hidden
- Also available via View → Applet Panel checkbox
- Custom painted +PAN spectrum icon replaces text label

**Drag-Reorderable Applets (#335)**
- Drag applets by their ⋮⋮ grip title bars to reorder in the panel
- Order persists across sessions
- View → Reset Applet Order to restore defaults
- Built on QDrag framework (future-proofs for pop-out to floating windows)

**Opus Codec Independent of RADE (#375)**
- SmartLink compressed audio now works without the RADE digital voice module
- System libopus detected via pkg-config when RADE is disabled
- Windows: setup-opus.ps1 builds static opus from source

**Modeless Dialogs**
- SpotHub, Radio Setup, and MIDI Mapping dialogs no longer block the main window
- Interact with the radio while dialogs are open

### Bug Fixes

**BNR Crash Fix (#376)**
- r8brain resampler buffer overflow — BNR output can return up to 9,600 samples at once but the resampler was allocated for 4,096. Increased to 16,384. Found and verified via ASAN.

**AppSettings Corruption Prevention**
- Atomic save: write to .tmp file, validate XML, rename over original
- Backup recovery: auto-recover from .bak if main file is corrupt
- Count guard: refuse to save if settings count dropped below half of loaded count
- Key validation: skip keys with invalid XML element name characters

**RadioModel Shutdown Use-After-Free**
- Disconnect all signals from RadioConnection before member destruction
- Prevents accessing destroyed XVTR map and slice models during teardown
- Found via AddressSanitizer (ASAN) build

**Opus SSE Alignment**
- Copy input data to alignas(16) buffers before opus_encode/opus_decode
- Prevents SEGV on SSE-optimized RADE opus builds

**Spot Label Deconfliction**
- Re-scan all placed labels after each nudge to properly stack across all levels
- Spots no longer overlap when multiple labels are close in frequency

**Other Fixes**
- Duplicate spot-to-cluster sends fixed (disconnect before reconnect in wirePanadapter)
- DIGI applet section headers use distinct label style (not confused with draggable title bars)
- DIGI applet layout: CAT Control and DAX Enable rows moved below their channel status for cleaner visual flow

### Platform Notes

- DAX IQ pipe output is Linux/macOS only. Windows DAX IQ support tracked in #87.
- Opus standalone requires system libopus on Linux (`libopus-dev`) or macOS (`brew install opus`). Windows builds from source via setup-opus.ps1.

---

73, Jeremy KK7GWY & Claude (AI dev partner)

---

## [v0.7.10] — 2026-03-29


### TX Audio
- **PR #353 merged** — DAX/SSB TX edge sync fix from @pepefrog1234. Immediate SSB TX audio gating (no delay on key-down), DAX backlog stabilization, Low-Latency DAX route option for FreeDV (Settings menu toggle)
- **MIDI CW keying** — straight key (`cw.key`), iambic dit/dah paddles (`cw.dit`/`cw.dah`), and PTT hold (`cw.ptt`) via new Gate param type. CW speed also mappable. (#295)

### Spots
- **Hover tooltips** — hover over any spot label to see callsign, frequency, source, spotter, comment, and spotted time
- **Spot trigger on click** — clicking a spot sends `spot trigger <index>` to the radio so external logging software sees the click (#341)
- **Lines stop at label** — spot dotted lines draw from bottom of spectrum to the label only, no clutter above (#337)
- **Cross-band tuning** — clicking a spot on a different band in SpotHub now tunes AND recenters the panadapter (#352)

### Panadapter
- **3v / 4v layouts** — 3 and 4 vertical pan stacking options in the layout chooser (#312)
- **Single-click-to-tune** — configurable in View menu (off by default). When enabled, single left-click on spectrum/waterfall tunes immediately (#342)

### Bug Fixes
- **Line out volume** — master volume slider controls radio line out when PC Audio is disabled (#244, #351)
- **Audio preserved on profile change** — profile load no longer switches audio to PC when listening through radio speakers (#336)
- **CW decode after profile change** — deferred re-check restarts decoder when mode status arrives after profile load (#305)
- **XVTR band stack** — added 2m, 1.25m, 440, 33cm, 23cm to band frequency lookup. Band stack save/restore now works for all XVTR bands (#346)
- **Full model reset on disconnect** — all radio-specific state (APD, tuner, amplifier, XVTR, power levels) resets when switching between different radio models (#359)
- **Bundled RtMidi** — MIDI controller support works on all platforms (Linux/macOS/Windows) without external package dependency

### Issues Closed
#25, #40, #54, #74, #105, #115, #157, #158, #190, #217, #219, #224, #226, #231, #244, #252, #262, #263, #270, #273, #295, #296, #300, #303, #305, #306, #308, #310, #312, #314, #319, #337, #338, #339, #340, #341, #342, #349, #351, #352, #355, #357, #359

Full changelog: https://github.com/ten9876/AetherSDR/compare/v0.7.9...v0.7.10

---

## [v0.7.9] — 2026-03-29


### Highlights

- **MIDI controller mapping** — Map any class-compliant USB MIDI controller to 50+ AetherSDR parameters. MIDI Learn mode: select a parameter, move a knob, binding created. Dedicated Settings → MIDI Mapping dialog with device selector, binding table, activity indicator, and named profiles. Supports CC (knobs/faders), Note On/Off (buttons/pads), and Pitch Bend (high-res tuning). CW straight key, iambic dit/dah paddle, and PTT via MIDI Gate parameters. Bindings stored in dedicated `~/.config/AetherSDR/midi.settings` XML file. Optional dependency (RtMidi). (#355, #295)

- **FlexControl USB tuning knob** — Auto-detect VID 0x2192 / PID 0x0010, rotary tuning with acceleration (1–6x), 3 configurable buttons (tap/double/hold). Radio Setup → Serial tab config. (#25)

- **USB Cable management** — Radio Setup → USB Cables tab for configuring USB-serial adapters on the radio's rear USB ports. CAT, BCD (band decoder), Bit (8-bit per-band switching), and Passthrough cable types. (#40)

- **FreeDV Reporter spots** — Real-time FreeDV station spots via Socket.IO WebSocket to qso.freedv.org. New FreeDV tab in SpotHub. (#349)

- **BNR streaming mode** — Documented container streaming vs transactional mode. The Maxine BNR container must be started with the streaming entrypoint for real-time audio processing. Wiki updated with correct docker run command.

### Other Changes

- Closed 8 TX audio / VOX issues (#54, #74, #115, #157, #158, #270, #306, #310) — all addressed by v0.7.6–v0.7.8 TX audio fixes
- RTTY Operation wiki page added
- Issue templates updated to encourage AI-assisted reports
- Radio Setup dialog widened to show all tabs without scroll arrows

### Dependencies

New optional dependencies (feature hidden when not available):
- `rtmidi` — MIDI controller support (`sudo pacman -S rtmidi`)
- `qt6-websockets` — FreeDV Reporter spots (`sudo pacman -S qt6-websockets`)

### Build

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j$(nproc)
```

Full changelog: https://github.com/ten9876/AetherSDR/compare/v0.7.6.1...v0.7.9

---

## [v0.7.8] — 2026-03-28

## What's New

### PC TX Audio
- PC microphone TX audio now works via Opus encoding on the `remote_audio_tx` stream
- Client-side mic gain control (P/CW slider, 0-100%, persisted in AppSettings)
- Client-side mic level metering with VU-style ballistics (fast attack, slow decay)
- VOX support with PC mic input
- DAX TX path (WSJT-X, VARA, etc.) remains uncompressed float32 on `dax_tx`

### P/CW Applet
- Mic level meter works correctly for both PC and hardware mic inputs
- Radio CODEC meters suppressed when mic_selection=PC (prevents noise floor flicker)
- Compression meter disabled pending gain reduction calculation fix (#345)

### Windows
- Application icon now displays in taskbar, Start Menu, and Alt-Tab

### Documentation
- Full TX audio signal path documented with all 15 radio meters (`docs/tx-audio-signal-path.md`)

## Bug Fixes
- Fixed `std::log10f` build failure on CI (not in C++ standard)

## Upgrade Notes
Drop-in replacement for v0.7.7. PC mic gain (`PcMicGain`) saved client-side in AppSettings (default 100%).

---

## [v0.7.7] — 2026-03-28


**AetherSDR v0.7.7 introduces SpotHub** — a completely new, unified spot management system that brings DX spotting to Linux ham radio like never before. Four spot sources. One panadapter. Zero external tools required.

---

## Introducing SpotHub

Gone are the days of running separate Python scripts, telnet clients, and browser tabs just to see who's on the air. SpotHub brings it all together in a single dialog (**Settings → SpotHub**) with six tabs:

### DX Cluster
Connect to any DX Spider, AR-Cluster, or CC Cluster node via telnet. Full interactive console with command input. See every spot the moment it hits the cluster — right on your panadapter.

### Reverse Beacon Network (RBN)
Automated CW/RTTY/FT8 skimmer spots from the worldwide RBN network. Built-in rate limiting prevents command flooding during contests. Connect to `telnet.reversebeacon.net` and let the skimmers do the work.

### WSJT-X Decode Spotter
**This is the big one.** AetherSDR listens for WSJT-X decode messages via UDP multicast and spots every station you can hear — directly on the panadapter. Features:
- **Smart filters**: Show only CQ calls, CQ POTA, or stations calling *you*
- **Color-coded by category**: Green for CQ, Cyan for CQ POTA, Red for stations calling you
- **SNR-based transparency**: Strong signals pop with full opacity, weak ones fade — you see propagation at a glance
- **Configurable lifetime**: 30-300 second spot decay, tuned for the fast pace of digital modes

### POTA (Parks on the Air)
Real-time POTA activation feed polling `api.pota.app`. See every active park activation on your panadapter with park reference, park name, and mode. Spot lifetime synced to the POTA API's own expiry timer. Never miss an activator again.

### Spot List
All spots from all sources in one sortable, filterable table. Band filter checkboxes (160m-6m). Source column tells you where each spot came from. Double-click any row to tune. Smart auto-scroll that pauses when you're reading and resumes when you scroll back.

### Display Controls
Full control over how spots render on the panadapter: label levels, position, font size, colors, background opacity, and spot lifetime.

---

## Per-Source Color Coding

Every spot source has its own configurable color:

| Source | Default Color | What You See |
|--------|--------------|--------------|
| DX Cluster | Tan | Classic cluster spots |
| RBN | Blue | Skimmer detections |
| WSJT-X | Green/Cyan/Red/White | Per-filter category colors |
| POTA | Yellow | Park activations |

Colors are sent to the radio via the FlexRadio spot API — they render natively on the panadapter with no client-side override needed.

## Spot Density Badges

When spots pile up (and they will), overlapping labels collapse into amber **+N** badges. Click a badge to expand a popup showing every collapsed callsign with its frequency. Click any entry to tune. No more unreadable label soup.

## Deduplication

Cross-source dedup ensures the same callsign on the same frequency doesn't flood your panadapter. If a station QSYs, the old spot is replaced. Each source uses its own appropriate lifetime for dedup timing.

## Worker Thread Architecture

All four spot sources run on a dedicated worker thread. Network I/O, protocol parsing, and log file writing happen completely off the main GUI thread. Spots are batched and forwarded to the radio once per second — smooth, efficient, and invisible.

---

## Other Fixes in This Release

- **SmartLink reconnect fix** (#224) — Signal connection leak that prevented switching between SmartLink radios
- **Version in title bar** (#315) — Window title now shows "AetherSDR vX.X.X"
- **Spot label hover cursor** — Pointing hand cursor on spot labels and density badges

---

## Upgrade Notes

Drop-in replacement for v0.7.6.x. No new required dependencies. SpotHub settings are saved to `~/.config/AetherSDR/AetherSDR.settings` on first use.

---

Built with [Claude Code](https://claude.ai/claude-code). 73 de KK7GWY.

---

## [v0.7.6.1] — 2026-03-28

## Bug Fix

- **Fix SmartLink reconnect signal leak** — `WanConnection` signal connections in `RadioModel::connectViaWan()` were never disconnected on teardown, causing duplicate signal delivery on each reconnect cycle. This prevented users from switching between SmartLink radios or cleanly disconnecting and reconnecting. (#224)

## Upgrade

Drop-in replacement for v0.7.6. No new dependencies or settings changes.

---

## [v0.7.6] — 2026-03-27

## Highlights

### NVIDIA NIM BNR — GPU-Accelerated AI Noise Removal (#288)

**A world first for SDR clients.** AetherSDR v0.7.6 introduces real-time GPU-accelerated neural noise removal powered by NVIDIA Maxine BNR, running on your local RTX GPU via a self-hosted Docker container.

- **Neural denoising** trained on massive audio datasets — superior to classical spectral (NR2) and RNNoise (RN2) approaches
- **Real-time gRPC streaming** — 48kHz mono float32, 10ms processing chunks, ~15ms total added latency
- **Intensity control** — adjustable denoising strength (0–100%) via slider in DSP panel
- **Jitter buffer** — 50ms priming for smooth, uninterrupted playback
- **Container management** — Start/Stop/Status in Radio Setup → Audio, optional autostart on app launch
- **Zero-config audio** — BNR button in VFO DSP tab and spectrum overlay, 3-way mutual exclusion with NR2/RN2

**Requirements:** NVIDIA RTX 4000+ GPU (Ada Lovelace or newer), Docker + NVIDIA Container Toolkit, NGC API key (free).

```bash
docker login nvcr.io  # Username: $oauthtoken, Password: <NGC API key>
docker run -d --gpus all --shm-size=8gb \
  -p 8001:8001 -p 8000:8000 \
  -e NGC_API_KEY=$NGC_API_KEY \
  -e STREAMING=true \
  --restart unless-stopped \
  --name maxine-bnr \
  nvcr.io/nim/nvidia/maxine-bnr:latest

cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DENABLE_BNR=ON
cmake --build build -j$(nproc)
```

### ESC Diversity Beamforming (#20, #38)

- **DIV toggle** in VFO audio tab enables dual-SCU diversity reception
- **ESC controls** — polar display (phase=angle, gain=radius), horizontal phase slider (0–360° in 5° steps), vertical gain slider (0.0–2.0)
- **Real-time ESC meter** — signal strength bar after ESC processing
- Protocol verified against SmartSDR pcap: `esc=on/off`, phase in radians, DiversityChild guard per FlexLib
- Requires DIV_ESC license (SmartSDR+) and dual-SCU radio

### Band Zoom Buttons

- **S** (Segment) and **B** (Band) buttons at bottom-left of waterfall
- Uses radio-native `segment_zoom=1` / `band_zoom=1` commands (SmartSDR protocol)

## Bug Fixes

- **XVTR band switch crash** — QPointer prevents SEGV when panadapter destroyed during band change
- **CW decoder not working on first pan** — initial applet wasn't wired through setActivePanApplet()
- **Band change panadapter not scrolling** — use tuneAndRecenter() instead of autopan=0
- **VFO widget not shrinking** — fixed setFixedWidth constraining height when ESC panel hidden
- **Null pointer guards** — m_panApplet checked before setCwPanelVisible() calls

## Build

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j$(nproc)

cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DENABLE_BNR=ON
cmake --build build -j$(nproc)
```

73, Jeremy KK7GWY & Claude (AI dev partner)

---

## [v0.7.5] — 2026-03-27

## What's New

### ESC (Enhanced Signal Clarity) — Diversity Beamforming (#20, #38)
- **DIV toggle** in VFO audio tab enables dual-SCU diversity reception
- **ESC controls**: toggle button, horizontal phase slider (0–360° in 5° steps), vertical gain slider (0.0–2.0)
- **Polar display**: crosshair circle with dot positioned by phase (angle) and gain (radius from center)
- **Real-time ESC meter**: signal strength bar after ESC processing (SLC/ESC meter, 10 fps)
- Protocol matches SmartSDR pcap: `esc=on/off`, phase in radians, DiversityChild guard per FlexLib
- ESC panel visible only on diversity parent slice — child slices show DIV but not ESC controls
- Requires DIV_ESC feature license (SmartSDR+ or higher)
- Dual-SCU radios only: FLEX-8600(M), FLEX-6700(R), FLEX-6600(M), AU-520(M)

### Bug Fixes
- **Band change panadapter scroll**: switching bands now recenters the panadapter on the new frequency (was sending `autopan=0` which prevented scrolling)
- **VFO widget resize**: panel properly shrinks when DIV is disabled (fixed `setFixedWidth` constraining height)

## Install
Download the appropriate package for your platform from the assets below, or build from source:
```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j$(nproc)
./build/AetherSDR
```

73, Jeremy KK7GWY & Claude (AI dev partner)

---

## [v0.7.4] — 2026-03-26

## What's New in v0.7.4

### VOX Support — #253
AetherSDR now creates a `remote_audio_tx` stream on connect and continuously streams mic audio to the radio during RX. This enables the radio's VOX (Voice-Operated Transmit) detection via `met_in_rx=1`.

Previously, only `dax_tx` streams were created and mic audio was only sent during active transmit — so VOX had no audio to monitor and could never trigger.

The DAX TX path is unchanged (only sends during active transmit to prevent mic bleed into digital modes). VOX audio flows on a separate `remote_audio_tx` stream with its own accumulator.

### Profile Load Fix — #289
Applying a global profile no longer causes the FFT spectrum to disappear. The radio resets `x_pixels`/`y_pixels` to defaults during profile application — AetherSDR now detects this and automatically re-pushes correct widget dimensions.

### Band Stack Alignment Fix — #279, #291
Removed bandwidth and center from band stack save/restore. Both are radio-authoritative per FlexLib API — pushing stale saved values caused FFT/waterfall horizontal misalignment on band changes. Only dBm scale (a client display preference) is persisted.

---

**Full Changelog**: https://github.com/ten9876/AetherSDR/compare/v0.7.3.1...v0.7.4

---

## [v0.7.3.1] — 2026-03-26

## v0.7.3.1 Hotfix

Fixes two regressions introduced in v0.7.3:

### Profile Load Blank Spectrum — #289
Applying a global profile caused the FFT spectrum to disappear. The radio resets `x_pixels`/`y_pixels` to defaults (50/20) during profile application, overwriting our correct widget dimensions. Now automatically detects when the radio reports small pixel values in status and re-pushes the actual widget dimensions.

### FFT/Waterfall Alignment on Band Change — #279, #291
Switching bands caused horizontal misalignment between FFT spectrum and waterfall. The band stack was saving and restoring **bandwidth** (a radio-authoritative setting per FlexLib API), which overrode the radio's bandwidth and created a timing mismatch. Removed bandwidth and center from band stack persistence — only dBm scale (a client-side display preference) is saved/restored.

### Also included from v0.7.3 post-release
- Title bar speaker mute now controls local PC audio engine (#259)
- VFO tab bar speaker icon toggles muted/unmuted on right-click (#283)

---

**Full Changelog**: https://github.com/ten9876/AetherSDR/compare/v0.7.3...v0.7.3.1

---

## [v0.7.3] — 2026-03-26

## What's New in v0.7.3

### DVK (Digital Voice Keyer) — #19
- **12-slot recording panel** with F1-F12 hotkeys, REC/STOP/PLAY/PREV buttons
- **Elapsed timer** with 100ms resolution and per-slot progress bars (red/green/blue by operation)
- **Right-click context menu**: Rename, Clear, Delete, Import WAV, Export WAV
- **Inline name editing** via double-click, with forbidden character stripping
- **WAV export** (download): reverse TCP transfer from radio to local file
- **WAV import** (upload): validates format (2-ch, 32-bit float, 48 kHz, max 5 MB), TCP streaming to radio
- **Mode-aware availability**: DVK enabled only in voice modes (USB/LSB/AM/SAM/FM/NFM/DFM)
- **Empty slot guards**: prevents TX on empty slots, disables Clear/Delete/Export for empty slots

### CWX/DVK Panel Management
- CWX enabled only in CW/CWL modes, DVK only in voice modes
- Three indicator states: active (cyan), available (dim), unavailable (dark grey)
- Auto-close on mode switch, mutual exclusion between panels
- 4-pane splitter layout fix (CWX + DVK + PanStack + AppletPanel) — #281

### FFT/Waterfall Alignment Fix — #279
- Removed hardcoded xpixels=1024 in RadioModel::configurePan() that overwrote correct widget dimensions
- FFT spectrum and native waterfall tiles now align horizontally at all window sizes

### FFT dBm Calibration Fix
- Bin conversion now uses actual y_pixels from radio status (bins are pixel Y positions, not 0-65535)
- Tracks y_pixels from radio status updates for correct dBm scaling across different display sizes

### Mute Controls — #259, #283
- **Title bar speaker button** now mutes local PC audio engine (was only muting radio line out)
- **VFO tab bar speaker icon** toggles between muted/unmuted to show mute state at a glance
- **Right-click speaker icon** on VFO tab bar toggles mute directly (matches SmartSDR)

---

**Full Changelog**: https://github.com/ten9876/AetherSDR/compare/v0.7.2...v0.7.3

---

## [v0.7.2] — 2026-03-25

## Bug Fixes

- **Per-slice step size from radio** (#274, #241) — Step size and step list are now driven entirely by the radio's per-slice, per-mode status. The RX applet stepper dynamically rebuilds when switching slices or modes. Removed client-side step overwrite that was fighting the radio's mode-specific defaults.

- **Antenna Genius discovery retry** — If the UDP port 9007 bind fails at startup (e.g. another process holds the port), AetherSDR now retries every 5 seconds until it succeeds. Once detected, the AG persists for the app lifetime. Previously a failed bind meant the AG was never detected.

## Policy

- **Radio-authoritative settings** — Documented and enforced the policy that the radio is always authoritative for any setting it stores. AetherSDR only persists client-side settings the radio doesn't know about. This prevents the radio and client from fighting on reconnect.

---

🤖 Generated with [Claude Code](https://claude.com/claude-code)

73, Jeremy KK7GWY & Claude (AI dev partner)

---

## [v0.7.1] — 2026-03-25


Bug fixes, multi-pan stability improvements, and new CW/spot controls.

### Multi-Pan Improvements
- **Radio-authoritative pan restore** — radio restores pans from GUIClientID session, client arranges layout (no more creating/removing pans on startup)
- **Close pan via X button** — click the X on any pan's title bar to close it and auto-rearrange remaining pans
- **Defensive xpixels re-push** — 500ms delayed re-push ensures all pans get proper FFT bin resolution after connect
- **TGXL/PGXL meter separation** — meters routed by amplifier handle so TGXL and PGXL gauges don't cross-contaminate

### CW Decoder Controls
- **Lock Pitch** button — prevents decoder from wandering to noise after finding the CW tone
- **Lock Speed** button — locks WPM to current detected value
- **Pitch Range sliders** (Lo/Hi) — constrain decoder frequency search window (300-1200 Hz)
- **Numeric Hz labels** on pitch range sliders with dynamic tooltips

### Spot Settings Enhancements (#260)
- **Color picker** for spot text override color
- **Color picker** for spot background override color
- **Background Opacity** slider (0-100)
- **Live preview** — all changes apply immediately to the spectrum

### Bug Fixes
- **macOS trackpad scroll-to-tune** (#266) — handle pixelDelta for trackpad, ignore momentum scrolling
- **FWDPWR/SWR meter source** (#233) — match "TX-" not just "TX" for exciter power
- **S-Meter TX power with PGXL** — shows amplifier output power when PGXL is connected
- **Title bar mute toggles** (#259) — click speaker/headphone icons to mute/unmute line out and headphones
- **Filter polarity normalization** — DIGU/USB modes correct negative filter offsets from radio session restore
- **Band stack filter recall** — only recalls filter when saved mode matches recalled mode

### Known Issues
- CW sidetone not yet available through PC Audio (radio sends sidetone to physical outputs only, client-side generation deferred)
- `sub codec all` subscription rejected on firmware v4.1.5 (harmless)

---

🤖 Generated with [Claude Code](https://claude.com/claude-code)

73, Jeremy KK7GWY & Claude (AI dev partner)

---

## [v0.7.0] — 2026-03-25


## CWX Keyer Panel

Built-in CW keyboard keyer — toggle via the **CWX** label in the status bar.

- **Send mode**: type your message, hit Enter to send with perfect timing and spacing
- **Live mode**: each keystroke sends immediately for real-time CW keying
- **F1-F12 macro editor**: store and recall 12 CW macros via the Setup view
- **F-key hotkeys**: press F1-F12 to send macros when the CWX panel is visible (independent of global keyboard shortcuts toggle)
- **Chat bubble history**: sent messages displayed as rounded bubbles with timestamps, scrolling from bottom up
- **Speed control**: 5-100 WPM stepper synced with radio
- **Prosign support**: = (BT), + (AR), ( (KN), & (BK), $ (SK)
- **QSK toggle** and **break-in delay** in Setup view

## AMP Applet — PGXL Amplifier Display

New **AMP** button in the applet panel, auto-shows when a Power Genius XL is detected.

- Forward Power gauge (0-2000W)
- SWR gauge (1-3, converted from Return Loss)
- Temperature gauge (30-100°C)
- Matches TGXL gauge styling (HGauge widgets)
- Meter wiring confirmed from live PGXL log data

## CW Decode Sensitivity Slider

New **Sens** slider in the CW decode bar filters out low-confidence decoded characters.

- Slide right = higher sensitivity (fewer garbage characters)
- Slide left = show everything
- Reduces noise in the CW decode display on weak signals

## Other Improvements

- Improved log file handling and diagnostics
- Additional issue triage and community engagement

---

🤖 Generated with [Claude Code](https://claude.com/claude-code)

73, Jeremy KK7GWY & Claude (AI dev partner)

---

## [v0.6.2] — 2026-03-24


## Band Stacking Registers

Per-band save/recall is here. When you switch bands using the spectrum overlay, AetherSDR remembers your last settings and restores them when you come back.

**Saved per band:** frequency, mode, AGC mode + threshold, RX/TX antenna, squelch, step size, all DSP flags (NR, NB, ANF, NRL, NRS, RNN, NRF, ANFL, ANFT, APF, NR2, RN2), pan bandwidth (zoom), and dBm scale.

First visit to a band uses the static default. Subsequent visits recall your last settings. Settings persist across restarts.

## Multi-Pan Improvements

- **Per-pan band change** — each pan's band buttons now target the correct slice, not the global active slice
- **Pan recenters on band change** — FFT/waterfall follows the VFO when switching bands across pans
- **Correct slice labels** — each pan shows the right slice letter (A, B, C, D)
- **Cross-band save guard** — prevents frequency contamination between bands in multi-pan mode
- **Widget cleanup on pan removal** — prevents crashes when reducing pan layouts

## Per-Slice Step Size

The radio sends per-slice step sizes and step lists (different for CW vs SSB vs digital). AetherSDR now parses these and syncs the RX applet stepper when switching between slices. The correct step list is shown per mode.

## New Features

- **Frequency auto-calibration** — Radio Setup → RX tab: Cal Frequency field + Start button + Offset display
- **Keyboard shortcuts** — View → Keyboard Shortcuts (off by default): arrow keys, step cycle, PTT, mute, tune lock
- **DX spot display** — spots from external tools (FlexSpots, N1MM, etc.) appear as clickable labels on the panadapter
- **Spot click-to-tune** — click a spot label to tune to that frequency
- **Spot settings** — Settings → Spots: enable/disable, font size, max stacking levels, color overrides

## Bug Fixes

- Fix GCC 13 internal compiler error on Ubuntu 24.04 (#254)
- Fix filter polarity on session restore (wrong-side passband for DIGU/USB)
- Fix FWDPWR/SWR meter on all radios — source matching was too strict (#233)
- Fix TGXL handle: accept initial 0x00000000 and upgrade to real handle
- Fix SmartLink: use UPnP ports when manual port forwards are absent (#230)
- Fix preamp sync: shared antenna hardware state propagated to all pans (#232)
- Fix crash in mhzToX during band transitions (divide by zero with spots visible)
- Fix Windows support bundle: resolve symlink to actual log file (#243)
- Fix memory recall to use per-pan commands (#236)
- Improved log file handling and diagnostics

---

🤖 Generated with [Claude Code](https://claude.com/claude-code)

73, Jeremy KK7GWY & Claude (AI dev partner)

---

## [v0.6.1] — 2026-03-24


Patch release with critical bug fixes, new features, and multi-pan stability improvements.

## New Features

### DX Spot Display (#228)
- **Spot markers on panadapter** — colored callsign labels at spotted frequencies with dotted tick lines
- **Click-to-tune** — click any spot label to tune to that frequency
- **Settings → Spots** — full control: enable/disable, font size, max stacking levels, vertical position, color override, clear all
- Works with FlexSpots, N1MM+, DXLabs, or any tool that sends `spot add` to the radio

### Keyboard Shortcuts (#234)
- **View → Keyboard Shortcuts** to enable (off by default for safety)
- Left/Right: nudge frequency by step size
- Shift+Left/Right: nudge by 10× step
- Up/Down: AF gain ±5
- T: toggle MOX | M: toggle mute | L: toggle tune lock
- [ / ]: cycle step size

### Frequency Calibration (#201)
- Radio Setup → RX tab: Cal Frequency field, Start button, Freq Offset (ppb) display
- Pre-populated from radio's `cal_freq` on connect

## Bug Fixes

- **Fix FWDPWR/SWR meter reading zero on all radios** (#233) — meter source is `TX-` not `TX`, broke in v0.5.8 PGXL fix
- **Fix SmartLink connection on UPnP radios** (#230) — now parses `public_upnp_tls_port`/`public_upnp_udp_port` (was connecting to port 65535)
- **Fix TGXL buttons not working** — handle was stuck at 0x00000000 from initial status, now upgrades to real handle
- **Fix preamp sync in Multi-Flex** (#232) — antenna preamp is shared hardware, now applied to all pans regardless of client
- **Fix VFO NR2 button not working** (#227) — connections now permanent per-VFO, not re-wired on focus switch
- **Fix scroll-to-tune only going one direction** — same-pan tuning uses immediate model update path
- **Fix crash on pan removal with active spots** — spot rebuild lambda now guarded with QPointer
- **Fix Windows support bundle** (#243) — resolves .lnk symlink to actual log file

## Multi-Pan Improvements

- Per-pan bandwidth, center, and dBm range drag commands (#236) — drags on one pan no longer affect other pans
- Layout restore on reconnect uses `applyPanLayout` to handle pan count mismatch from split mode

---

🤖 Generated with [Claude Code](https://claude.com/claude-code)

73, Jeremy KK7GWY & Claude (AI dev partner)

---

## [v0.6.0] — 2026-03-24


**Multi-pan is here!** This release introduces experimental support for multiple simultaneous panadapters on FlexRadio transceivers.

> ⚠️ **Experimental Feature** — Multi-panadapter support is functional but still under active development. Some interactions between pans may behave unexpectedly. We need your help testing and finding bugs. Please report any issues you encounter on the [Issues](https://github.com/ten9876/AetherSDR/issues) page.

## Multi-Panadapter Highlights

- **Layout Picker** — click +PAN in the status bar to choose from available layout options:
  - A / B (2 vertical), A | B (2 horizontal)
  - A|B / C (2 top + 1 bottom), A / B|C (1 top + 2 bottom)
  - A|B / C|D (2×2 grid), Single (1 pan)
- **Independent FFT + waterfall** per pan with native VITA-49 tile routing
- **Independent display settings** per pan (AVG, FPS, fill, gain, black level)
- **Click-to-tune** uses SmartSDR protocol (`slice m <freq> pan=<panId>`)
- **Layout persistence** — saved across restarts
- **Pan reduction** — switch from more pans to fewer, excess pans close automatically
- Dual-SCU radios (FLEX-6600/6700/8600, AU-520): up to 4 pans
- Single-SCU radios (FLEX-6400/8400, AU-510): up to 2 pans

## Other New Features

- **Heartbeat indicator** — title bar circle flashes green on each TCP ping, blinks red on missed beats
- **Keepalive ping** — sends `keepalive enable` + 1s ping timer (matches FlexLib protocol)
- **Reconnect dialog** — "Radio disconnected — Waiting for reconnect" on unexpected disconnect with auto-reconnect
- **Fast disconnect detection** — 5s discovery stale timeout with TCP ping fallback for routed/SmartLink
- **Low Bandwidth Connect** — connection panel checkbox for VPN/LTE/metered connections
- **PA inhibit during TUNE** — opt-in safety feature disables ACC TX output before tune, restores after
- **VFO TX badge toggle** — click to assign OR unassign TX from any slice
- **TGXL OPERATE disables TUNE/ATU/MEM** — TX applet buttons dimmed when external tuner is in OPERATE mode
- **VFO slider value labels** — AF gain, SQL, AGC-T show numeric values
- **RIT/XIT offset lines** — dashed lines on panadapter showing actual RX/TX frequencies
- **RTTY mark/space lines** — dashed M/S frequency lines on panadapter in RTTY mode
- **Multi-Flex indicator** — green "multiFLEX" badge in title bar with hover tooltip
- **Step size persistence** — tuning step saved across restarts
- **RTTY mark default from radio** — reads radio's value on connect
- **Show TX in Waterfall** — waterfall freezes during TX when disabled, multi-pan aware
- **Network MTU setting** — Radio Setup → Network → Advanced
- **XVTR panel** — 2×4 grid with auto-grow for configured bands
- **Fill slider label fix** — display panel Fill slider updates label
- **Additional protocol subscriptions** — cwx, dax, daxiq, radio, codec, dvk, usb_cable, spot, license

## Bug Fixes

- Fix volume slider sync: parse `audio_level` not `audio_gain` (#161)
- Fix exciter forward power disappearing when PGXL connected (#181)
- Fix NR2/RN2 button sync between spectrum overlay, VFO, and RX applet
- Fix NR2 freeze: spectrum overlay bypassed FFTW wisdom generation (#214)
- Fix crash on exit: proper MainWindow destructor stops DSP before teardown (#167)
- Fix SQL button showing enabled in digital modes (#192)
- Fix TNF toggle sending no command (#184)
- Fix FDX toggle: optimistic update since radio doesn't echo status (#188)
- Fix DAX channel not persisting (#180)
- Fix profile save overwrite UX (#177)

## Protocol Discoveries (SmartSDR pcap analysis)

- SmartSDR **never sends `slice set <id> active=1`** — active slice is client-side only
- Click-to-tune uses `slice m <freq> pan=<panId>` with radio-side slice routing
- Per-pan `xpixels/ypixels` must be pushed after creation (radio defaults to 50×20)
- Waterfall tiles use stream ID 0x42xxxxxx (distinct from FFT's 0x40xxxxxx)
- `keepalive enable` + `ping ms_timestamp=` every 1 second

## Known Multi-Pan Issues

- Bandwidth drag may affect rates on other pans (#236)
- Split mode interaction with multi-pan needs further testing
- Per-pan display settings are not pushed on reconnect (second pan gets radio defaults)
- Visual stepping when adding pans (pans appear one at a time before rearranging)

---

🤖 Generated with [Claude Code](https://claude.com/claude-code)

73, Jeremy KK7GWY & Claude (AI dev partner)

---

## [v0.5.8] — 2026-03-23

## v0.5.8 — Heartbeat, Reconnect, PA Safety & Bug Fixes

### New Features

- **Heartbeat indicator** — Small circle in title bar flashes green on each radio ping, blinks red when connection is lost
- **Reconnect on power cycle** — Dialog appears on unexpected disconnect with auto-reconnect via 5s retry timer and discovery detection (#209)
- **Fast disconnect detection** — Radio loss detected in ~5s via discovery timeout instead of 30-60s TCP timeout (#209)
- **PA inhibit during TUNE** — Safety feature: opt-in setting temporarily disables ACC TX output during tune to protect external amplifiers (#156)
- **Low Bandwidth Connect** — Checkbox in connection panel sends `client low_bw_connect` for VPN/LTE/metered links
- **Keepalive ping** — Sends `keepalive enable` + 1s ping (matches FlexLib), drives heartbeat for all connection types
- **VFO slider value labels** — AF gain, SQL, AGC-T sliders show numeric values in VFO widget (#198)
- **Step size persistence** — Tuning step saved/restored across restarts (#211)
- **TGXL OPERATE disables TUNE/ATU/MEM** — TX applet buttons dimmed when external tuner is in OPERATE mode (#197)

### Bug Fixes

- **NR2 freeze on first enable** — Spectrum overlay DSP panel bypassed FFTW wisdom generation, freezing UI. Now all paths use background thread with progress dialog (#214)
- **NR2/RN2 persistence** — Client-side DSP state saved on exit, restored on launch (#167)
- **Exciter power disappearing with PGXL** — FWDPWR meter index overwritten by amplifier's meter. Now filtered by source "TX" (#181)
- **VFO TX badge** — Now toggles (assign AND unassign), matching RX applet behavior (#213)
- **Volume slider sync** — Fixed status key `audio_level` (was `audio_gain`), sliders track Maestro/SmartControl changes (#161)
- **TGXL state detection** — BYPASS mode (`operate=1 bypass=1`) no longer disables TX applet buttons (#197)

### Closed Issues

#101, #116, #119, #132, #137, #149, #161, #173, #177, #180, #181, #185, #189, #198, #199, #213, #214, #218, #222 and more

### Wiki

- New page: [Low Bandwidth Connections](https://github.com/ten9876/AetherSDR/wiki/Low-Bandwidth-Connections)

---

🤖 Generated with [Claude Code](https://claude.com/claude-code)

---

## [v0.5.7] — 2026-03-23


Major bug fix and feature release — 25+ issues addressed across two marathon sessions.

### Bug Fixes
- **Crash on exit** (#167): double-free of VfoWidget buttons during Qt teardown; proper MainWindow destructor stops NR2/RN2/RADE before destruction
- **Volume sliders not syncing** (#161): wrong status key (`audio_gain` → `audio_level`); sliders now track Maestro/SmartControl and profile changes
- **FDX toggle not working** (#188): radio accepts but doesn't echo status — added optimistic update
- **TNF toggle not sending command** (#184): `setGlobalEnabled()` now emits `commandReady`
- **SQL showing green in digital modes** (#192): disabled button with distinct dimmed style in DIGU/DIGL; squelch saved/restored on mode switch
- **Fill slider label not updating** (#206): missing `setText()` in `valueChanged` handler
- **NR2/RN2 not syncing** between spectrum overlay, VFO widget, and RX applet
- **Show TX in Waterfall had no effect** (#207): waterfall now freezes during TX when disabled (multi-pan aware)
- **RTTY mark default hardcoded** (#200): now reads from radio status on connect

### New Features
- **Multi-Flex indicator** (#185): green "multiFLEX" badge in title bar with connected client tooltip
- **Configurable quick-mode buttons** (#191): right-click USB/CW/AM to assign any mode; SSB toggles USB↔LSB, DIG toggles DIGU↔DIGL
- **RTTY mark/space lines** (#189): dashed M/S frequency lines on panadapter in RTTY mode
- **RIT/XIT offset lines** (#199): dashed lines showing actual RX (slice color) and TX (red) frequencies
- **UI scaling** (#194): View → UI Scale (75%–200%) via `QT_SCALE_FACTOR`
- **Band plan overlay toggle** (#193): View menu checkbox to show/hide ARRL overlay
- **Network MTU** (#202): Radio Setup → Network → Advanced spin box
- **Station name** (#182): configurable in Radio Setup → Radio tab
- **VFO slider value labels** (#198): AF gain, SQL, AGC-T show live numeric values
- **XVTR panel** (#204): 2×4 grid with auto-grow for configured transverter bands
- **DAX channel persistence** (#180): saved/restored across restarts
- **Profile manager UX** (#177): selecting a profile populates the name field
- **Client-side DSP persistence**: NR2/RN2 state restored on launch
- **PA temp/voltage precision** (#195): XX.X°C and XX.XX V

### Improvements
- **macOS mic permission** (#157): proper `AVAuthorizationStatus` check with diagnostic logging
- **APD visibility**: hidden on radios that don't support it
- **FDX indicator**: matches TNF/CWX/DVK font size (24px)
- **Disabled button style**: reusable `kDisabledBtn` for dimmed non-clickable buttons

### Protocol
- Added `sub client all` subscription for real-time Multi-Flex client tracking
- Client disconnect status parsing (`client <handle> disconnected`)
- Network MTU sent on connect (`client set enforce_network_mtu=1 network_mtu=N`)

---

73, Jeremy KK7GWY & Claude (AI dev partner)

---

## [v0.5.6] — 2026-03-23



---

## [v0.5.5] — 2026-03-22



---

## [v0.5.4] — 2026-03-22



---

## [v0.5.3] — 2026-03-22



---

## [v0.5.2] — 2026-03-21

## What's Changed

- **Log file rotation**: timestamped filenames, keeps last 5 sessions
- **Multi-Flex discovery**: show connected client station name (e.g. "Available (Multi-Flex: Maestro)")
- **Default diagnostic logging**: Discovery, Commands, and Status logging enabled by default (pre-1.0)
- **Antenna Genius applet layout fix**: buttons and labels no longer clip in the 260px sidebar

Full changelog: https://github.com/ten9876/AetherSDR/compare/v0.5.1...v0.5.2

---

## [v0.5.1] — 2026-03-21



---

## [v0.5.0] — 2026-03-21



---

## [v0.4.17] — 2026-03-21



---

## [v0.4.16] — 2026-03-20



---

## [v0.4.15] — 2026-03-20

## SmartLink Remote Operation — Now Working!

Operate your FlexRadio over the internet from Linux. Connect via SmartLink and get full spectrum, waterfall, audio, and meters — just like being on the local network.

### What's new

- **SmartLink VITA-49 UDP streaming** — FFT spectrum, waterfall tiles, RX audio (float32 stereo), and meter data now stream over WAN. Previously only the TLS command channel worked; now the full receive path is functional.
- **WAN UDP registration protocol** — Implements FlexLib's `client udp_register handle=0x<HANDLE>` sent via UDP datagram (not TCP `client udpport`, which the radio rejects on WAN connections with error `0x500000AA`).
- **NAT pinhole keepalive** — Sends `client ping handle=0x<HANDLE>` via UDP every 5 seconds to maintain the NAT mapping for sustained remote sessions.
- **Pre-bound UDP socket** — Binds the VITA-49 UDP port before requesting SmartLink connection, passing `hole_punch_port` to the relay server so the radio knows where to send packets.
- **SmartLink debug logging** — Comprehensive logging across the full WAN connection flow (Auth0, SmartLink server, TLS handshake, UDP registration, VITA-49 packet arrival) for troubleshooting.
- **Fixed log file path** — Logs now write to `~/.config/AetherSDR/aethersdr.log` (was double-nested).
- **macOS DAX virtual audio bridge** (PR #93) — CoreAudio HAL plugin for DAX audio devices on macOS.
- **Fixed low SSB voice TX level** (PR #94) — +24 dB gain compensation for USB mic input.

### SmartLink requirements

- FlexRadio with SmartSDR+ subscription
- UPnP enabled on the radio's router (or manual UDP port forwarding for port 4993)
- NAT hole-punching for radios without UPnP is not yet implemented

### Known limitations

- Audio may be jittery on high-latency connections (jitter buffer not yet implemented)
- Opus compressed audio (PCC 0x8005) not yet supported — uses uncompressed float32 (~384 kbps)
- No WAN auto-reconnect on connection loss

### Tested

FLEX-8600 fw v4.1.5 via SmartLink with UPnP — all four VITA-49 stream types confirmed working (FFT 0x8003, waterfall 0x8004, audio 0x03E3, meters 0x8002) at ~330 KB/s sustained.

---

**Full changelog:** https://github.com/ten9876/AetherSDR/compare/v0.4.14...v0.4.15

---

## [v0.4.14] — 2026-03-19



---

## [v0.4.13] — 2026-03-19



---

## [v0.4.12] — 2026-03-19



---

## [v0.4.11] — 2026-03-19


### PC Audio Toggle
New **PC** button on the VFO widget audio tab controls whether audio plays through your PC speakers or the radio's physical outputs (line out, headphone, front speaker).

- **PC ON** (green, default) — audio streams to your PC via `remote_audio_rx`
- **PC OFF** (grey) — audio plays through the radio's line out / headphone / front speaker

This fixes the issue where users with powered speakers connected to the radio's line out jack couldn't hear audio (#71). The radio mutes its physical outputs when a `remote_audio_rx` stream is active — toggling PC Audio OFF removes that stream.

The setting persists across sessions.

### Also in this release
- 48kHz audio fallback for devices that don't support 24kHz
- AppImage rebuilt with Qt 6.7 via aqtinstall (fixes Ubuntu audio)
- Windows build bundles libfftw3-3.dll (#81)
- XVTR band sub-menu and context-aware frequency entry
- Direct frequency entry (double-click frequency display)
- NR2 UX improvements (3-state NR button, wisdom dialog)
- 4O3A Antenna Genius support (@EI6JGB)

### Downloads
| Platform | File |
|----------|------|
| Linux x86_64 | `AetherSDR-v0.4.11-x86_64.AppImage` |
| Linux ARM | `AetherSDR-v0.4.11-aarch64.AppImage` |
| macOS | `AetherSDR-v0.4.11-macOS-universal.dmg` |
| Windows | `AetherSDR-v0.4.11-Windows-x64.zip` |

### Full Changelog
https://github.com/ten9876/AetherSDR/compare/v0.4.10...v0.4.11

---

## [v0.4.10] — 2026-03-18


### Critical: Audio Fix for Ubuntu/Debian
- **AppImage rebuilt with Qt 6.7 via aqtinstall on Ubuntu 22.04 base** — fixes empty audio device dropdowns and no audio output on Ubuntu 24.04, Debian 12+, and other modern distros
- Root cause: Ubuntu 22.04's Qt 6.2.4 had no separate multimedia plugin — linuxdeploy couldn't bundle it. Now using Qt 6.7.3 which ships the multimedia backend as a proper loadable plugin
- Bundles GStreamer audio plugins (pulseaudio, alsa, pipewire) directly in the AppImage
- **48kHz fallback**: AudioEngine now automatically upsamples RX (24k→48k) and downsamples TX (48k→24k) for devices that don't natively support 24kHz
- Diagnosed with help from a second Claude instance running inside an Ubuntu 24.04 VM

### XVTR Transverter Support
- XVTR button in band grid opens transverter sub-panel with configured bands
- "HF" button returns to regular band grid
- Context-aware frequency entry: on XVTR bands, bare integers get decimal after 3rd digit (1446 → 144.6 MHz)

### Direct Frequency Entry
- Double-click the frequency display (VFO widget or RX applet) to type a frequency
- Accepts: 14.225 (MHz), 14225 (kHz), 14225000 (Hz), 14.225.000 (dotted)

### NR2 UX Improvements
- 3-state NR button: Off → NR → NR2 → Off
- FFTW wisdom dialog: progress after each plan, breathing animation, auto-close messaging

### Windows Fix
- Bundle `libfftw3-3.dll` in Windows ZIP (fixes #81)

### 4O3A Antenna Genius Support (contributed by @EI6JGB)
- UDP discovery, TCP control, per-port antenna grid, band→antenna memory

### Downloads
| Platform | File |
|----------|------|
| Linux x86_64 | `AetherSDR-v0.4.10-x86_64.AppImage` |
| Linux ARM | `AetherSDR-v0.4.10-aarch64.AppImage` |
| macOS | `AetherSDR-v0.4.10-macOS-universal.dmg` |
| Windows | `AetherSDR-v0.4.10-Windows-x64.zip` |

### Full Changelog
https://github.com/ten9876/AetherSDR/compare/v0.4.9...v0.4.10

---

## [v0.4.9] — 2026-03-18


### AppImage Audio Fix (Ubuntu/Debian)
The AppImage now bundles GStreamer and PipeWire multimedia plugins. This fixes the no-audio issue reported on Ubuntu 24.04 LTS (#75) where the PC audio device list was empty despite system audio working normally.

### Firmware Update Staging
New staged firmware update workflow in Settings → Radio Setup → Radio tab:
- **Check for Update** queries FlexRadio's website and compares versions
- **Download** fetches the SmartSDR installer, verifies MD5 against published hash
- **Extract** carves the .ssdr firmware file from the installer binary
- **Validate** checks the extracted file header before enabling upload
- **Browse .ssdr** still available for manual file selection
- Experimental feature — disclaimer included

### NR2 UX Polish
- 3-state NR button cycle in RX Applet: Off → NR → NR2 → Off
- NR2 toggle added to DSP side panel
- All NR2 buttons sync bidirectionally
- Buttons only show "on" after wisdom generation completes
- FFTW wisdom dialog: progress reports after each plan, breathing animation, auto-close messaging

### Platform Firmware Mapping
All consumer FlexRadios (Microburst/DeepEddy/BigBend/Aurora) use FLEX-6x00 firmware. Only DragonFire (FLEX-9600) uses separate firmware.

### Downloads
| Platform | File |
|----------|------|
| Linux x86_64 | `AetherSDR-v0.4.9-x86_64.AppImage` |
| Linux ARM | `AetherSDR-v0.4.9-aarch64.AppImage` |
| macOS | `AetherSDR-v0.4.9-macOS-universal.dmg` |
| Windows | `AetherSDR-v0.4.9-Windows-x64.zip` |

### Full Changelog
https://github.com/ten9876/AetherSDR/compare/v0.4.7a...v0.4.9

---

## [v0.4.7a] — 2026-03-18


Minor UX improvements to the NR2 spectral noise reduction feature introduced in v0.4.7.

### NR Button 3-State Cycle
The NR button in the RX Applet now cycles through three states:
- **Off** → click → **NR** (radio-side noise reduction)
- **NR** → click → **NR2** (client-side spectral noise reduction)
- **NR2** → click → **Off**

The button label changes to show which mode is active. NR and NR2 are never on simultaneously — the radio's NR is automatically disabled when switching to NR2.

### Wisdom Dialog Improvements
- Progress bar now advances **after** each FFT plan completes, not before — no more false 100% while the last plan is still computing
- Description label shows which plan is being computed during slow phases ("Computing COMPLEX-TO-REAL FFT size 262144...")
- Breathing opacity animation on the dialog when progress >= 90% (the slow phase) so users know it's still working
- "Wisdom generation complete!" confirmation before auto-close
- Light text on progress bar readable at all fill levels

### NR2 Button Sync
All three NR2 controls (VFO widget, DSP side panel, RxApplet) now stay in sync and only show "on" after wisdom generation completes and NR2 is actually processing audio.

### DSP Side Panel
- NR2 toggle button added to the left-side DSP overlay panel
- DSP panel top-aligned with button menu

### Downloads
Pre-built binaries auto-attach when CI completes (~5 min).

### Full Changelog
https://github.com/ten9876/AetherSDR/compare/v0.4.7...v0.4.7a

---

## [v0.4.7] — 2026-03-18


### NR2 Spectral Noise Reduction (contributed by @EI6JGB)

Client-side spectral noise reduction using the Ephraim-Malah MMSE Log-Spectral Amplitude estimator with OSMS noise floor tracking. This is a sophisticated DSP algorithm that complements the radio's built-in NR — particularly effective on weak signals where the radio's NR alone isn't enough.

**Features:**
- Toggle NR2 in the VFO widget DSP panel
- FFTW3 optimized FFTs with automatic radix-2 fallback when FFTW3 isn't installed
- Background FFTW wisdom generation — audio continues playing during optimization
- 1-second startup ramp for smooth transition as the noise estimator converges
- Anti-musical-noise temporal gain smoothing
- Decision-directed a priori SNR estimation with speech presence probability
- CPU cost: ~1-2% of one core at 24kHz mono processing

**Dependencies (optional):**
- Linux: `libfftw3-dev` (falls back to built-in FFT without it)
- macOS: `brew install fftw`
- Windows: bundled in `third_party/fftw3/`

Thank you @EI6JGB for this excellent contribution — real DSP engineering with proper overlap-add processing, Hann windowing, and numerical stability.

### Firmware Uploader

Update your radio's firmware directly from Linux — no Windows machine required.

- Settings → Radio Setup → Radio tab → Firmware Update section
- Browse for .ssdr firmware files
- Progress bar with chunked TCP upload
- Confirmation dialog with model verification
- Automatic port fallback (4995 → 42607)

The .ssdr files can be obtained from an existing SmartSDR installation (`C:\ProgramData\FlexRadio Systems\SmartSDR\Updates\`).

### Downloads

Pre-built binaries auto-attach when CI completes (~5 min):

| Platform | File |
|----------|------|
| Linux x86_64 | `AetherSDR-v0.4.7-x86_64.AppImage` |
| Linux ARM | `AetherSDR-v0.4.7-aarch64.AppImage` |
| macOS | `AetherSDR-v0.4.7-macOS-universal.dmg` |
| Windows | `AetherSDR-v0.4.7-Windows-x64.zip` |

### Full Changelog
https://github.com/ten9876/AetherSDR/compare/v0.4.6...v0.4.7

---

## [v0.4.6] — 2026-03-18


### ARRL Band Plan Overlay
- Color-coded sub-band segments on the bottom of the FFT display
- CW (blue), DATA (red), PHONE (orange), Beacon (cyan), Satellite (purple)
- License class shading: Extra (dim), General (medium), Tech (bright)
- White dot markers for spot frequencies (QRP calling, beacons, SSTV, AM calling, etc.)
- Hover over any dot for a tooltip with the frequency and description
- Source: ARRL band chart (rev. 1/16/2026) + Considerate Operator's Frequency Guide

### Waterfall Time Scale
- Time strip on the right edge of the waterfall (0s at top, increasing down)
- Auto-measured row interval — adapts when rate slider changes
- 5-second tick marks, always displayed in seconds

### TX Audio (PC Mic → Radio)
- PC microphone audio now streams to the radio via DAX when mic source = PC
- Auto-starts when selecting PC, stops when switching to BAL/MIC/LINE/ACC
- No longer forces mic_selection=PC on connect — respects radio's saved state

### Waterfall TX Transition
- Immediately falls back to FFT-derived waterfall rows during TX
- Immediately resumes native tiles on RX — no more 2-3 second pause

### Mic Metering
- `met_in_rx=1` enabled on connect for RX mic monitoring
- Gauge suppressed when monitoring is disabled

### Display Settings Persistence
- WNB on/off, WNB level, RF gain saved to settings and restored on connect

### UI Polish
- All overlays consume mouse/wheel events — no accidental VFO tuning
- Black level slider range fixed for proper noise floor suppression
- Rate slider range adjusted (sends 71-100 to radio)

### Linux ARM Support
- aarch64 AppImage built alongside x86_64 on every release

### Downloads

Pre-built binaries auto-attach when CI completes (~5 min):

| Platform | File |
|----------|------|
| Linux x86_64 | `AetherSDR-v0.4.6-x86_64.AppImage` |
| Linux ARM | `AetherSDR-v0.4.6-aarch64.AppImage` |
| macOS | `AetherSDR-v0.4.6-macOS-universal.dmg` |
| Windows | `AetherSDR-v0.4.6-Windows-x64.zip` |

### Full Changelog
https://github.com/ten9876/AetherSDR/compare/v0.4.5...v0.4.6

---

## [v0.4.5] — 2026-03-18


### Multi-Slice Operation
- Color-coded slice markers on spectrum (A=cyan, B=magenta, C=green, D=yellow)
- Click slice badge or VFO line to switch focus
- Independent TX assignment — click grey TX badge in VFO overlay to move TX
- Close (✕) and lock (🔒) buttons floating beside the VFO overlay
- +RX button and right-click "Close Slice" for slice management
- Off-screen slice indicators with frequency readout
- Clean single-slice startup (no race conditions with multiple slices)

### Tracking Notch Filters
- +TNF button creates a notch at the center of the active filter passband
- Right-click spectrum or waterfall to add/remove/configure TNFs
- Drag TNF markers to reposition
- Color-coded: yellow = temporary, green = permanent
- Width (50-500 Hz) and depth (Normal/Deep/Very Deep) via context menu
- Fixed race condition that could crash the radio (#69)

### Audio Controls
- New Audio tab in Radio Setup: line out, headphone, front speaker controls
- PC audio input/output device selection with live switching
- Mic source selector fully wired (MIC/BAL/LINE/ACC/PC)

### Other Improvements
- Dynamic mode list from radio (FDVU, FDVM, future modes auto-detected)
- FreeDV-aware DSP controls
- Saved routed radios auto-probe and reconnect on launch
- Security hardened logging (redacted credentials, restricted permissions)
- AI-assisted feature request guide for non-developer contributors
- Right-click context menu on spectrum and waterfall

### Downloads

Pre-built binaries auto-attach when CI completes (~5 min):

| Platform | File |
|----------|------|
| Linux | `AetherSDR-v0.4.5-x86_64.AppImage` |
| macOS | `AetherSDR-v0.4.5-macOS-universal.dmg` |
| Windows | `AetherSDR-v0.4.5-Windows-x64.zip` |

### Full Changelog
https://github.com/ten9876/AetherSDR/compare/v0.4.0...v0.4.5

---

## [v0.4.0] — 2026-03-17


### New Features

**Tracking Notch Filters (TNF)**
- Right-click spectrum or waterfall to add/remove TNFs
- Drag markers to reposition, adjust width and depth via context menu
- Color-coded: yellow = temporary, green = permanent (survives power cycles)

**SmartLink Remote Operation (beta)**
- Log in with your FlexRadio SmartSDR+ account
- Radio auto-discovered via SmartLink relay server
- Full command channel over TLS — tune, change modes, all controls work remotely
- UDP streaming (FFT, waterfall, audio) in progress — testers welcome

**Manual (Routed) Connection**
- Connect to radios on different subnets/VLANs where UDP broadcast doesn't reach
- Enter IP address, AetherSDR probes the radio and adds it to the list

**Audio Settings Tab**
- Line out gain/mute, headphone gain/mute, front speaker mute
- PC audio input/output device selection (live-switching)

**4-Channel CAT Control**
- Independent rigctld TCP server per slice (ports 4532-4535)
- PTY symlinks per channel (/tmp/AetherSDR-CAT-A through -D)
- PTT auto-switches TX to the keyed channel's slice

**Cross-Platform Builds**
- Linux AppImage, macOS universal DMG (Intel + Apple Silicon), Windows ZIP
- All auto-built via GitHub Actions on tagged releases

**Other Improvements**
- Dynamic mode list from radio (supports FDVU, FDVM, and future modes)
- Mode-aware DSP controls for FreeDV digital voice modes
- Security hardening: redacted credentials in logs, restricted log file permissions
- Right-click context menu on spectrum and waterfall
- Qt 6.2 compatibility fixes for Ubuntu 22.04

### Downloads

Pre-built binaries will be attached automatically when CI completes (~5 min).

| Platform | File |
|----------|------|
| Linux | `AetherSDR-v0.4.0-x86_64.AppImage` |
| macOS | `AetherSDR-v0.4.0-macOS-universal.dmg` |
| Windows | `AetherSDR-v0.4.0-Windows-x64.zip` |

### Full Changelog
https://github.com/ten9876/AetherSDR/compare/v0.3.90-beta...v0.4.0

---

## [v0.3.90-beta] — 2026-03-17


**This is a beta release.** SmartLink TCP command channel is fully working. UDP streaming (FFT, waterfall, audio) needs community testing.

### What's New: SmartLink

Operate your FlexRadio remotely over the internet — no VPN required.

**Working:**
- Log in with your FlexRadio SmartSDR+ account (email/password)
- Radio auto-discovered via SmartLink server
- Full TCP command channel: tune, change modes, adjust filters, all controls work
- Unified radio list shows both local (LAN) and remote (SmartLink) radios
- Stable TLS connection with automatic keepalive

**Needs Testing (call for testers!):**
- UDP streaming (FFT spectrum, waterfall, audio) over WAN
- We confirmed TCP works but UDP packets aren't arriving through carrier NAT (T-Mobile)
- **We need testers with home WiFi remote access** (not mobile hotspot) to verify UDP works with proper port forwarding
- Port forwarding required: TCP 4994 + UDP 4993 to radio's LAN IP

### How to Test SmartLink

1. **At home:** Enable SmartLink in SmartSDR → Settings → SmartLink Setup
2. **Forward ports:** TCP 4994 and UDP 4993 to your radio's LAN IP
3. **Test ports:** Click "Test" in SmartSDR's SmartLink Setup — both should be green
4. **Remote:** Build AetherSDR on a remote machine, log in with your FlexRadio account
5. **Report:** Share results in [Discussions](https://github.com/ten9876/AetherSDR/discussions) — especially if FFT/waterfall/audio works!

### Requirements
- SmartSDR+ subscription (for SmartLink access)
- Port forwarding on home router (TCP 4994, UDP 4993)
- FlexRadio with SmartLink enabled and registered

### Full Changelog
https://github.com/ten9876/AetherSDR/compare/v0.3.0...v0.3.90-beta

---

## [v0.3.0] — 2026-03-17


### Profile Management
Full profile management matching SmartSDR's Profiles menu:

**Profiles menu** (menu bar):
- Dynamic list of global profiles loaded from radio
- Active profile marked with checkmark
- Click to instantly load a global profile

**Profile Manager dialog** (Profiles → Profile Manager...):
- **Global tab**: Load, Save, Delete global profiles with name entry field
- **Transmit tab**: Manage TX profiles
- **Microphone tab**: Manage mic profiles
- **Auto-Save tab**: Toggle auto-save for TX/mic profile changes
- Lists refresh in real-time as the radio processes changes
- Delete confirmation dialog
- Double-click to load

### Critical Bug Fixes

**Profile load crash (SEGV)**: Loading a global profile that changes the slice caused a segfault in `AppletPanel::setSlice()` — the old SliceModel was deleted but its pointer wasn't cleared before disconnect. Fixed by nulling all slice pointers in `onSliceRemoved` before re-wiring.

**No audio after profile load**: After loading a profile, the radio destroys and recreates the slice, invalidating the old `remote_audio_rx` stream. The radio rejected duplicate stream creation with `5000008e`. Fixed by removing the old stream before creating a new one, chained via callback.

**No FFT after profile load**: The `m_panResized` flag wasn't reset when a slice was removed, preventing the panadapter from re-syncing with the radio's new display settings.

### Full Changelog
https://github.com/ten9876/AetherSDR/compare/v0.2.9...v0.3.0

---

## [v0.2.9] — 2026-03-17


### DAX Audio Channels UI
- DAX applet with Enable button, 4 RX level meters, 1 TX level meter
- Slice assignment labels update dynamically from radio state
- TX status shows active slice name during transmit, "Ready" otherwise
- UI ready for PipeWire integration (issue #15)

### rigctld Improvements
- Protocol v1 dump_state with all fields required by WSJT-X/Hamlib
- TCP_NODELAY on client connections for immediate responses
- Command-level debug logging for troubleshooting
- Fixed RX audio loss caused by stale `dax=1` on connect

### Applet Panel Cleanup
- Renamed buttons: VU, RX, TUN, TX, PHNE, P/CW, EQ, DIGI
- Tighter spacing to fit all 8 buttons without clipping

### Bug Fixes
- Reverted DAX PipeWire integration (not ready — TX audio format issues, RX routing conflicts)
- Removed automatic `transmit set dax=1` on connect that was silencing RX audio

### Full Changelog
https://github.com/ten9876/AetherSDR/compare/v0.2.8...v0.2.9

---

## [v0.2.8] — 2026-03-16


### Hamlib rigctld CAT Control
- TCP server on port 4532 — works with WSJT-X, fldigi, N1MM, and any Hamlib NET rigctl client
- Virtual serial port (PTY) at `/tmp/AetherSDR-CAT` for apps that need a serial device
- Autostart options in Settings menu for both rigctld and TTY
- Protocol v1 dump_state with full Hamlib field set
- New CAT Control applet in sidebar with slice selector, enable toggles, port/path configuration
- Credit: rigctld implementation contributed by @pepefrog1234

### Native Waterfall Tiles
- VITA-49 waterfall tiles (PCC 0x8004) now rendered natively with full frame assembly
- FFT spectrum and waterfall are fully decoupled — changing AVG, FPS, or dBm range affects only the FFT
- Waterfall appearance controlled independently via Gain, Black Level, and Rate

### Display Sub-Menu
- New Display panel on left overlay: AVG, FPS, Fill (opacity + color picker), Weighted Average
- Waterfall controls: Gain, Black Level (+ Auto from tile headers), Rate
- All 9 display settings persisted in XML settings file

### Radio Setup Dialog (Complete)
- All 8 tabs implemented: Radio, Network, GPS, TX, Phone/CW, RX, Filters, XVTR
- TX Band Settings popup with per-band power, interlock, and PTT routing
- Transverter management with create/remove/edit

### Other Improvements
- TX stuck fix: MOX unkeys immediately instead of 7-15s delay on fw v1.4.0.0
- Multi-Flex: independent operation alongside SmartSDR/Maestro clients
- XML settings persistence (SSDR-compatible format)
- Network diagnostics, memory channels, spot settings dialogs
- Desktop integration (`.desktop` file, icon, `cmake --install`)
- PC audio TX via DAX stream
- macOS build support

### Full Changelog
https://github.com/ten9876/AetherSDR/compare/v0.2.7...v0.2.8

---

## [v0.2.7] — 2026-03-16


### Native Waterfall Tiles
The waterfall display is now fully decoupled from the FFT spectrum:

- **Native VITA-49 waterfall tiles** (PCC 0x8004) re-enabled with full frame assembly from fragmented packets (2460 bins per frame across ~4 UDP packets)
- **Frequency-based pixel mapping** using tile FrameLowFreq + BinBandwidth with linear interpolation between bins
- **Independent intensity color mapping** via `intensityToRgb()` — waterfall appearance no longer affected by FFT dBm range, averaging, or FPS changes
- **Time-based row interpolation** between consecutive tile rows for smooth scrolling
- **2-second fallback** to FFT-derived waterfall if native tiles stop arriving
- **AutoBlackLevel** from tile headers piped to Display sub-panel Auto button

### Display Settings Persistence
All 9 display controls now save to `~/.config/AetherSDR/AetherSDR.settings` in real-time on every slider, button, or color change:

- `DisplayFftAverage`, `DisplayFftFps`, `DisplayFftFillAlpha`, `DisplayFftFillColor`, `DisplayFftWeightedAvg`
- `DisplayWfColorGain`, `DisplayWfBlackLevel`, `DisplayWfAutoBlack`, `DisplayWfLineDuration`

Settings are restored on app launch with overlay menu sliders synced to saved values.

### Display Control Tuning
- Auto Black Level defaults to **on**
- Black slider range 0–100 (internally scaled to 0–150)
- Gain range expanded to 120 dB at minimum for better dim/contrast control
- Rate slider 50–500ms
- FFT fill color picker changes both spectrum line and gradient fill

### Full Changelog
https://github.com/ten9876/AetherSDR/compare/v0.2.6...v0.2.7

---

## [v0.2.6] — 2026-03-16


### Display Sub-Menu
New Display panel on the left overlay menu with independent FFT and waterfall controls:

**FFT controls:**
- AVG slider (0-100) — radio-side FFT averaging depth
- FPS slider (5-30) — FFT frame rate
- Fill slider (0-100) + color picker — FFT gradient fill opacity and color
- Weighted Average toggle

**Waterfall controls:**
- Gain slider (0-100) — waterfall color intensity/contrast
- Black slider (0-125) + Auto — waterfall black level threshold
- Rate slider (25-500ms) — waterfall scroll speed

All controls send `display pan set` / `display panafall set` commands to the radio and update the local display simultaneously.

### XML Settings System
Migrated all client-side settings from Qt's QSettings INI format to an XML file at `~/.config/AetherSDR/AetherSDR.settings`:
- SSDR-compatible key names (PascalCase, True/False booleans)
- Auto-migration from old QSettings on first launch
- Per-station settings support
- Human-readable, hand-editable XML

### Settings Menu Dialogs
- **Network Diagnostics** — RTT, max RTT, RX/TX rates, packet drop stats
- **Memory Channels** — create/select/remove radio-side memory channels
- **Spot Settings** — spot display preferences (levels, font size, colors)

### Other Improvements
- Unified combo box styling via shared `ComboStyle.h` — all dropdowns now have consistent dark-themed appearance with painted down-arrow
- Auto-connect fix: disconnect now clears saved radio serial, preventing unwanted reconnect on app restart
- Updated STYLEGUIDE.md and CONTRIBUTING.md

### Full Changelog
https://github.com/ten9876/AetherSDR/compare/v0.2.5...v0.2.6

---

## [v0.2.5] — 2026-03-16


### Complete Radio Setup Dialog
All 8 tabs of the Settings → Radio Setup dialog are now fully implemented, matching SmartSDR's configuration interface:

**Radio** — Serial number, callsign, nickname, region, HW version, options, Remote On/multiFLEX toggles

**Network** — IP/mask/MAC display, DHCP/Static IP configuration, Enforce Private IP

**GPS** — Installed status, coordinates, grid square, altitude, satellite tracking, speed, freq error, UTC time

**TX** — Timing delays, TX profile selector, interlock polarity, max power, tune mode, show TX in waterfall. New TX Band Settings popup with per-band RF/Tune power, PTT inhibit, and RCA/ACC interlock checkboxes

**Phone/CW** — Mic BIAS/+20dB boost, PC audio input device selector, CW iambic A/B, paddle swap, CWU/CWL sideband, CWX sync, RTTY mark default

**RX** — GPSDO detection with auto/manual frequency calibration, 10 MHz reference source selector (Auto/TCXO/GPSDO/External), oscillator lock status, binaural audio, mute local when remote

**Filters** — Voice/CW/Digital filter sharpness sliders (Low Latency ↔ Sharp), Auto buttons, low latency digital modes checkbox

**XVTR** — Transverter management with sub-tabs per entry, create/remove, all fields editable (RF/IF/LO freq, LO error, RX gain, max power, RX only)

### Full Changelog
https://github.com/ten9876/AetherSDR/compare/v0.2.4...v0.2.5

---

## [v0.2.4] — 2026-03-16


### Bug Fix: Band Memory Deprecated
Per-band settings persistence has been temporarily disabled pending a full redesign (#9). The automatic band-crossing detection was incorrectly saving state during connection, causing 80m/40m band buttons to tune to 20m instead (#8). 

Band switching still works — clicking a band button tunes to the correct default frequency and mode. Per-band memory (remembering antenna, zoom, filter settings per band) will return in a future release with a more robust design.

### TX Settings Tab
New TX tab in the Radio Setup dialog:
- ACC TX, RCA TX1/TX2/TX3 timing delays
- TX Profile dropdown (populated from radio)
- RCA/Accessory interlock polarity settings
- Max Power limit (editable, 0-100%)
- Tune Mode selector (Single Tone / Two Tone)
- Show TX in Waterfall toggle

### Full Changelog
https://github.com/ten9876/AetherSDR/compare/v0.2.3...v0.2.4

---

## [v0.2.3] — 2026-03-16


### Bug Fix
- **Band menu tuning to wrong frequency** — Clicking 80m or 40m would sometimes tune to 20m instead. The band-crossing detection was incorrectly saving state during initial connection. Fixed by guarding with `m_updatingFromModel` flag. Stale band memory is automatically cleared on first launch of v0.2.3. (#8)

### Settings Dialog
- **Settings menu** added between File and View (Radio Setup, Network, FlexControl, multiFLEX, etc.)
- **Radio Setup** — Radio tab with serial number, callsign, nickname (editable), region, HW version, options, Remote On/multiFLEX toggles
- **Network tab** — IP address, subnet mask, MAC address, DHCP/Static IP configuration with Apply button, Enforce Private IP toggle
- **GPS tab** — Installed status, latitude/longitude, grid square, altitude, satellite tracked/visible, speed, freq error, UTC time

### Other
- macOS build support (Homebrew portaudio link path fix)
- Desktop integration (.desktop file + cmake install rules)
- System UTC clock fallback when no GPS installed
- DAX/Hamlib added to roadmap (#7)

### Full Changelog
https://github.com/ten9876/AetherSDR/compare/v0.2.2...v0.2.3

---

## [v0.2.2] — 2026-03-16


### Bug Fix: Multi-Client (Multi-Flex) Support

AetherSDR now operates correctly when another client (SmartSDR, Maestro) is already connected to the radio. Previously, connecting AetherSDR as a second client would cause:

- Waterfall displaying all red (processing the other client's FFT data)
- Zoom/scale changes replicating between clients
- Both clients tuning in sync (sharing the same slice)

**Fix:** Three-layer filtering by `client_handle`:
1. **Slice ownership** — only tracks slices belonging to our client
2. **Display status** — only processes our panadapter/waterfall status updates
3. **VITA-49 packets** — only decodes FFT/waterfall data from our stream IDs

AetherSDR now creates its own independent slice and panadapter when connecting to a radio with existing clients, enabling true Multi-Flex operation.

### Full Changelog
https://github.com/ten9876/AetherSDR/compare/v0.2.1...v0.2.2

---

