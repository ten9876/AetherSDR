# Understanding Data Modes

## Why Digital Operation Feels More Complicated

Voice operation can often be understood as "microphone in, speaker out." Digital operation adds more moving parts:

- a radio-control path
- a receive-audio path
- a transmit-audio path
- sometimes an IQ path
- sometimes a single integrated control protocol such as TCI

The goal is not to memorize every acronym. The goal is to keep each path visible and predictable.

## The Digital Signal Paths Inside AetherSDR

### Control path

This is how outside software reads frequency, mode, PTT, and other radio state. In AetherSDR that path is usually one of these:

- CAT over TCP
- CAT over virtual TTY or PTY
- TCI over WebSocket

### Receive audio path

This is how digital software hears the radio. In AetherSDR that is usually DAX receive audio.

### Transmit audio path

This is how digital software sends audio back into the radio path. In AetherSDR that is usually the DAX transmit path.

### IQ path

This is for applications that need raw IQ instead of demodulated audio. AetherSDR supports DAX IQ streaming with selectable rates.

## The Windows and Controls That Matter Most

### Slice or VFO area

The slice tells you which frequency, mode, and filter you are actually working on. If your digital program is talking to the wrong slice, everything else will feel wrong even if CAT appears to be working.

For digital work, pay special attention to:

- active slice letter
- mode
- transmit assignment
- digital offset
- DAX channel

### DIGI applet

This is the control center for external-software integration. It exposes:

- `Enable TCP`
- `Enable TTY`
- CAT base port
- per-channel CAT status for channels `A` through `D`
- TCI enable and port
- DAX enable
- DAX receive and transmit level controls

Keep this applet visible during setup. It answers the question, "What endpoints is AetherSDR offering to the rest of the station?"

### Spectrum `DAX` overlay

The panadapter overlay also has a `DAX` panel. This is where you manage DAX and IQ channel choices in the same visual area where you are choosing slices and frequency. For IQ-based applications, this is an important bridge between the display and the external software.

### `Radio Setup...`

For digital workflows, the most relevant setup tabs are:

- `Audio`
- `Phone/CW`
- `Filters`
- `Serial`

Those tabs affect routing, compression, digital behavior, and external control integration more than casual receive-only operation does.

## Important Terms

### DIGU and DIGL

These are digital sideband modes. In practice, they give digital software a predictable transmit and receive environment without the voice-processing assumptions of SSB phone modes.

### DAX

DAX is the digital audio exchange between AetherSDR and outside applications. Think of it as the software patch cable between the radio session and your digital program.

### CAT

CAT is rig control. A digital program uses it to follow frequency changes, key the transmitter, read mode, or command changes back to AetherSDR.

### TTY or PTY

This is the serial-style version of CAT. Some older or more rigid software expects a serial-looking endpoint instead of a TCP socket.

### TCI

TCI is a more integrated network protocol that can carry control, audio, IQ, CW, and spot information through one server connection when the client application supports it.

### DAX IQ

DAX IQ is raw IQ streaming rather than demodulated audio. Use it when the external program needs baseband data rather than normal receive audio.

## How AetherSDR Maps Channels

The DIGI applet is designed around four CAT channels, labeled `A`, `B`, `C`, and `D`. They track the common four-slice workflow and line up naturally with slice letters.

### CAT over TCP

When `Enable TCP` is on:

- AetherSDR starts four rigctld-style TCP servers.
- The base port defaults to `4532`.
- The four channels use the base port and the next three ports.

That means a default setup typically looks like:

- channel `A`: `4532`
- channel `B`: `4533`
- channel `C`: `4534`
- channel `D`: `4535`

### CAT over TTY or PTY

When `Enable TTY` is on, AetherSDR exposes serial-style endpoints for the same channel model. The DIGI applet shows the actual path names, which are useful when an external application can talk only to a serial-looking device.

### TCI

TCI is separate from the four-channel CAT model. It is one server, with a default port of `50001`.

### DAX audio

