# FlexRadio TX Audio Signal Path

Documented from FLEX-8600 firmware v4.1.5 meter definitions.
All meters are source `TX-` unless noted. Units are dBFS unless noted.

## Client-side TX DSP (AetherSDR, before the radio)

Since v0.8.15 AetherSDR applies its own client-side DSP to the TX
stream between the mic/VirtualAudioBridge and the VITA-49 encoder.
The radio receives the already-shaped signal and treats it identically
to any other PC-mic input (enters at SC_MIC, meter 26).

```
Mic capture (QAudioSource) / DAX TX audio
  │
  ▼
┌─────────────────────────────┐
│  Client EQ   (ClientEq)     │  ◄── 10-band parametric, #1660
│  Client CMP  (ClientComp)   │  ◄── Pro-XL-style compressor + limiter, #1661
│                             │     Chain order: CMP→EQ (default) or
│                             │     EQ→CMP, user-selectable in the
│                             │     floating editor
└─────────┬───────────────────┘
          │
          ▼  (meters: ClientComp inputPeak/outputPeak/GR/limiterActive,
          │         ClientEq FFT tap)
          │
          ▼
     VITA-49 encode → UDP → radio
```

Post-encoding, the radio sees this stream as any other PC-mic source
and runs it through the firmware TX chain below.

## DAX TX Routing

The `DaxTxLowLatency` AppSettings flag controls how DAX TX audio is
encoded into VITA-49 packets before being sent to the radio.

| Mode | Packet Class Code | Format | `transmit set dax` | Use case |
|------|-------------------|--------|-------------------|----------|
| **Radio-native** (default) | PCC 0x0123 | int16 mono | `dax=1` | Standard DAX/TCI TX |
| **Low-latency** | PCC 0x03E3 | float32 stereo (FlexLib DAXTXAudioStream) | `dax=0` | External FreeDV client via virtual audio cable |

### Platform behavior

