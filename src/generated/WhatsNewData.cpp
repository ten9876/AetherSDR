// Auto-generated from CHANGELOG.md by scripts/gen_whatsnew.py
// Do not edit — changes will be overwritten on next build.
#include "generated/WhatsNewData.h"

namespace AetherSDR {

const std::vector<ReleaseEntry>& whatsNewEntries() {
    static const std::vector<ReleaseEntry> entries = {
        {QStringLiteral("0.7.16"), QStringLiteral("2026-03-31"), QStringLiteral("World-First TGXL Relay Control & Global Band Plans"), {
            {ChangeCategory::Feature, QStringLiteral("Manual TGXL Pi Network Relay Control"), QStringLiteral("Scroll over the C1, L, or C2 relay bars in the Tuner applet to adjust relay positions one step at a time AetherSDR auto-connects directly to the TGXL on TCP port 9010 Real-time relay updates with ~...")},
            {ChangeCategory::Feature, QStringLiteral("Selectable IARU Band Plans"), QStringLiteral("View → Band Plan now includes a region selector: ARRL (US), IARU Region 1, Region 2, Region 3 Region 1: Europe, Africa, Middle East (80m stops at 3.800, 40m at 7.200) Region 2: Americas (80m to 4.0...")},
            {ChangeCategory::Feature, QStringLiteral("multiFLEX Dashboard"), QStringLiteral("Settings → multiFLEX opens a live station dashboard Per-client: station name, program, TX antenna, TX frequency LOCAL PTT status, enable/disable toggle")},
            {ChangeCategory::BugFix, QStringLiteral("Default MTU reduced to 1450"), QStringLiteral("VITA-49 FFT/waterfall packets are 1436 bytes at MTU 1500, exceeding most VPN/SD-WAN tunnel MTUs (WireGuard 1420, OpenVPN 1400) Confirmed fix by user on Cisco Meraki SD-WAN Adjustable in Radio Setup...")},
            {ChangeCategory::BugFix, QStringLiteral("FFTW thread safety for NR2"), QStringLiteral("Added mutex to serialize all FFTW plan creation/destruction Prevents potential crashes when switching DSP modes or regenerating wisdom `fftw_execute()` left unlocked (thread-safe per FFTW spec)")},
            {ChangeCategory::BugFix, QStringLiteral("Removed broken release.yml workflow"), QStringLiteral("GitHub Actions release workflow removed (GITHUB_TOKEN PRs can't trigger CI) Ship/release now handled locally via Claude using gh CLI and GPG-signed tags")},
        }},
        {QStringLiteral("0.7.15"), QStringLiteral("2026-03-30"), QStringLiteral("Digital-Friendly Minimal Mode"), {
            {ChangeCategory::Feature, QStringLiteral("Minimal Mode"), QStringLiteral("Ctrl+M or ↙ button in the title bar collapses AetherSDR to a 260px-wide applet-only strip — just your VFO, RX controls, TX controls, and meters Spectrum, waterfall, and status bar are hidden; title...")},
            {ChangeCategory::Feature, QStringLiteral("multiFLEX Dashboard"), QStringLiteral("Settings → multiFLEX opens a live dashboard showing all connected stations Per-client display: station name, program, TX antenna, TX frequency LOCAL PTT status with checkmarks and \"Enable Local PTT...")},
            {ChangeCategory::Infrastructure, QStringLiteral("CI/CD Pipeline"), QStringLiteral("Docker-based CI builds in ~3.5 minutes (down from 3–19 min variable) CodeQL analysis runs in parallel without blocking merge `git ship` squashes local commits into single auto-merge PR `git release...")},
        }},
        {QStringLiteral("0.7.12"), QStringLiteral("2026-03-29"), QStringLiteral(""), {
            {ChangeCategory::Feature, QStringLiteral("GPG Release Signing"), QStringLiteral("Linux AppImage and source archives are GPG-signed with detached `.asc` signatures SHA256SUMS.txt generated and signed for each release macOS artifacts signed via Apple codesign + notarization (unch...")},
            {ChangeCategory::Feature, QStringLiteral("Commit Signing"), QStringLiteral("All commits to `main` require GPG signatures (branch protection enforced) Contributor setup guide in CONTRIBUTING.md")},
            {ChangeCategory::Feature, QStringLiteral("Configurable Band Plan Size"), QStringLiteral("View → Band Plan submenu: Off, Small (6pt), Medium (10pt), Large (12pt), Huge (16pt) Strip height scales with font size Replaces the previous on/off checkbox")},
            {ChangeCategory::Feature, QStringLiteral("Visual Keyboard Shortcut Manager"), QStringLiteral("View → Configure Shortcuts opens a visual keyboard map dialog Full ANSI keyboard layout with keys color-coded by action category Click any key to assign/change/clear its binding ~45 bindable action...")},
            {ChangeCategory::Feature, QStringLiteral("Click-and-Drag VFO Tuning"), QStringLiteral("Click inside the filter passband and drag left/right to tune the VFO Frequency snaps to step size during drag Filter edge drag (resize) takes priority within ±5px grab zone")},
            {ChangeCategory::Feature, QStringLiteral("Go to Frequency (G key)"), QStringLiteral("Press G to open the VFO direct frequency entry field Pre-fills with current frequency, selected for easy overtype")},
            {ChangeCategory::Feature, QStringLiteral("Space PTT Hold-to-Transmit"), QStringLiteral("Hold Space to transmit, release to return to RX (true momentary PTT) Works regardless of which UI widget has focus Properly syncs TX state with TX applet and status bar")},
            {ChangeCategory::BugFix, QStringLiteral("NR2/RN2/BNR Crash on DSP Mode Switch"), QStringLiteral("SEGV in SpectralNR::process() when switching from BNR to NR2 Root cause: enabled flag was set before the DSP object was constructed; audio arriving during the transition called process() on a null ...")},
            {ChangeCategory::BugFix, QStringLiteral("FlexControl UI Lag"), QStringLiteral("Each encoder step sent a separate TCP command, flooding the radio Fix: coalesce rapid encoder steps into a single command every 20ms")},
            {ChangeCategory::BugFix, QStringLiteral("FlexControl Menu Stub"), QStringLiteral("Settings → FlexControl showed \"not implemented\" Fix: wired to open Radio Setup dialog on the Serial tab")},
            {ChangeCategory::BugFix, QStringLiteral("TNF Crash on +TNF Click"), QStringLiteral("SIGBUS on macOS when clicking +TNF after a panadapter layout change Root cause: rebuildTnfMarkers lambda captured raw SpectrumWidget pointer that became dangling when the pan was removed Fix: captu...")},
            {ChangeCategory::BugFix, QStringLiteral("FlexControl ToggleMox/ToggleTune Stuck in TX"), QStringLiteral("Pressing the FlexControl button a second time didn't toggle TX off Root cause: `applyTransmitStatus()` never parsed `mox=` from radio status, so `isMox()` always returned false Fix: parse `mox=` ke...")},
            {ChangeCategory::BugFix, QStringLiteral("VFO Lock Icon Not Updating"), QStringLiteral("Lock icon on VFO overlay didn't update when toggled via FlexControl or RxApplet Root cause: VfoWidget::setSlice() connected 30+ SliceModel signals but was missing `lockedChanged` Fix: added the mis...")},
            {ChangeCategory::BugFix, QStringLiteral("Mouse Wheel 8x Step on KDE/Cinnamon"), QStringLiteral("Tuning steps were 8x the selected step size on Linux Mint, Cinnamon, and KDE Plasma Root cause: these desktops send high-resolution angleDelta (960 per notch instead of 120) Fix: accumulate angleDe...")},
            {ChangeCategory::BugFix, QStringLiteral("ESC Gain Slider Black Thumb"), QStringLiteral("ESC gain slider thumb was invisible (black) on macOS and Windows Root cause: kSliderStyle only defined horizontal handle rules; ESC gain slider is vertical Fix: added vertical groove and handle QSS...")},
            {ChangeCategory::BugFix, QStringLiteral("RxApplet NR2 Button Not Working"), QStringLiteral("Cycling the RX panel NR button to NR2 didn't enable noise reduction Root cause: the `nr2CycleToggled` handler only synced the VFO button visual but never called `enableNr2WithWisdom()` Fix: NR2 now...")},
            {ChangeCategory::BugFix, QStringLiteral("Split Slice on Wrong Pan"), QStringLiteral("In multi-pan mode, clicking SPLIT could create the TX slice on the wrong panadapter Root cause: split used `m_activePanId` (global) instead of the RX slice's actual pan Fix: use `rxSlice->panId()` ...")},
        }},
        {QStringLiteral("0.7.11"), QStringLiteral("2026-03-29"), QStringLiteral("Platform Notes"), {
            {ChangeCategory::Feature, QStringLiteral("Panadapter Click-to-Spot"), QStringLiteral("Right-click on the panadapter to create a spot marker with callsign, comment, and configurable lifetime Optionally forward spots to your connected DX cluster Right-click on existing spots: Tune, Co...")},
            {ChangeCategory::Feature, QStringLiteral("Per-Slice Record & Play"), QStringLiteral("Record (⏺) and Play (▶) buttons on each VFO flag, matching SmartSDR placement Record button pulses red while recording Play disabled until a recording exists (radio-managed) TX playback: press MOX ...")},
            {ChangeCategory::Feature, QStringLiteral("DAX IQ Streaming"), QStringLiteral("Raw I/Q data from the radio's DDC to external SDR apps (SDR#, GQRX, GNU Radio) 4 IQ channels at 24/48/96/192 kHz via PulseAudio virtual capture devices DIGI applet: per-channel rate dropdown, level...")},
            {ChangeCategory::Feature, QStringLiteral("Applet Panel Collapse"), QStringLiteral("☰ hamburger icon in the status bar toggles the right applet panel Spectrum/waterfall expands to full width when panel is hidden Also available via View → Applet Panel checkbox Custom painted +PAN s...")},
            {ChangeCategory::Feature, QStringLiteral("Drag-Reorderable Applets"), QStringLiteral("Drag applets by their ⋮⋮ grip title bars to reorder in the panel Order persists across sessions View → Reset Applet Order to restore defaults Built on QDrag framework (future-proofs for pop-out to ...")},
            {ChangeCategory::Feature, QStringLiteral("Opus Codec Independent of RADE"), QStringLiteral("SmartLink compressed audio now works without the RADE digital voice module System libopus detected via pkg-config when RADE is disabled Windows: setup-opus.ps1 builds static opus from source")},
            {ChangeCategory::Feature, QStringLiteral("Modeless Dialogs"), QStringLiteral("SpotHub, Radio Setup, and MIDI Mapping dialogs no longer block the main window Interact with the radio while dialogs are open")},
            {ChangeCategory::BugFix, QStringLiteral("BNR Crash Fix"), QStringLiteral("r8brain resampler buffer overflow — BNR output can return up to 9,600 samples at once but the resampler was allocated for 4,096. Increased to 16,384. Found and verified via ASAN.")},
            {ChangeCategory::BugFix, QStringLiteral("AppSettings Corruption Prevention"), QStringLiteral("Atomic save: write to .tmp file, validate XML, rename over original Backup recovery: auto-recover from .bak if main file is corrupt Count guard: refuse to save if settings count dropped below half ...")},
            {ChangeCategory::BugFix, QStringLiteral("RadioModel Shutdown Use-After-Free"), QStringLiteral("Disconnect all signals from RadioConnection before member destruction Prevents accessing destroyed XVTR map and slice models during teardown Found via AddressSanitizer (ASAN) build")},
            {ChangeCategory::BugFix, QStringLiteral("Opus SSE Alignment"), QStringLiteral("Copy input data to alignas(16) buffers before opus_encode/opus_decode Prevents SEGV on SSE-optimized RADE opus builds")},
            {ChangeCategory::BugFix, QStringLiteral("Spot Label Deconfliction"), QStringLiteral("Re-scan all placed labels after each nudge to properly stack across all levels Spots no longer overlap when multiple labels are close in frequency")},
            {ChangeCategory::BugFix, QStringLiteral("Other Fixes"), QStringLiteral("Duplicate spot-to-cluster sends fixed (disconnect before reconnect in wirePanadapter) DIGI applet section headers use distinct label style (not confused with draggable title bars) DIGI applet layou...")},
        }},
        {QStringLiteral("0.7.4"), QStringLiteral("2026-03-26"), QStringLiteral("VOX Support — #253"), {
            {ChangeCategory::BugFix, QStringLiteral("Full Changelog"), QStringLiteral("")},
        }},
        {QStringLiteral("0.4.0"), QStringLiteral("2026-03-17"), QStringLiteral("Downloads"), {
            {ChangeCategory::Feature, QStringLiteral("Tracking Notch Filters (TNF)"), QStringLiteral("Right-click spectrum or waterfall to add/remove TNFs Drag markers to reposition, adjust width and depth via context menu Color-coded: yellow = temporary, green = permanent (survives power cycles)")},
            {ChangeCategory::Feature, QStringLiteral("SmartLink Remote Operation (beta)"), QStringLiteral("Log in with your FlexRadio SmartSDR+ account Radio auto-discovered via SmartLink relay server Full command channel over TLS — tune, change modes, all controls work remotely UDP streaming (FFT, wate...")},
            {ChangeCategory::Feature, QStringLiteral("Manual (Routed) Connection"), QStringLiteral("Connect to radios on different subnets/VLANs where UDP broadcast doesn't reach Enter IP address, AetherSDR probes the radio and adds it to the list")},
            {ChangeCategory::Feature, QStringLiteral("Audio Settings Tab"), QStringLiteral("Line out gain/mute, headphone gain/mute, front speaker mute PC audio input/output device selection (live-switching)")},
            {ChangeCategory::Feature, QStringLiteral("4-Channel CAT Control"), QStringLiteral("Independent rigctld TCP server per slice (ports 4532-4535) PTY symlinks per channel (/tmp/AetherSDR-CAT-A through -D) PTT auto-switches TX to the keyed channel's slice")},
            {ChangeCategory::Feature, QStringLiteral("Cross-Platform Builds"), QStringLiteral("Linux AppImage, macOS universal DMG (Intel + Apple Silicon), Windows ZIP All auto-built via GitHub Actions on tagged releases")},
            {ChangeCategory::Feature, QStringLiteral("Other Improvements"), QStringLiteral("Dynamic mode list from radio (supports FDVU, FDVM, and future modes) Mode-aware DSP controls for FreeDV digital voice modes Security hardening: redacted credentials in logs, restricted log file per...")},
        }},
        {QStringLiteral("0.3.0"), QStringLiteral("2026-03-17"), QStringLiteral("Profile Management"), {
            {ChangeCategory::BugFix, QStringLiteral("Profile load crash (SEGV)"), QStringLiteral("")},
            {ChangeCategory::BugFix, QStringLiteral("No audio after profile load"), QStringLiteral("")},
            {ChangeCategory::BugFix, QStringLiteral("No FFT after profile load"), QStringLiteral("")},
        }},
        {QStringLiteral("0.2.2"), QStringLiteral("2026-03-16"), QStringLiteral("Full Changelog"), {
            {ChangeCategory::BugFix, QStringLiteral("Fix:"), QStringLiteral("")},
        }},
    };
    return entries;
}

} // namespace AetherSDR
