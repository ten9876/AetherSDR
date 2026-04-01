# QA Test Script: Keyboard Shortcut Manager (#239)

**Branch:** `feature/keyboard-shortcuts`
**Build:** `cmake --build build -j$(nproc)`
**Precondition:** AetherSDR connected to a FlexRadio (any model)

---

## 1. Default Shortcuts (Backward Compatibility)

Verify all existing shortcuts still work before touching the dialog.

- [ ] Enable **View → Keyboard Shortcuts** (checkbox)
- [ ] **Right arrow** → frequency increases by 1 step
- [ ] **Left arrow** → frequency decreases by 1 step
- [ ] **Shift+Right** → frequency increases by 10 steps
- [ ] **Shift+Left** → frequency decreases by 10 steps
- [ ] **Up arrow** → AF gain increases by 5
- [ ] **Down arrow** → AF gain decreases by 5
- [ ] **T** → toggles MOX on/off
- [ ] **M** → toggles mute on/off
- [ ] **]** → step size increases to next value
- [ ] **[** → step size decreases to previous value
- [ ] **L** → toggles tune lock (verify VFO lock icon updates)
- [ ] **Space** → activates TX (MOX)
- [ ] All shortcuts **disabled** when View → Keyboard Shortcuts is unchecked
- [ ] All shortcuts **suppressed** when typing in a text field (frequency direct entry, SpotHub search, etc.)

---

## 2. Open Configure Shortcuts Dialog

- [ ] **View → Configure Shortcuts...** opens the dialog
- [ ] Dialog is modal (main window blocked while open)
- [ ] Dark theme matches the rest of the application
- [ ] Visual keyboard renders all keys with correct labels
- [ ] Bound keys are color-coded (T = red/TX, Right = cyan/Frequency, etc.)
- [ ] Unbound keys are dark grey
- [ ] Legend strip at bottom of keyboard shows all categories with correct color swatches
- [ ] Dialog is resizable — keyboard scales with window size

---

## 3. Keyboard Map Interaction

- [ ] **Hover** over a key → key brightens, cursor changes to pointing hand
- [ ] **Click** a key → key gets cyan border (selected)
- [ ] Selected key info panel updates: shows key name, current action, category
- [ ] **Click** an unbound key → action dropdown shows "(none)"
- [ ] **Click** a bound key (e.g. T) → action dropdown shows "[TX] MOX Toggle"

---

## 4. Assign a New Binding

- [ ] Click an unbound key (e.g. **G**)
- [ ] Select an action from the dropdown (e.g. "[Audio] AF Gain Up")
- [ ] Key on the keyboard map turns green (Audio category color)
- [ ] Action label appears on the key
- [ ] Action table row for "AF Gain Up" updates to show "G" in Current Key column
- [ ] Close dialog
- [ ] Press **G** → AF gain increases (verify the new binding works)

---

## 5. Reassign an Existing Binding

- [ ] Open Configure Shortcuts dialog
- [ ] Click key **T** (currently MOX Toggle)
- [ ] Change action to "[Audio] Mute Toggle"
- [ ] Confirm conflict dialog appears: "Key [T] is currently bound to 'MOX Toggle'. Reassign it?"
- [ ] Click **Yes**
- [ ] Key T now shows "Mute Toggle" in green (Audio)
- [ ] MOX Toggle row in the table now shows empty Current Key
- [ ] Close dialog
- [ ] Press **T** → toggles mute (not MOX)

---

## 6. Clear a Binding

- [ ] Open Configure Shortcuts dialog
- [ ] Click a bound key
- [ ] Click **Clear** button
- [ ] Key returns to dark grey (unbound)
- [ ] Action table row shows empty Current Key
- [ ] Close dialog, verify the key no longer triggers any action

---

## 7. Reset to Default

- [ ] After modifying bindings in tests 4-6, open Configure Shortcuts dialog
- [ ] Click a modified key
- [ ] Click **Reset to Default** → key returns to its original default binding
- [ ] Click **Reset All to Defaults** → confirmation dialog appears
- [ ] Click **Yes** → all keys return to original defaults
- [ ] Close dialog, verify all original shortcuts work

---

## 8. Action Table

- [ ] Action table shows all ~45 actions with columns: Action, Category, Current Key, Default Key
- [ ] **Search filter**: type "mox" → table filters to show only MOX-related actions
- [ ] **Category filter**: select "TX" → table shows only TX actions
- [ ] **Category filter**: select "All" → table shows all actions
- [ ] Click a row in the table → corresponding key highlights on the keyboard (if bound)
- [ ] Unbound actions show empty Current Key column

---

## 9. Persistence

- [ ] Assign a new binding (e.g. **G** → AF Gain Up)
- [ ] Close the dialog
- [ ] Quit AetherSDR
- [ ] Relaunch AetherSDR
- [ ] Open Configure Shortcuts → **G** still shows AF Gain Up
- [ ] Press **G** → AF gain increases (binding persists)

---

## 10. Band / Mode Shortcuts (Unbound by Default)

- [ ] Open Configure Shortcuts dialog
- [ ] Assign **2** → "20m" (Band category)
- [ ] Assign **4** → "40m" (Band category)
- [ ] Close dialog
- [ ] Press **2** → radio tunes to 20m (14.225 MHz area)
- [ ] Press **4** → radio tunes to 40m (7.200 MHz area)
- [ ] Open dialog, assign **U** → "USB" (Mode category)
- [ ] Close dialog, press **U** → mode switches to USB

---

## 11. Edge Cases

- [ ] **F1-F12 keys**: If DVK or CWX panel is visible, F-keys should trigger DVK/CWX (panel-scoped), not global shortcuts
- [ ] **Modifier keys** (Shift, Ctrl, Alt): clicking these on the keyboard map should select them but not crash
- [ ] **Rapid dialog open/close**: open and close Configure Shortcuts several times → no crash or memory leak
- [ ] **Dialog while disconnected**: all shortcuts that require a radio connection should be no-ops when disconnected

---

## 12. Multi-Slice Interaction

- [ ] With two slices active, assign a key to "Next Slice"
- [ ] Press the key → active slice switches
- [ ] Press again → switches back
- [ ] Assign a key to "Split Toggle"
- [ ] Press → split mode activates (new TX slice created)
- [ ] Press again → split mode deactivates

---

## Pass Criteria

All checkboxes checked. No crashes. Bindings persist across restarts. Default behavior unchanged when no customization is made.
