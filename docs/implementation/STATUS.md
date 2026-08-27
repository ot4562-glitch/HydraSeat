# HydraSeat Implementation Status

## Current program state

- Current phase: **Phase 3 — Input Compatibility & Isolation**
- Current default packet: **P3-HW-01 — Gate A/B/C physical acceptance runner**
- Current default packet state: **CODE_COMPLETE** — tooling/CI are complete; real two-keyboard/two-pointing-device physical Gate A/B/C evidence is still required.
- Product contract: **HydraSeat v1 is a two-Seat, game-first local Windows gaming product** for households that want to use the spare performance of one capable PC instead of buying a second complete desktop solely for simultaneous local gaming. See `docs/PRODUCT_V1.md`.
- v1 does not pursue N-Seat generalization, a general independent Windows desktop per Seat, or a maintainer-created official game-certification badge.
- Selected Phase 4 runtime/display/control foundations were implemented early and validated/code-completed independently. This does **not** close Phase 3 or replace its physical acceptance gate.
- Upstream development is carried by the cumulative upstream PR from the fork; exact remote status is updated as the current branch is pushed.

This file is an execution ledger. Automated evidence, physical/manual evidence, real-game evidence, community evidence, and release claims remain separate.

## State legend

`BLOCKED`, `READY`, `IN_PROGRESS`, `CODE_COMPLETE`, `VALIDATED`, `MERGED`, `UPSTREAMED`, `DEFERRED`, `REJECTED`.

`CODE_COMPLETE` means implementation and declared automated evidence exist but a required manual/physical/game/install/reboot gate is still pending. `VALIDATED` requires all evidence declared by that packet.

## Product decisions locked on 2026-08-27

The roadmap and repository documentation now use these v1 decisions:

- at most two active Seats;
- Seat = physical hardware station; Player/Game/TwoPlayerSetup/runtime binding are separate;
- first-run Seat configuration is optional and individual device categories may be `Set later`;
- normal UX is `Game -> Seat 1 / Seat 2 / Both -> Player(s) -> Play`;
- one Seat may stop/change games while the other continues;
- idle Seat uses a minimal Seat Launcher, not a full desktop shell;
- installed-game discovery is normal, manual EXE is fallback;
- same-game/two-instance setup has automatic and guided manual paths where the game/provider permit it;
- protected titles may be explicit warned experiments but HydraSeat never bypasses protection and never claims anti-cheat safety;
- compatibility is transparent success/failure/sample evidence, not an official `Certified` badge;
- compatibility tests are local-first and community sharing is opt-in with redacted JSON preview;
- core operation is offline-first;
- compatibility/setup data refresh is separate from program/runtime/driver update;
- executable/runtime/driver updates require user approval;
- least privilege and a real Windows installer/uninstaller are v1 requirements;
- project license/contribution terms remain a release gate before the product is described as legally open source.

## Completed/validated foundation

