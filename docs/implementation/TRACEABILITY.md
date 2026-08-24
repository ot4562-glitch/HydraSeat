# Product Requirement Traceability

This document maps the user's intended product behavior to the design decisions, implementation packets, and evidence gates that must prove it. It prevents a technically convenient subproject from replacing the actual multiseat goal.

## Requirement matrix

| Requirement | Design decisions | Primary packets | Required evidence |
| --- | --- | --- | --- |
| One physical Windows PC feels like multiple local PCs | D-001, D-003, D-004, D-030 | P4-RUN-01, P5-LAUNCH-01, P7-SHELL-01 | Two active Seats, host-owned runtime, distinct shell/process/display/input state |
| Seat, not monitor, is the unit | D-001, D-002 | P2-SEAT-01, P4-DIS-02, P7-SHELL-01 | Seat 1 owns LG+Samsung while Seat 2 owns BenQ |
| One Seat may own multiple monitors | D-002, D-013, D-015 | P4-DIS-01/02/03, P4-WIN-02 | Mixed-DPI multi-monitor physical acceptance and hot-plug recovery |
| Different games/apps run concurrently | D-014, D-028 | P3-E-03, P5-MVP-02, P6 provider/profile packets | Exact two-target compatibility entries and session evidence |
| Keyboard/mouse do not bleed into the other Seat | D-005, D-006, D-008, D-011 | P3-API/RAW packets, P3-D-02, P3-E-03 | Zero measured cross-Seat key/button/movement events |
| Both games may believe they are active | D-005, D-006 | P3-API-03, controlled probes, P3-E profiles | Per-process focus/capture API results and game profile evidence |
| Independent visible cursors per Seat | D-015 | P3-API-03, P7-CURSOR-01 | Two cursor overlays within disjoint Seat display groups and latency report |
| Controllers are assigned per Seat/game | D-017 | P3-CTRL-01/02, P5-CTRL-01 | API-specific state/vibration and zero cross-routing |
| Audio output/input is assigned per Seat | D-018 | P5-AUD-01/02 | Two process audio streams at declared endpoints; unsupported cases block |
| Seat owns processes/windows, not just coordinates | D-014, D-019 | P4-PROC-01, P4-WIN-01/02, P4-POL-01 | Process-tree/window attribution and unrelated-window noninterference |
| Apps stay on assigned display group | D-002, D-014, D-015, D-016 | P4-DIS-02/03, P4-WIN-02 | Fullscreen/borderless/DPI/hot-plug placement and restore evidence |
| Environment has launcher/taskbar/wallpaper feel | D-001, D-004, D-023 | P7-SHELL/LAUNCH/TASK/DESK packets | Two independent Seat shells across multi-monitor layouts |
| Core works without VM/RDP/streaming | D-003, D-030 | P3–P5 core path | Supported direct local-display compatibility entries |
| UI can close while runtime continues | D-004, D-019, D-032 | P4-RUN-01, P4-IPC-01, P4-CTRL-01, P7-REC-01 | UI kill/restart while host/session state remains correct |
| Whole-machine controls stay on the Management Seat | D-031 | P4-CTRL-01, P7-SHELL-01 | Console opens on Seat 1 primary by default, falls back visibly, and other Seats cannot stop/reconfigure by default |
| User can return the split PC to ordinary Windows with one clear operation | D-033 | P4-CTRL-02, P8-RESET-01 | Stop/Return transaction verifies rollback; all monitors/input return to normal without reboot |
| User can reconfigure monitors/input with a few guided steps | D-034 | P4-CTRL-02, Phase 6 profile UI | Active session safely stops, editor opens on Management Seat, validation/save/start uses a new plan |
| User can choose manual, hidden background, or automatic validated startup | D-032 | P8-BOOT-01 | Reboot/logon matrix proves Manual, BackgroundIdle, and AutoActivateValidatedSession behavior |
| UI/UX and end-user README are available in English, Korean, and Simplified Chinese | D-035 | P7-I18N-01, P7-A11Y-01, P10-UX-01 | `en-US`/`ko-KR`/`zh-CN` catalog parity, three-language critical-flow acceptance, and README language/version/status parity |
| Startup is silent and ordinary use has no repeated UAC | D-004, D-020 | P8-PRIV-01, P8-BOOT-01 | Standard-user logon/reboot acceptance |
| Dangerous operations recover automatically | D-009, D-020 | P8-WATCH-01, P8-RESET-01, P8-JOURNAL-01 | Host/helper kill, timeout, reset, safe mode, no orphan state |
| Device hiding occurs only after replacement path works | D-008, D-009 | P3-REC-01, P3-D-01/02 | Guarded physical cloak test with spare input/expiry/watchdog |
| No anti-cheat/DRM/protected-process bypass | D-007 | Planner/profile/security packets | Protected profiles block or remain observation-only |
| Existing related products guide architecture lawfully | D-024, D-025, D-030 | Research/clean-room docs, P10-LIC-01 | Provenance, license, third-party notices, no unclear copied code |
| Profiles and providers make sessions repeatable | D-005, D-012 | P6-SCHEMA/MIG/CATALOG/PROV/PLAN packets | Deterministic immutable plan hash and provider regression fixtures |
| Compatibility claims are exact, not universal | D-028 | P5-COMPAT-01, P10-SCOPE/COMPAT packets | Versioned machine-readable matrix and current evidence |
| Optional adapters/extensions cannot bypass policy | D-024 | P8-TRUST-01, Phase 9 SDK/package/capability/RPC packets | Package trust, permission denial, out-of-process failure isolation |
| Installer/update/uninstall are reversible | D-020, D-024 | P8-INST-01, P8-UPD-01 | Clean-machine install/update rollback/uninstall acceptance |
| Performance is good enough for gaming | D-021, D-029 | P3-MET-01, P5-MET-01, P10-PERF-01 | p50/p95/p99 latency and resource budgets on reference topology |
| Product survives long use and repeated failures | D-020, D-029 | P8-SOAK-01, P10-REL-01 | Soak/fault/reboot/resource-leak campaign |
| Release is legally and technically distributable | D-025 | P10-LIC-01, P10-PKG-01, P10-RC/GA packets | License, notices, SBOM, signatures, provenance, verified artifacts |