The DIGI applet exposes four receive DAX channels and one transmit path. Keep your channel numbering consistent with your slice usage so you do not have to rediscover the routing every time you operate.

## First Digital Setup

Use this sequence the first time you integrate a new digital application:

1. Connect to the radio.
2. Create or select the slice you want for digital work.
3. Set the slice to the correct digital mode, usually `DIGU` or `DIGL`.
4. Confirm that the correct slice owns transmit.
5. Open the `DIGI` applet and place it where you can see it continuously.
6. Turn on `Enable TCP` or `Enable TTY` depending on what the external application expects.
7. If the application supports TCI, decide whether you want TCI instead of separate CAT and audio routing.
8. Enable DAX if your workflow uses digital audio channels.
9. Set or confirm the DAX channel for the slice you are using.
10. Open the external digital application and match its radio-control and audio settings to the endpoints shown by AetherSDR.
11. Test receive first.
12. Test transmit only after receive, frequency tracking, and slice assignment all make sense.

## Practical Workflow by Application Type

### CAT plus audio applications

Programs such as WSJT-X, fldigi, JS8Call, Winlink, and VARA often want:

- one CAT endpoint
- one receive audio device
- one transmit audio device

In that workflow:

1. pick the correct CAT port or TTY path
2. pick the correct DAX receive audio source
3. pick the correct DAX transmit audio destination
4. confirm that frequency changes follow in both directions

### TCI-capable applications

Some applications can use TCI and avoid the split between CAT and separate audio routing. In that workflow:

1. enable the TCI server
2. use the TCI port shown in the DIGI applet
3. confirm the application is truly using TCI rather than a second fallback CAT path

### IQ-driven applications

If the external application needs raw IQ:

1. open the panadapter `DAX` overlay
2. select the IQ channel and rate you want
3. point the external application at the matching IQ source

## Keep the Window Readable During Digital Sessions

Digital problems are easier to solve when the right controls stay visible. A good digital layout usually keeps these items on screen:

- the active slice and its mode
- the DIGI applet
- the TX applet
- the status bar network and TX indicators
- at least one visible panadapter

If you collapse too much into `Minimal Mode` too early, you may hide the clues you need to diagnose a routing problem.

## Gain Staging

Bad gain staging creates many fake "protocol" problems. If decode quality is poor or your transmitted signal looks dirty:

1. reduce the problem to one slice and one digital application
2. set moderate DAX levels
3. avoid clipping in the digital program
4. avoid excessive transmit processing in the voice path
5. test again with a short transmission

Digital software is often much less tolerant of overdriven audio than casual voice monitoring.

## Common Mistakes

### The software connects, but the wrong slice moves

Cause:

- the external program is attached to a different CAT channel than the slice you are watching

Fix:

- match the CAT channel, slice letter, and DAX channel intentionally instead of assuming they were paired automatically

### Receive works, but transmit goes nowhere

Cause:

- wrong transmit slice
- wrong DAX transmit path
- wrong audio device selected in the external program

Fix:

- confirm TX ownership first, then confirm the application's transmit-audio destination

### CAT works, but there is no decode audio

Cause:

- the radio-control path is correct but the DAX receive audio path is not

Fix:

- leave CAT alone and correct only the receive-audio source

### Audio is present, but the application does not tune the radio

Cause:

- the DAX path is right, but CAT, TTY, or TCI is wrong or blocked

Fix:

- leave audio alone and fix only the control path

### Everything works locally, but remote digital operation is choppy

Cause:

- WAN latency, audio compression, or too much visual and streaming load

Fix:

- simplify the session, reduce extra panadapters, and re-check audio and network quality before changing every digital setting

## A Safe Troubleshooting Order

When a digital setup fails, isolate one layer at a time:

1. verify the slice and mode
2. verify CAT, TTY, or TCI control
3. verify receive audio
4. verify transmit audio
5. verify transmit ownership
6. verify IQ only if the workflow actually needs it

This order is faster than changing ports, channels, modes, and audio devices all at once.
