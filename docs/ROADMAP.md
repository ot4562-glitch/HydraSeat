# HydraSeat Development Roadmap

---

## Phase 0: Research & Foundation (Complete)
- [x] Establish C++20 / Qt 6 project workspace architecture
- [x] Configure `.agents/AGENTS.md` autonomous iteration rules
- [x] Document Windows input/display architectural design
- [x] Evaluate Interception, HidHide, ViGEmBus, IDD and related systems
- [x] Record related-product/source research and clean-room contribution policy

Research:
- [PHASE0_RESEARCH.md](PHASE0_RESEARCH.md)
- [RELATED_SYSTEMS_RESEARCH.md](RELATED_SYSTEMS_RESEARCH.md)
- [CLEAN_ROOM_POLICY.md](CLEAN_ROOM_POLICY.md)

---

## Phase 1: Hardware Detection (Complete)
- [x] Implement `HardwareDetector` module
- [x] Detect and list physical display monitors and likely virtual displays
- [x] Differentiate distinct physical keyboards through Raw Input plus SetupAPI/ConfigMgr identity
- [x] Differentiate distinct mice and touchpads
- [x] Differentiate XInput and generic HID controllers
- [x] Build CLI hardware detection tool for verification
- [x] Add deterministic identity tests and Windows/MSVC CI

---

## Phase 2: Seat Composition & Assignment UI (Complete)
- [x] Build drag-and-drop Seat assignment UI (current Win32 prototype; Qt 6 migration remains UI polish)
- [x] Implement `WorkspaceManager` / Seat state machine
- [x] Support multiple displays per Seat with an explicit primary display
- [x] Support keyboard, mouse, controller and audio ownership
- [x] Enforce exclusive physical-device assignment by default with explicit sharing
- [x] Save and load validated Seat profiles to transactional UTF-8 JSON

---

## Phase 3: Input Compatibility & Isolation (Current)

Design:
- [x] Implement Win32 Raw Input sink window and event decoding
- [x] Identify individual physical keyboard and mouse events
- [x] Add Seat routing-policy and backend interfaces
- [x] Research ProtoInput, Universal Split Screen, Nucleus Co-op, HidHide, devreorder, Duo and ASTER behavior
- [x] Define capability-planned, profile-driven architecture and recovery model
- [x] Implement compatibility capability vocabulary, backend descriptors and deterministic planner
- [x] Add `hydra_plan` diagnostics CLI and profile templates

Feasibility gates:
- [ ] Gate A — two-window observation harness with hot-plug diagnostics
- [ ] Gate B — explicit per-device routing to two HydraSeat-owned target windows
- [ ] Gate C — per-process Raw Input, key-state, cursor and focus virtualization in HydraSeat test processes
- [ ] Gate D — optional HidHide session-cloak experiment with watchdog and crash rollback
- [ ] Gate E — two different non-anti-cheat games with measured zero cross-seat input bleed

Exit condition:
- [ ] Route input to target processes without normal Windows input merging for a documented set of game profiles
- [ ] Demonstrate clean rollback to ordinary single-user Windows behavior

Detailed design: [PHASE3_INPUT_ISOLATION_DESIGN.md](PHASE3_INPUT_ISOLATION_DESIGN.md)

---

## Phase 4: Display Routing & Virtual Display Management
- [ ] Implement Seat-local display coordinate spaces
- [ ] Keep Seat-owned windows inside assigned physical monitor groups
- [ ] Integrate or build an IDD/IddCx-compatible local virtual-display adapter
- [ ] Auto-create virtual display outputs for supported secondary devices
- [ ] Add display-driver installation, signing, recovery and latency tests

---

## Phase 5: Two-Seat Gaming MVP
- [ ] Launch two different target games/apps on two Seat display groups
- [ ] Assign independent keyboard/mouse/controller and audio devices
- [ ] Verify low latency and zero input bleeding for supported profiles
- [ ] Verify restart, device reconnect and crash recovery
- [ ] Publish an explicit compatibility matrix rather than claiming universal support

---

## Phase 6: Game Launcher & Profile Manager
- [ ] Steam, Epic, EA, GOG and custom executable profiles
- [ ] Typed compatibility-profile editor
- [ ] Backend availability and risk report before launch
- [ ] Process/child tracking with Windows Job Objects where compatible
- [ ] Automatic Seat restoration on game launch
- [ ] Per-application audio endpoint routing

---

## Phase 7: Seat Shell & Extensions
- [ ] Seat-local launcher/taskbar experience
- [ ] Seat-local cursor rendering and display boundaries
- [ ] Community SDK for compatibility, controller, audio and display adapters
- [ ] Versioned adapter protocol and third-party signing/trust policy