| Packet | State | Evidence summary |
| --- | --- | --- |
| P0-RES-01 | UPSTREAMED | Research, related-system review, clean-room policy |
| P1-HW-01 | UPSTREAMED | Stable hardware detection/identity and Windows CI |
| P2-SEAT-01 | UPSTREAMED | Multi-display Seat composition and transactional profile foundation |
| P3-PLAN-01 | UPSTREAMED | Capability planner / `hydra_plan` |
| P3-OBS-01 | UPSTREAMED | Gate A Raw Input observation ledger |
| P3-ROUTE-01 | UPSTREAMED | Gate B controlled two-window routing |
| P3-IPC-01 | UPSTREAMED | Gate C bounded/versioned process protocol |
| P3-STATE-01 | UPSTREAMED | Process-local adapter state/C ABI |
| P3-PROC-01 | UPSTREAMED | Two-process synthetic state separation |
| P3-QUEUE-01 | UPSTREAMED | Bounded target writer queues |
| P3-FAIL-01 | UPSTREAMED | Fail-closed Gate C lifecycle |
| P3-API-01 | VALIDATED | Controlled Win32 API baseline; Windows run `32722277035` |
| P3-ARCH-01 | VALIDATED | x64/x86/cross-architecture Gate C matrix; run `32727711605` |
| P3-API-02 | VALIDATED | Polling shim; run `32780563364` |
| P3-API-03 | VALIDATED | Cursor/focus/capture shim; run `32792381573` |
| P3-RAW-01 | VALIDATED | Raw Input behavior probe; run `32800513365` |
| P3-RAW-02 | VALIDATED | Controlled Raw Input virtualization; run `32806163164` |
| P3-CTRL-01 | VALIDATED | Controlled XInput state/remap; fork PR #15 run `32832036967` |
| P3-CTRL-02 | VALIDATED | Controlled DirectInput visibility/order; fork PR #16 run `32840474306` |
| P3-MET-01 | VALIDATED | Input metrics; fork PR #18 run `32857666855` |
| P3-D-01 | VALIDATED | HidHide read-only capability probe; fork PR #20 run `32915683414` |
| P3-REC-01 | VALIDATED | Crash/watchdog/session-end recovery; fork PR #23 run `32973197727` plus human actual Sign out and Restart acceptance |
| P3-E-01 | VALIDATED | Pinned real GLFW 3.5.1 open-source application path; fork PR #24 run `33038227992` |
| P8-WATCH-01 | VALIDATED | Independent watchdog; fork PR #21 run `32919928489` |
| P8-JOURNAL-01 | VALIDATED | Crash journal/safe-mode marker; fork PR #22 run `32947110442` |
| P8-RESET-01 | VALIDATED | Emergency reset; fork PR #25 run `33050902127` plus local scheduled-task launch |

## Phase 3 active gates

| Packet | State | Immediate evidence required |
| --- | --- | --- |
| P3-HW-01 | CODE_COMPLETE | Run the guided real two-keyboard/two-pointing-device Gate A/B/C acceptance; record physical identities, hot-plug/soak, routing, receiver evidence, and privacy-safe report |
| P3-D-02 | BLOCKED | Becomes actionable only after P3-HW-01 physical acceptance; recovery prerequisites are already validated |
| P3-E-02 | BLOCKED | First explicit real non-anti-cheat game profile after physical suppression/isolation path is proven |
| P3-E-03 | BLOCKED | Two different real-game zero-bleed proof after P3-E-02 |
| P3-CLOSE-01 | BLOCKED | Dedicated Phase 3 close verification after required physical/game gates |

## Phase 4 early foundation

The following work is permitted early as independently testable infrastructure. It does not waive P3-CLOSE-01.

| Packet | State | Evidence / remaining gate |
| --- | --- | --- |
| P4-RUN-01 | VALIDATED | Implementation commit `c139354`; fork PR #26 run `33062975789` passed native x64/x86, Gate C cross-architecture, and P3-E regression |
| P4-IPC-01 | VALIDATED | Implementation commit `1813d39`; same PR #26 exact-head Windows matrix |
| P4-PROC-01 | VALIDATED | Implementation commit `e0e7334`; same PR #26 exact-head Windows matrix |
| P4-WIN-01 | VALIDATED | Implementation commit/head `0fdaf80`; same PR #26 exact-head Windows matrix |
| P4-DIS-01 | CODE_COMPLETE | Local MSVC x64 + Win32/x86 full build and 70/70 CTest pass; real physical monitor identity/reconnect acceptance pending |
| P4-DIS-02 | CODE_COMPLETE | Local MSVC x64 + Win32/x86 70/70 CTest; physical layout acceptance remains tied to Phase 4 display gate |
| P4-WIN-02 | CODE_COMPLETE | Local x64/x86 70/70 CTest; real borderless/fullscreen/DPI/window-restore acceptance pending |
| P4-POL-01 | CODE_COMPLETE | Local x64/x86 70/70 CTest; integration/physical gate remains |
| P4-DIS-03 | CODE_COMPLETE | Local x64/x86 70/70 CTest; physical unplug/replug acceptance pending |
| P4-VID-01 | DEFERRED | Virtual-display backend is not required for the physical-monitor v1 critical path |
| P4-VID-02 | DEFERRED | External virtual-display integration deferred for v1 |
| P4-IDD-01 | DEFERRED | Custom IDD/IddCx driver deferred unless a measured post-v1/v1 requirement justifies cost |
| P4-CTRL-01 | CODE_COMPLETE | Management/control model + GUI integration pass local x64/x86 70/70; real Management display placement acceptance pending |
| P4-CTRL-02 | CODE_COMPLETE | Return/reconfigure transition foundation passes local x64/x86 70/70; independent Seat game lifecycle remains separate P4-SEAT-01 |
| P4-SEAT-01 | BLOCKED | Must implement independent per-Seat game lifecycle and reject a third active v1 Seat after prerequisites are integrated |
| P4-REC-01 | BLOCKED | Requires P4-SEAT-01 plus physical display/recovery matrix |
| P4-CLOSE-01 | BLOCKED | Dedicated Phase 4 close verification after all non-deferred required gates |