- **Linux** (`HAVE_PIPEWIRE` defined): The menu item is **not shown**.
  FreeDV audio routing is handled at the OS level by PipeWire/PulseAudio,
  so the low-latency float32 VITA-49 route has no utility. The radio-native
  `dax=1` route is always used. Any stale `DaxTxLowLatency=True` from prior
  builds is migrated to `False` on startup (#1681).

- **macOS / Windows**: The **Settings → Low-Latency DAX (FreeDV)** toggle
  is available for users who route an external FreeDV client through a
  virtual audio cable (Loopback on macOS, VB-Cable on Windows).

### Why `dax=0` is harmful

When `DaxTxLowLatency=True`, the radio is told `transmit set dax=0`. This
routes the physical microphone to the TX modulator and **silently discards
all `dax_tx` VITA-49 packets**. The relay clicks, the TCI TX level meter
shows audio, but no RF is produced. This was the root cause of the
WSJT-X/JTDX "no RF output over TCI" failure mode (fixed in #1680).

AetherSDR's internal RADE/FreeDV engine (`AudioEngine::sendModemTxAudio`)
bypasses this flag entirely — it feeds the modulator directly without
consulting `DaxTxLowLatency`.

## Firmware Signal Flow

```
PC Mic Audio (Opus via remote_audio_tx) — arrives with client-side
                                          EQ + CMP already applied
  │
  ▼
┌─────────────────────────────┐
│  Opus Decode                │
│  (radio firmware)           │
└─────────┬───────────────────┘
          │
          ▼
┌─────────────────────────────┐
│  SC_MIC (meter 26)          │  ◄── "Signal strength of MIC output"
│  src=TX-, unit=dBFS         │      PC audio enters TX chain here
│  fps=10                     │
└─────────┬───────────────────┘
          │
          │   Hardware Mic (BAL/LINE/ACC)
          │     │
          │     ▼
          │   ┌───────────────────────┐
          │   │  CODEC ADC            │
          │   │  MICPEAK (meter 1)    │  ◄── "Signal strength of MIC output in CODEC"
          │   │    src=COD-, fps=40   │      Peak, hardware mic only
          │   │  MIC (meter 2)        │  ◄── "Average Signal strength of MIC output in CODEC"
          │   │    src=COD-, fps=20   │      Average, hardware mic only
          │   └───────┬───────────────┘
          │           │
          ▼           ▼
┌─────────────────────────────┐
│  Mic Level + Mic Boost      │  ◄── "transmit set mic_level=XX"
│  (radio-side gain)          │      "transmit set mic_boost=X"
└─────────┬───────────────────┘
          │
          ▼
┌─────────────────────────────┐
│  CODEC (meter 24)           │  ◄── "Signal strength of CODEC output"
│  src=TX-, fps=10            │      Combined output after mic gain
└─────────┬───────────────────┘
          │
          ▼
┌─────────────────────────────┐
│  TX AGC (input)             │
│  TXAGC (meter 25)           │  ◄── "Signal strength post AGC/FIXED GAIN"
│  src=TX-, fps=10            │
└─────────┬───────────────────┘
          │
          ▼
┌─────────────────────────────┐
│  Equalizer (8-band)         │  ◄── "eq txsc" command
│  AFTEREQ (meter 27)         │  ◄── "Signal strength after the EQ"
│  src=TX-, fps=20            │
└─────────┬───────────────────┘
          │
          ▼
┌─────────────────────────────┐
│  Speech Processor           │  ◄── "transmit set speech_processor_enable/level"
│  (Compander + Clipper)      │
│  COMPPEAK (meter 28)        │  ◄── "Signal strength before CLIPPER (Compression)"
│  src=TX-, fps=20            │      Used for P/CW applet compression gauge
└─────────┬───────────────────┘
          │
          ▼
┌─────────────────────────────┐
│  TX Filter 1                │  ◄── "transmit set lo=XX hi=XX"
│  SC_FILT_1 (meter 29)       │  ◄── "Signal strength after Filter 1"
│  src=TX-, fps=20            │
└─────────┬───────────────────┘
          │
          ▼
┌─────────────────────────────┐
│  SW ALC (SSB Peak)          │
│  ALC (meter 30)             │  ◄── "Signal strength after SW ALC (SSB Peak)"
│  src=TX-, fps=10            │      Used for P/CW applet ALC indicator
└─────────┬───────────────────┘
          │
          ▼
┌─────────────────────────────┐
│  RM TX AGC                  │  ◄── Remote TX AGC (PC audio level normalization?)
│  RM_TX_AGC (meter 31)       │  ◄── "Signal strength after RM TX AGC"
│  src=TX-, fps=10            │
└─────────┬───────────────────┘
          │
          ▼
┌─────────────────────────────┐
│  TX Filter 2                │
│  SC_FILT_2 (meter 32)       │  ◄── "Signal strength after Filter 2"
│  src=TX-, fps=10            │
└─────────┬───────────────────┘
          │
          ▼
┌─────────────────────────────┐
│  TX AGC (final)             │
│  TX_AGC (meter 33)          │  ◄── "Signal strength after TX AGC"
│  src=TX-, fps=10            │
└─────────┬───────────────────┘
          │
          ▼
┌─────────────────────────────┐
│  Ramp (key shaping)         │  ◄── CW/digital key envelope
│  B4RAMP (meter 34)          │  ◄── "Signal strength before the ramp"
│  src=TX-, fps=10            │
│  AFRAMP (meter 35)          │  ◄── "Signal strength after the ramp"
│  src=TX-, fps=10            │
└─────────┬───────────────────┘
          │
          ▼
┌─────────────────────────────┐
│  Power Control / Atten      │  ◄── "transmit set rfpower=XX"
│  POST_P (meter 36)          │  ◄── "After all processing, before power attenuation"
│  src=TX-, fps=10            │
│  ATTN_FPGA (meter 37)       │  ◄── "After Fine Tune, HW ALC, SWR Foldback (FPGA)"
│  src=TX-, fps=10            │
└─────────┬───────────────────┘
          │
          ▼
┌─────────────────────────────┐
│  PA (Power Amplifier)       │
│  FWDPWR (meter 8)           │  ◄── "RF Power Forward" (dBm → watts)
│  src=TX-, fps=20            │
│  REFPWR (meter 9)           │  ◄── "RF Power Reflected" (dBm)
│  src=TX-, fps=20            │
│  SWR (meter 10)             │  ◄── "RF SWR"
│  src=TX-, fps=20            │
│  PATEMP (meter 11)          │  ◄── "PA Temperature" (degC)
│  src=TX-, fps=0             │
│  HWALC (meter 3)            │  ◄── "Voltage at Hardware ALC RCA Plug" (dBFS)
│  src=TX-, fps=20            │
└─────────┬───────────────────┘
          │
          ▼
        ANT1/ANT2 → Antenna (or external amplifier/tuner)
```

## Meter Summary Table

| ID | Source | Name | Unit | FPS | Description | Used in AetherSDR |
|----|--------|------|------|-----|-------------|-------------------|
| 1 | COD- | MICPEAK | dBFS | 40 | Hardware mic peak level | P/CW mic gauge (BAL/LINE/ACC) |
| 2 | COD- | MIC | dBFS | 20 | Hardware mic average level | P/CW mic gauge (BAL/LINE/ACC) |
| 3 | TX- | HWALC | dBFS | 20 | HW ALC voltage | P/CW ALC indicator |
| 8 | TX- | FWDPWR | dBm | 20 | Forward power | TX applet, S-Meter power, Tuner |
| 9 | TX- | REFPWR | dBm | 20 | Reflected power | — |
| 10 | TX- | SWR | SWR | 20 | Standing wave ratio | TX applet, Tuner |
| 11 | TX- | PATEMP | degC | 0 | PA temperature | Status bar |
| 24 | TX- | CODEC | dBFS | 10 | CODEC output (post mic gain) | — |
| 25 | TX- | TXAGC | dBFS | 10 | Post AGC/fixed gain | — |
| 26 | TX- | SC_MIC | dBFS | 10 | MIC output (PC audio entry point) | P/CW mic gauge (PC mic) |
| 27 | TX- | AFTEREQ | dBFS | 20 | Post equalizer | — |
| 28 | TX- | COMPPEAK | dBFS | 20 | Pre-clipper (compression) | P/CW compression gauge |
| 29 | TX- | SC_FILT_1 | dBFS | 20 | Post TX filter 1 | — |
| 30 | TX- | ALC | dBFS | 10 | Post SW ALC (SSB peak) | P/CW ALC indicator |
| 31 | TX- | RM_TX_AGC | dBFS | 10 | Post remote TX AGC | — |
| 32 | TX- | SC_FILT_2 | dBFS | 10 | Post TX filter 2 | — |
| 33 | TX- | TX_AGC | dBFS | 10 | Post final TX AGC | — |
| 34 | TX- | B4RAMP | dBFS | 10 | Before key ramp | — |
| 35 | TX- | AFRAMP | dBFS | 10 | After key ramp | — |
| 36 | TX- | POST_P | dBFS | 10 | Post all processing, pre power control | — |
| 37 | TX- | ATTN_FPGA | dBFS | 10 | Post FPGA attenuation | — |

## Notes

- **PC mic path**: Audio enters at SC_MIC (meter 26), bypassing the CODEC ADC entirely.
  The COD- meters (MICPEAK/MIC) only respond to hardware mic inputs.
- **mic_level**: Radio-side gain applied after CODEC/SC_MIC, before TXAGC. Affects both
  hardware and PC mic paths.
- **met_in_rx=1**: Tells the radio to meter incoming remote_audio_tx during RX for
  VOX detection and P/CW mic level display.
- **Meter IDs are dynamic**: The radio assigns meter IDs on connection. The IDs shown
  here are from one session — match by name, not by ID.
- **Unit conversion**: dBm meters (FWDPWR) need watts = 10^(dBm/10)/1000.
  SWR is raw. degC/degF use raw/64.0f. Volts/Amps use raw/1024.0f.

## Future: TX Audio Path Meter Panel

A dedicated panel showing all TX-chain meters as a vertical stack of horizontal
bars would let users visualize the impact of each processing stage:

```
SC_MIC    ████████████░░░░░░░░░░  -12 dBFS   (mic input)
CODEC     █████████████░░░░░░░░░  -10 dBFS   (post mic gain)
TXAGC     █████████████░░░░░░░░░  -10 dBFS   (post AGC)
AFTEREQ   ████████████░░░░░░░░░░  -12 dBFS   (post EQ)
COMPPEAK  ██████████████░░░░░░░░   -8 dBFS   (pre-clipper)
SC_FILT_1 ████████████░░░░░░░░░░  -12 dBFS   (post filter 1)
ALC       ███████████░░░░░░░░░░░  -14 dBFS   (post SW ALC)
POST_P    █████████░░░░░░░░░░░░░  -18 dBFS   (final output)
FWDPWR    ████████████████░░░░░░   85 W       (RF power)
```

This would be invaluable for diagnosing TX audio issues — you can immediately
see where in the chain the signal is being attenuated or clipped.
