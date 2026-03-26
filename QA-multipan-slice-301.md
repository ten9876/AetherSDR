# QA Test Script: Multi-Pan Slice Lifecycle Fixes (#301)

**Issue:** #301
**Prereqs:** Radio connected, dual-SCU radio (FLEX-8600/6700/6600M) or single-SCU with 2+ pan support

---

## Setup

1. Connect to radio with a single pan (Slice A, 20m USB)
2. Create a second pan: click **+PAN** in the status bar
3. Verify two vertical pans are displayed, each with FFT + waterfall

---

## 1. +RX Creates Slice on Correct Pan

| # | Step | Expected |
|---|------|----------|
| 1.1 | Click in the **bottom** pan to make it active (blue title highlight) | Bottom pan is active |
| 1.2 | Click **+RX** on the **bottom** pan's overlay menu | New slice (B) appears in the **bottom** pan, NOT the top pan |
| 1.3 | Click in the **top** pan to make it active | Top pan is active |
| 1.4 | Click **+RX** on the **top** pan's overlay menu | New slice (C) appears in the **top** pan |
| 1.5 | Click **+RX** on the **bottom** pan's overlay (top pan still active) | New slice created in **bottom** pan — the button routes to its own pan regardless of which pan is active |
| 1.6 | Verify slice VFO flag appears on the correct pan's spectrum | VFO marker, filter passband, and frequency all show on the correct pan |

---

## 2. Pan Title Updates on Slice Deletion

| # | Step | Expected |
|---|------|----------|
| 2.1 | With two pans, note the title bar labels (e.g., "Slice A" on top, "Slice B" on bottom) | Titles show correct slice letters |
| 2.2 | Close Slice B (click X on VFO flag in bottom pan) | Bottom pan title clears (empty or no label) |
| 2.3 | Bottom pan FFT/waterfall still renders (no slice flag) | Spectrum continues without a slice |
| 2.4 | Create a new slice on bottom pan (+RX on bottom) | Title updates to new slice letter (e.g., "Slice C") |
| 2.5 | Close Slice A on top pan | Top pan title clears |
| 2.6 | Close all slices, then create one on top pan | Only the top pan gets a title; bottom stays clear |

---

## 3. CW Decode Panel Follows Active Pan

| # | Step | Expected |
|---|------|----------|
| 3.1 | Switch to CW mode on Slice A (top pan) | CW decode panel appears on top pan |
| 3.2 | Click bottom pan to make it active | CW decode panel moves to bottom pan (if bottom has a CW slice) |
| 3.3 | Delete the bottom pan (close it) | No crash. CW decode panel remains on remaining pan |
| 3.4 | Create a new second pan | CW decode panel appears on active pan |
| 3.5 | Switch active pan back and forth | CW decode text follows the active pan without duplication or loss |
| 3.6 | Switch to USB mode (non-CW) | CW decode panel hides |
| 3.7 | Switch back to CW mode | CW decode panel reappears on active pan |

---

## 4. No Crash on Slice/Pan Manipulation

| # | Step | Expected |
|---|------|----------|
| 4.1 | Create 2 pans, 2 slices (one per pan) | Both display correctly |
| 4.2 | Delete slice from bottom pan | No crash, bottom pan shows empty spectrum |
| 4.3 | Delete slice from top pan | No crash, top pan shows empty spectrum |
| 4.4 | Re-create slices on both pans (+RX on each) | Both slices appear correctly |
| 4.5 | Delete bottom pan entirely (X on pan title) | No crash, single pan remains |
| 4.6 | Create new second pan, add slice, delete slice, delete pan | No crash at any step |
| 4.7 | Rapidly: create slice, delete slice, create slice, delete slice (on second pan) | No crash, no stale titles |
| 4.8 | With 2 pans: delete Slice A, then delete Slice B, then +RX on top pan | No crash, new slice created on top pan |

---

## 5. Edge Cases

| # | Step | Expected |
|---|------|----------|
| 5.1 | Single pan mode: +RX button | Creates slice on the only pan (same as before) |
| 5.2 | Max slices reached (4 on dual-SCU): click +RX | Nothing happens (no crash, button is a no-op) |
| 5.3 | Profile load with different pan count | Pans recreated, titles updated, CW decode follows active pan |
| 5.4 | Disconnect and reconnect | Pan titles, slice assignments, CW decode all restore correctly |
| 5.5 | Split mode: create split on top pan, then +RX on bottom | Split operates on top, new slice on bottom — independent |

---

## Results

| Section | Pass | Fail | Notes |
|---------|------|------|-------|
| 1. +RX Pan Routing | | | |
| 2. Pan Title Updates | | | |
| 3. CW Decode Panel | | | |
| 4. No Crash | | | |
| 5. Edge Cases | | | |

**Tested by:** ________________  **Date:** ________________