### 2026-08-27 — Phase 4 accumulated local foundation verification

State: code-complete foundation for display/window-policy/management/reconfiguration packets; existing RUN/IPC/PROC/WIN-01 packets already independently validated by fork PR #26.
Branch/commit: `feat/p4-runtime-foundation`, implementation commit `60dab1f75b00126b042b87072dfb5982765e9340` (`feat: extend Phase 4 display and control foundations`).

Local build/test evidence:

- initial MSVC x64 build exposed one real integration defect: `hydra_tests` compiled the shared GUI source that referenced new display/control libraries but did not link those dependencies;
- `CMakeLists.txt` was repaired so `hydra_tests` links `hydra_control_surface` and `hydra_session_control` along with its existing libraries;
- after the repair, MSVC x64 full build passed and CTest passed **70/70**;
- MSVC Win32/x86 full build passed and CTest passed **70/70**;
- these local runs cover the new display topology/layout/recovery, window policy, runtime policy, Management Seat/control surface, session-control transition, host/IPC/runtime, and existing regression targets included by CTest;
- no physical display unplug/replug, two-Seat game lifecycle, or P3-HW physical input claim is inferred from these automated results.

Known product gap exposed by the roadmap rewrite:

- the current host/session control foundation still centers several operations around a whole-session command model;
- v1 requires the additional P4-SEAT-01 per-Seat lifecycle so `Seat 1 = Playing, Seat 2 = Idle` is a normal healthy state and one Seat can stop/change games without stopping the other;
- this is documented as a required future packet rather than falsely claimed as already implemented.

## Manual/physical gates still pending

| Gate | State | Required evidence |
| --- | --- | --- |
| Gate A physical device observation | PENDING | Two keyboards + two pointing devices, hot-plug/composite behavior, >=10-minute trace |
| Gate B physical Seat routing | PENDING | Exclusive Seat routes plus shared/ambiguous fail-closed case |
| Gate C physical controlled-process routing | PENDING | Two physical input sets, two controlled targets, receiver-aware metrics, zero verified cross-Seat/process events |
| Gate D guarded device cloaking/suppression | NOT IMPLEMENTED | Only after physical acceptance + validated recovery/reset |
| First real non-protected game | NOT IMPLEMENTED | Exact game/provider/version profile and real evidence |
| Two real games concurrently | NOT IMPLEMENTED | Objective two-Seat input/controller/audio/window/display evidence |
| Same-title two-instance v1 demonstration | NOT IMPLEMENTED | One lawful game/provider setup after Phase 6 setup system; no bypass |
| Real Phase 4 display placement/reconnect | PENDING | Physical Seat display groups, DPI/window transitions, unplug/replug |
| Installer/uninstaller | NOT IMPLEMENTED | Clean-machine install/repair/uninstall and ordinary Windows postconditions |

## Current next action

The default execution task remains **P3-HW-01** because its physical evidence is the only remaining blocker before the guarded device-cloak experiment and the remaining Phase 3 game path can proceed.

The accumulated Phase 4 development may be committed/pushed/reviewed in parallel as early foundation, but agents must not use that remote progress to mark Phase 3 complete.

## Evidence update template

When a packet changes state, record at minimum:

```text
### <date> — <packet ID>
State:
Branch/commit:
Windows CI:
Automated tests:
Manual evidence:
Known limitations:
Rollback result:
Next packet:
```

Historical detailed packet evidence remains available in the repository history and linked fork PR/CI runs above. New status entries should favor exact commit/run/evidence references without duplicating hundreds of lines of already immutable historical narrative.