## Product-defining acceptance scenario

The minimum scenario that proves the original user intent is:

```text
One Windows PC

Seat 1
- LG display (primary)
- Samsung display (secondary)
- keyboard A
- mouse A
- optional controller A
- headset A
- target/game A
- Seat 1 shell/cursor/process group
- Management Seat = Seat 1
- HydraSeat control console opens on LG primary when requested

Seat 2
- BenQ display (primary)
- keyboard B
- mouse B
- optional controller B
- speakers B
- target/game B, different from target A
- Seat 2 shell/cursor/process group
```

Required observable behavior:

1. Both targets run at the same time.
2. Target A remains on LG/Samsung; target B remains on BenQ.
3. Keyboard/mouse/controller A produce no input state in target B.
4. Keyboard/mouse/controller B produce no input state in target A.
5. Each target sees the focus/cursor behavior declared by its profile.
6. Audio routes to the declared endpoint or the session refuses to start.
7. Disconnect/reconnect and one target restart behave according to profile.
8. The HydraSeat control console opens on the Management Seat primary display, may close/reopen without losing runtime authority, and other Seats do not receive whole-machine Stop/Reconfigure controls by default.
9. `Stop / Return to Windows` restores ordinary one-PC Windows input/display/audio behavior without reboot.
10. `Reconfigure` safely returns to ordinary Windows, edits assignments on the Management Seat, validates/saves a new plan, and may start it again.
11. Manual, BackgroundIdle, and AutoActivateValidatedSession startup modes behave exactly as selected and unsafe auto-activation falls back to idle.
12. A report records versions, topology, backends, latency, drops, bleed, and rollback.

This scenario is reached by P3-E-03, P4 physical display/window acceptance, P5-MVP-02, P5-HOT-01, P7 shell packets, and Phase 8 recovery/productization. No earlier synthetic test satisfies the complete requirement.

## Negative traceability

The following results are useful but do not prove the product:

| Result | What it proves | What it does not prove |
| --- | --- | --- |
| Two windows launch | Process/window creation | Device isolation, display ownership, focus, recovery |
| Raw Input sees two keyboards | Physical source observation | Other games cannot see the input |
| `PostMessage` reaches a target | Diagnostic message routing | Raw Input/polled state virtualization |
| Two adapter contexts differ | Process-local state model | Unmodified game calls the adapter |
| Device is hidden | Visibility change | Replacement input delivery or complete suppression |
| XInput controller detected | Device availability | Per-process slot routing/vibration isolation |
| Window moved to a monitor | One placement action | Process ownership, persistence, hot-plug recovery |
| Audio endpoint assigned in profile | Desired configuration | Actual per-process audio routing |
| Synthetic CI passes | Deterministic controlled logic | Physical hardware/game compatibility |
| One game works once | Initial experimental evidence | Version-independent support or two-game zero bleed |

## Roadmap change rule

When a product requirement changes:

1. update this matrix;
2. update or add a decision in [DECISIONS.md](DECISIONS.md);
3. update affected phase packet dependencies/invariants/acceptance;
4. update [STATUS.md](STATUS.md) if the critical path changes;
5. add/withdraw compatibility evidence where applicable;
6. run the roadmap validator.

A code change must not silently redefine a requirement.
