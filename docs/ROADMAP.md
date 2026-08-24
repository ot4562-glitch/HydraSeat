# HydraSeat Development Roadmap

---

## Phase 0: Research & Foundation (Complete)
- [x] Establish C++20 / Qt 6 project workspace architecture
- [x] Configure `.agents/AGENTS.md` autonomous iteration rules
- [x] Document Windows input/display architectural design
- [x] Evaluate open-source libraries: Interception, HidHide, ViGEmBus, Virtual Display Driver (IDD)

---

## Phase 1: Hardware Detection (Complete)
- [x] Implement `HardwareDetector` module
- [x] Detect and list physical display monitors & virtual displays
- [x] Differentiate distinct physical keyboards by HID device path (`RAWINPUTHEADER.hDevice`)
- [x] Differentiate distinct mice and touchpads
- [x] Differentiate controllers (XInput, DualSense, HID)
- [x] Build CLI hardware detection tool for verification

---

## Phase 2: Seat Composition & Assignment UI (Complete)
- [x] Build drag-and-drop Seat assignment UI (current Win32 prototype; Qt 6 migration is UI polish, not a data-model blocker)
- [x] Implement `WorkspaceManager` / Seat composition state machine with multi-display ownership and explicit primary display
- [x] Save and load validated Seat profiles to UTF-8 JSON

---

## Phase 3: Raw Input Router Core (Current)
- [x] Implement Win32 Raw Input sink window & hook loop
- [x] Identify individual physical keyboard events independently (routing/isolation enforcement still in progress)
- [x] Identify individual physical mouse events independently (routing/isolation enforcement still in progress)
- [ ] Route input events to target window handles without OS merging (Phase 3 isolation backend skeleton added; zero-bleed enforcement not yet implemented)

---

## Phase 4: Display Routing & Virtual Display Management
- [ ] Integrate Virtual Display Driver (IDD) / SpaceDesk / Sunshine adapters
- [ ] Auto-create virtual display outputs for secondary devices (e.g. Android phone)

---

## Phase 5: Prototype Game Support (MVP)
- [ ] Launch single local co-op target game with 2 independent displays & input devices
- [ ] Verify low latency, zero input bleeding between workspaces

---

## Phase 6: Game Launcher & Profile Manager
- [ ] Game profiles (Steam, Epic, EA, GOG, Custom Executables)
- [ ] Automatic workspace restoration on game launch

---

## Phase 7: Plugin SDK & Extensions
- [ ] Community plugin SDK for custom controllers, display drivers, and game hooks
