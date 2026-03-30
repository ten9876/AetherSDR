# Contributing to AetherSDR

Thanks for your interest in AetherSDR! We're building a native SmartSDR
client for FlexRadio on Linux (and macOS), and community contributions are
welcome. This document explains how to get involved and what we expect from
contributors.

---

## Quick Start for Contributors

1. Browse [open issues](https://github.com/ten9876/AetherSDR/issues) —
   issues labeled `good first issue` are great starting points.
2. Fork the repo and create a feature branch from `main`.
3. Implement the fix or feature (one issue per PR).
4. Open a pull request referencing the issue number.

---

## Issue Tracker Workflow

All work is tracked via GitHub issues. Each issue represents a single
feature, enhancement, or bug. **Pick one issue, implement it, submit a PR.**

### Issue Labels

| Label | Meaning |
|-------|---------|
| `feature` | New capability not yet in AetherSDR |
| `enhancement` | Improvement to an existing feature |
| `bug` | Something that's broken |
| `good first issue` | Self-contained, well-scoped — great for new contributors |
| `help wanted` | Larger features where we'd especially welcome help |
| `GUI` | User interface work (Qt6 widgets, layouts, styling) |
| `spectrum` | Panadapter, waterfall, FFT display |
| `audio` | Audio engine, streaming, DAX |
| `protocol` | SmartSDR TCP/UDP protocol, VITA-49 |
| `priority: high` | Core functionality blocking other work |
| `priority: medium` | Important but not blocking |
| `priority: low` | Nice to have, implement when convenient |

### Claiming an Issue

Comment on the issue to let others know you're working on it. If you stop
working on it, comment again so someone else can pick it up.

---

## How to Contribute

### Reporting Bugs

- Open a GitHub issue with a clear title and description.
- Include your OS/distro, Qt version, radio model, and firmware version.
- Attach relevant logs (`~/.config/AetherSDR/aethersdr.log`), screenshots, or Wireshark
  captures if possible.
- Check existing issues first to avoid duplicates.

### Suggesting Features

- Open a GitHub issue tagged `feature` or `enhancement`.
- Describe the SmartSDR feature you'd like replicated, or the new capability
  you're proposing.
- Reference the SmartSDR UI or FlexLib behavior where applicable —
  screenshots of the Windows client are very helpful.

### Submitting Code

**Development tool:** AetherSDR is developed using [Claude Code](https://claude.com/claude-code)
as the primary development environment. The entire codebase — its architecture, naming
conventions, signal/slot patterns, and code style — has been built and maintained through
AI-assisted development. To ensure consistency and minimize integration friction, we
**strongly encourage all contributors to use Claude Code** for writing and modifying code.

PRs that don't follow the project's established patterns (naming, RAII, signal/slot wiring,
`AppSettings` instead of `QSettings`, etc.) will require significant rework before merging.
Using Claude Code with the project's `CLAUDE.md` context naturally produces code that
integrates cleanly.

1. **Fork the repo** and create a feature branch from `main`.
2. **Use Claude Code** for development — it has full context of the codebase via `CLAUDE.md`.
3. **One issue per PR.** Keep changes focused and reviewable.
4. **Follow the coding conventions** outlined below.
5. **Test your changes** against a real FlexRadio if possible, or describe
   how you tested without hardware.
6. **Open a pull request** against `main` with a clear description of what
   changed and why. Reference the issue: `Fixes #42`.

---

## Project Architecture

Understanding the codebase structure will help you find where to make changes.

```
src/
├── main.cpp              — Entry point, logging setup
├── core/
│   ├── RadioDiscovery    — UDP 4992 broadcast listener
│   ├── RadioConnection   — TCP 4992 command/control (V/H/R/S/M protocol)
│   ├── CommandParser     — Stateless protocol line parser
│   ├── PanadapterStream  — VITA-49 UDP: FFT, waterfall, meters, audio
│   └── AudioEngine       — QAudioSink (RX) + QAudioSource (TX via DAX)
├── models/
│   ├── RadioModel        — Central state: connection, slices, panadapter
│   ├── SliceModel        — Per-slice: freq, mode, filter, DSP, RIT/XIT
│   ├── TransmitModel     — TX state: power, profiles, ATU, CW, mic
│   ├── MeterModel        — Meter definitions + VITA-49 value conversion
│   ├── TunerModel        — 4o3a TGXL external tuner state
│   ├── EqualizerModel    — 8-band TX/RX equalizer
│   └── BandDefs.h        — Shared band table (freq ranges, defaults)
└── gui/
    ├── MainWindow        — Top-level window, wires everything together
    ├── SpectrumWidget    — FFT + waterfall + overlays + mouse interaction
    ├── VfoWidget         — Floating VFO info panel with tabbed sub-menus
    ├── SpectrumOverlayMenu — Left-side overlay (Band/ANT/DSP/DAX menus)
    ├── FrequencyDial     — 9-digit MHz display with click/scroll tuning
    ├── ConnectionPanel   — Radio list + connect/disconnect
    ├── AppletPanel       — Right sidebar toggle buttons + applet stack
    ├── RxApplet          — Full RX controls (filter, AGC, DSP, RIT/XIT)
    ├── TxApplet          — TX controls (power, ATU, profiles, MOX/TUNE)
    ├── TunerApplet       — TGXL tuner gauges and controls
    ├── PhoneCwApplet     — Mic/CW controls
    ├── PhoneApplet       — VOX, AM carrier, DEXP, TX filter
    ├── EqApplet          — 8-band graphic equalizer
    ├── SMeterWidget      — Analog S-meter gauge
    ├── RadioSetupDialog  — 8-tab settings dialog
    ├── SpotHubDialog     — Unified spot manager (DX Cluster, RBN, WSJT-X, POTA, Spot List, Display)
    └── HGauge.h          — Reusable horizontal gauge (header-only)
```

### Key Patterns

- **Model → Radio**: Model setters emit `commandReady(cmd)` →
  `RadioModel` sends command to radio via TCP.
- **Radio → Model**: Status messages (`S` lines) arrive via TCP →
  `RadioModel::onStatusReceived()` → routes to model's `applyStatus()`.
- **Model → GUI**: Models emit signals (e.g., `frequencyChanged()`) →
  GUI widgets update via slots.
- **GUI → Model**: GUI widgets call model setters directly. Use
  `QSignalBlocker` or `m_updatingFromModel` guards to prevent echo loops.
- **VITA-49 → GUI**: `PanadapterStream` routes by PCC (Packet Class Code):
  `0x8003` (FFT), `0x8004` (waterfall), `0x8002` (meters),
  `0x03E3`/`0x0123` (audio).

### Multi-Flex (Multi-Client) Filtering

When another client (SmartSDR, Maestro) is connected, AetherSDR filters by
`client_handle` at three layers:
1. **Slice ownership** — only track slices with our `client_handle`
2. **Panadapter status** — only process our `display pan` updates
3. **VITA-49 packets** — only display FFT/waterfall from our stream IDs

If your change touches `RadioModel`, `SliceModel`, or `PanadapterStream`,
ensure it respects these ownership filters.

---

## SmartSDR Protocol Reference

The protocol is ASCII over TCP (port 4992) with VITA-49 binary over UDP.

| Prefix | Direction | Meaning |
|--------|-----------|---------|
| `V` | Radio→Client | Firmware version |
| `H` | Radio→Client | Client handle (hex) |
| `C` | Client→Radio | Command: `C<seq>\|<cmd>\n` |
| `R` | Radio→Client | Response: `R<seq>\|<hex_code>\|<body>` |
| `S` | Radio→Client | Status: `S<handle>\|<object> key=val ...` |
| `M` | Radio→Client | Informational message |

Response code `0` = success. `50001000` = command not supported on this
firmware. `5000002D` = invalid parameter value.

### FlexLib Reference

The `reference/FlexLib/` directory contains the C# source from FlexRadio's
official library. Use it to understand protocol behavior, but **do not
copy-paste code** — write clean-room C++ implementations based on observed
behavior.

Key files for protocol research:
- `Slice.cs` — slice properties, filter limits, mode handling
- `Radio.cs` — connection, subscriptions, command dispatch
- `Panadapter.cs` — panadapter properties, RF gain, WNB
- `Transmit.cs` — TX state, profiles, CW, mic
- `Meter.cs` — meter definitions and unit conversion
- `APD.cs` — adaptive pre-distortion
- `TNF.cs` — tracking notch filters
- `CWX.cs` — CW keyer and memories
- `DVK.cs` — digital voice keyer

---

## Coding Conventions

### C++ Style

- **C++20 / Qt6** — use modern idioms (`std::ranges`, `auto`, structured
  bindings, `constexpr`).
- **RAII everywhere.** No naked `new`/`delete`. Use Qt parent-child
  ownership or smart pointers.
- **No `using namespace std;`** in headers.
- **Qt signals/slots** over raw callbacks for cross-object communication.
- **`QSignalBlocker`** to prevent feedback loops when updating UI from
  model state.
- **Keep classes small** and single-responsibility.
- **No compiler warnings.** Build passes with `-Wall -Wextra`.

### Naming

- Classes: `PascalCase` (e.g., `SliceModel`, `SpectrumWidget`)
- Methods/functions: `camelCase` (e.g., `setFrequency()`, `applyStatus()`)
- Member variables: `m_camelCase` (e.g., `m_frequency`, `m_sliceId`)
- Constants: `UPPER_SNAKE` for macros, `PascalCase` for `constexpr`
- Signals: past tense or descriptive (e.g., `frequencyChanged`, `commandReady`)

### File Organization

- One class per `.h`/`.cpp` pair.
- Headers use `#pragma once`.
- Group includes: project headers first, then Qt headers, then std headers.
- Implementation in `.cpp`, not headers (except small inline/constexpr and
  header-only utilities like `HGauge.h`).

### Dark Theme

All GUI follows the dark theme: background `#0f0f1a`, text `#c8d8e8`,
accent `#00b4d8`, borders `#203040`. Match existing widget styling. Use
`rgba()` for transparency. Avoid hard-coding colors — reference existing
widgets for consistency.

### Commit Messages

- Imperative mood: "Add band stacking" not "Added band stacking".
- First line under 72 characters.
- Reference issues: `Fixes #42` or `Closes #42`.
- Blank line then longer description if needed.

### Commit Signing

All commits to `main` must be GPG-signed. Unsigned commits will be rejected
by branch protection. Each contributor uses their own personal GPG key — do
NOT use the project release signing key for commits.

#### Quick Setup

1. **Generate a GPG key** (if you don't already have one):
   ```bash
   gpg --quick-gen-key "Your Name <your-email@example.com>" ed25519 sign 0
   ```

2. **Add the key to your GitHub account:**
   ```bash
   # Copy your public key
   gpg --armor --export <KEY_ID>
   ```
   Paste the output at [GitHub → Settings → SSH and GPG keys → New GPG key](https://github.com/settings/gpg/new).

3. **Configure git to sign commits:**
   ```bash
   git config --global user.signingkey <KEY_ID>
   git config --global commit.gpgsign true
   ```

4. **Verify it works:**
   ```bash
   git commit --allow-empty -m "Test signed commit"
   git log --show-signature -1
   ```

Your commits will show a green "Verified" badge on GitHub.

#### Troubleshooting

If `gpg` prompts for a passphrase but hangs in a terminal, set the GPG TTY:

```bash
echo 'export GPG_TTY=$(tty)' >> ~/.bashrc   # or ~/.zshrc
```

For GUI-based passphrase entry, install `pinentry-gnome3` or `pinentry-qt`.

---

## What We Will Not Accept

- **Wine/Crossover workarounds.** The goal is a fully native client.
- **Vendored dependencies.** Use system packages or CMake's `find_package`.
- **Copied proprietary code.** Do not copy-paste from SmartSDR or FlexLib
  binaries. Clean-room implementations based on observed protocol behavior
  and the FlexLib C# source (in `reference/`) are fine.
- **Changes that break the core RX path.** If your change touches
  `RadioModel`, `SliceModel`, `PanadapterStream`, or `AudioEngine`, test
  the full receive chain: discovery → connect → FFT display → audio output.
- **Large reformatting PRs.** Fix style only in files you're already modifying.

---

## Development Setup

### Dependencies

**Arch Linux:**
```bash
sudo pacman -S qt6-base qt6-multimedia cmake ninja pkgconf portaudio
```

**Ubuntu/Debian/Linux Mint:**
```bash
sudo apt install qt6-base-dev qt6-multimedia-dev cmake ninja-build \
  pkg-config gstreamer1.0-pulseaudio gstreamer1.0-plugins-base
```

**macOS (Homebrew):**
```bash
brew install qt@6 ninja portaudio cmake pkg-config
```

### Build

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j$(nproc)
./build/AetherSDR
```

### Install (optional)

```bash
sudo cmake --install build
```

### Logging

Debug logs are written to `~/.config/AetherSDR/aethersdr.log`
(overwritten each launch). Include relevant log excerpts in bug reports and PRs.

### Hardware

A FlexRadio FLEX-6000 or FLEX-8000 series radio is the reference target.
Tested on FLEX-8600 running SmartSDR v4.1.5. If you don't have hardware,
you can still contribute to UI, protocol parsing, and model logic — just
note in your PR how you tested.

---

## AI-Assisted Feature Requests

**You don't need to be a developer to contribute a great feature request.** If you
have an idea for AetherSDR but aren't sure how to write it up in a way that's
useful to developers, you can use [Claude.ai](https://claude.ai) (free) to help
you structure it. This saves us time and gets your feature implemented faster.

### How it works

1. Go to [claude.ai](https://claude.ai) and start a new conversation.
2. Paste the prompt below, filling in the parts in `[brackets]` with your idea.
3. Claude will generate a structured feature request.
4. Copy the output and paste it into a [new GitHub issue](https://github.com/ten9876/AetherSDR/issues/new).

### The prompt

Copy and paste this into Claude.ai:

```
Before responding, please read the AetherSDR project repository at
https://github.com/ten9876/AetherSDR to understand the project's
architecture, existing features, and current roadmap.

I want to request a feature for AetherSDR, a Linux-native Qt6/C++20 client
for FlexRadio transceivers. It uses the FlexLib API over TCP/UDP.

Before writing the feature request, please check the existing open issues at:
https://github.com/ten9876/AetherSDR/issues

Search for keywords related to my idea. If you find an existing issue that
covers the same thing, tell me the issue number and title instead of writing
a new one — I'll go add my +1 and comments there.

If no duplicate exists, please write a GitHub issue for this feature request.
Include:
1. A clear title
2. A "What" section describing what the feature does from the user's perspective
3. A "Why" section explaining why this is useful (what problem it solves)
4. A "How Other Clients Do It" section — if this feature exists in other
   SDR applications, describe how it works there (screenshots welcome)
5. A "Suggested Behavior" section with specific details about how it should
   work in AetherSDR (what the user clicks, what they see, what happens)
6. A "Protocol Hints" section — if you know the FlexLib API calls involved,
   list them. If not, just say "Unknown — needs research"

Here is my feature idea:

[Describe your feature here in plain English. Be as specific as you can.
For example: "I want to be able to right-click on a signal in the waterfall
and have it automatically look up the callsign on QRZ.com" or "I want the
S-meter to show a bar graph option instead of just the analog needle" or
"I want AetherSDR to remember which band I was on for each mode so when
I switch from USB to CW it goes back to where I was on CW"]
```

### What makes a good feature request

- **Be specific.** "Better audio" is hard to act on. "Add a noise gate with
  adjustable threshold to the TX audio path" is actionable.
- **Describe the problem, not just the solution.** Tell us *why* you want it.
  "I run WSJT-X on a separate slice and need independent CAT control" helps
  us understand the use case.
- **Reference SmartSDR.** If the feature exists in SmartSDR, tell us. A
  screenshot of the Windows client is worth a thousand words.
- **One feature per issue.** Don't bundle "add CW memories AND fix the
  waterfall colors AND add a band map" into one request.

### Example

A user might type into Claude.ai:

> I want AetherSDR to support band stacking. When I change bands, I want it
> to remember the frequency, mode, and filter I was using on that band. When
> I come back to the band, it should restore those settings. SmartSDR does
> this with a "band stack" feature.

Claude would produce a nicely structured issue with title, sections, and
protocol hints that our development team (including our AI pair-programmer)
can immediately work from.

---

## Notes for AI Agents (Claude, Copilot, etc.)

If you are an AI agent contributing to this project, read
[CLAUDE.md](CLAUDE.md) first — it is the authoritative project context
document with full architecture details, protocol specification, VITA-49
packet formats, and implementation patterns.

### Key rules for AI contributors:

1. **Read before writing.** Always read existing code before modifying.
   Use the architecture section above to find the right file.
2. **One issue per PR.** Don't bundle unrelated changes.
3. **Match existing patterns.** Look at how similar features are
   implemented (e.g., look at `RxApplet` before building a new applet,
   look at `SliceModel` before adding a new model property).
4. **Protocol commands must be verified.** If you're adding a new command,
   check `reference/FlexLib/` for the correct syntax. Commands that return
   `50001000` are not supported on the current firmware — handle gracefully.
5. **Dark theme.** All GUI must match the dark theme. Copy color values
   from existing widgets.
6. **No feedback loops.** When a GUI widget updates from a model signal,
   use `QSignalBlocker` on the widget to prevent it from sending the same
   value back to the radio.
7. **Guard model updates.** `MainWindow` uses `m_updatingFromModel = true`
   when updating GUI from model state. Check this flag before emitting
   commands back to the radio.
8. **Multi-Flex safety.** Filter all status updates and VITA-49 packets by
   `client_handle`. Do not process data from other clients' slices or
   panadapters.
9. **Test the RX chain.** Any change to core classes must not break:
   discovery → connect → subscribe → FFT display → audio output.
10. **Firmware version comments.** When adding protocol commands, comment
    which firmware version you tested against (e.g., `// fw v4.1.5`).
11. **Use AppSettings, NEVER QSettings.** All client-side persistence uses
    `AppSettings::instance()` (`src/core/AppSettings.h`), which stores an
    XML file at `~/.config/AetherSDR/AetherSDR.settings`. Key names use
    PascalCase. Booleans are `"True"` / `"False"` strings. See CLAUDE.md
    for usage examples. `QSettings` is only used in the one-time migration
    code in `AppSettings.cpp`.

### AI-to-AI Debugging

If you're an AI agent working on a contributor's fork and run into an issue
that requires coordination with the maintainer's AI (e.g., your feature
works locally but breaks on merge, or you need protocol clarification from
the radio logs), open a GitHub issue with:

1. **Title**: Clear description of the problem
2. **Your analysis**: What you've investigated, what you think the cause is
3. **Relevant log output**: Paste the key lines from `aethersdr.log`
4. **Code references**: Which files/functions are involved
5. **Your proposed fix**: If you have one, describe it

The maintainer's Claude instance monitors issues and will respond with
analysis, log requests, or fixes. This is the standard async communication
channel between AI agents working on AetherSDR. Keep the discussion
technical and focused — treat it like a code review conversation.

### File quick reference for AI agents:

| Task | Start here |
|------|-----------|
| Add a new slice property | `SliceModel.h/.cpp` — add getter/setter/signal, parse in `applyStatus()` |
| Add a new TX property | `TransmitModel.h/.cpp` — same pattern as SliceModel |
| Add a new GUI control | Look at `RxApplet.cpp` for control patterns, `VfoWidget.cpp` for tab panels |
| Add a new applet | Copy `EqApplet` as template, register in `AppletPanel` |
| Add a new overlay sub-menu | See `SpectrumOverlayMenu.cpp` — `buildBandPanel()` as template |
| Parse a new status object | `RadioModel::onStatusReceived()` — add routing for the object name |
| Add a new meter display | `MeterModel` already parses all meters — wire to a gauge widget |
| Add a new Radio Setup tab | `RadioSetupDialog.cpp` — follow existing tab patterns |
| Add a new spot source | `SpotHubDialog.cpp` — follow DX Cluster/RBN/WSJT-X/POTA tab patterns |
| Fix a protocol command | Check `reference/FlexLib/` for correct syntax, test with radio logs |

---

## Code of Conduct

Be respectful, constructive, and patient. Ham radio has a long tradition of
helping each other learn — bring that spirit here. We're all building
something together. See our full [Code of Conduct](CODE_OF_CONDUCT.md).

73 de KK7GWY

## License

By contributing to AetherSDR, you agree that your contributions will be
licensed under the [GNU General Public License v3.0](LICENSE).
