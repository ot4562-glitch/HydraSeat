# HydraSeat Implementation Status

## Current program state

- Current validation phase: **Phase 3 — Input Compatibility & Isolation**
- Current default packet: **P8-SIGN-01 — Code and driver signing pipeline** (next automated distribution frontier under D-051)
- Current default packet state: **ACTIONABLE FOR AUTOMATED IMPLEMENTATION under D-051** — Phase 9 data-first ecosystem closure plus P8 redacted diagnostics, least-privilege broker core, and startup policy are now `CODE_COMPLETE`; next is secure release signing/provenance plumbing, followed by installer/update integration. Physical, real-game, clean-machine installer/reboot, mixed-DPI, Explorer, and live public-service/reporting/retention evidence remains pending and is not promoted to validation.
- Deferred validation queue starts with **P3-HW-01** — tooling/CI are complete; real two-keyboard/two-pointing-device physical Gate A/B/C evidence is still required before physical isolation/cloaking claims and Phase 3 closure.
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
| P3-HW-01 | CODE_COMPLETE | Deferred validation: run the guided real two-keyboard/two-pointing-device Gate A/B/C acceptance before physical claims/release |
| P3-D-02 | CODE_COMPLETE | Guarded transaction, disabled-by-default native boundary, durable recovery snapshot, watchdog/reset restore executor, x64/x86 75/75, and strict P3-D-02 3/3 pass; physical activation/claim remains deferred until P3-HW-01 |
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
| P4-SEAT-01 | CODE_COMPLETE | Independent two-Seat lifecycle integrated with RuntimeHost/protocol v3/IPC/events/reconnect; x64/x86 72/72 and controlled Job Object independence pass; real-game/physical recovery remains separate |
| P4-REC-01 | CODE_COMPLETE | Controlled x64/x86 Seat/Host crash, UI reconnect, two-target watchdog, restart/global-return, and no-orphan matrix passes; production launch integration and physical display/session acceptance remain |
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

### 2026-08-28 — P4-SEAT-01 controlled implementation complete

State: `CODE_COMPLETE` — declared automated implementation and controlled-process evidence pass; real-game/physical recovery gates remain separate.
Branch/commit: `feat/p4-seat-lifecycle`, implementation commit `b5fd37ff8fda8aed6e35b5f2b951e8d707972b69`; fork PR #27 run `33134415259` passed Windows x64, Win32/x86, Gate C cross-architecture, and P3-E open-source application jobs.
Automated evidence:

- MSVC x64 full build and CTest pass **72/72**, including `SeatGameLifecycleTests`, `HostProtocolTests`, and `SeatGameProcessLifecycleTests`;
- MSVC Win32/x86 full build and CTest pass **72/72** with the same lifecycle/protocol/real controlled-process coverage;
- MinGW strict portable `SeatGameLifecycleTests` pass with `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion`;
- controlled process evidence uses exact Seat-owned Job Object process trees and confirms Seat 1 remains the same live process instance while Seat 2 stops and restarts.
- authoritative `RuntimeHost` integration requires an active prepared whole-machine runtime before Seat start, performs Seat-local cleanup before global rollback/reset, and rejects profile replacement/host exit while Seat-local work remains;
- factory/start exceptions are contained as Seat-local backend failures; an unverified partial-start cleanup retains the exact Seat-local instance in `RecoveryRequired`, and focused failure-injection tests prove a later Seat-local stop retries and verifies cleanup rather than losing process/backend ownership;
- invalid lifecycle construction now fails closed with the exact cause across every mutation path: empty/duplicate Seat configuration reports `InvalidSeat`, a third active Seat reports `V1SeatLimitExceeded`, and a missing instance factory reports `BackendFailure`; none can falsely request whole-machine return readiness;
- host protocol v3 and real host-process IPC cover canonical Seat-ID-ordered snapshots, bounded assign/start/stop/reconcile commands, Seat-identified ordered events, UI-style reconnect, malformed/unavailable-plan failure, impossible phase/binding and contradictory whole-machine-return rejection, and unknown third-Seat rejection.
- `hydra_hostctl` exposes bounded Seat assign/start/stop/reconcile commands and per-Seat text/JSON state, discovers non-default Management Seat authority, and uses per-client OS-random correlation seeds so separate CLI/UI processes do not collide in the authoritative lifecycle deduplication window.

Known limits:

- the controlled fixture is not real-game, physical input, display, audio, controller, or physical zero-bleed evidence;
- the minimal idle Seat Launcher UI is owned by Phase 7 and is not implied by the Idle runtime state;
- P3-HW-01 and all physical/manual gates remain pending and unchanged.

Next dependency: P4-REC-01 controlled automation is code-complete, but production launch integration and physical display/session acceptance remain pending before validation/closure.

### 2026-08-28 — P4-REC-01 controlled recovery implementation complete

State: `CODE_COMPLETE` — declared controlled automation is implemented and passing; required production integration/manual evidence is incomplete.
Branch/commit: `feat/p4-seat-lifecycle`, implementation commit `b5fd37ff8fda8aed6e35b5f2b951e8d707972b69`; fork PR #27 run `33134415259` passed Windows x64, Win32/x86, Gate C cross-architecture, and P3-E open-source application jobs.
Automated evidence:

- MSVC x64 and Win32/x86 full builds and CTest pass **72/72**;
- an authoritative `RuntimeHost` owns two real controlled Job Object process trees, preserves Seat 1's exact PID/creation identity after a forced Seat 2 exit, acknowledges only the failed Seat, completes ten Seat 2 stop/restart cycles, and completes three full two-Seat/explicit-return cycles without exact-root leaks;
- forced global reset removes both exact roots, Host destruction leaves zero exact owned process orphans, and a unit test verifies Seat-local teardown precedes shared-backend rollback;
- a separate authoritative Host fixture is forcibly terminated without destructors after both Seat trees become live; Job Object handle closure removes both exact roots on x64 and x86;
- a separate read-only UI-style client is forcibly terminated while both controlled Seats are Playing; a fresh client reconnects and resnapshots both authoritative Playing states while both process trees remain live;
- the production control-surface model and Win32 status line display authoritative per-Seat phases, surface one-Seat degradation without hiding the healthy Seat, project Seat recovery into a reset-only fail-closed action, show the both-ended return request explicitly, and reject a third inactive GUI Seat configuration before Host mutation;
- the watchdog Host-death fixture now registers two distinct PID/creation-time identities, forcibly terminates the owner without destructors, and verifies both reverse rollback actions complete independently on x64/x86;
- existing watchdog/reset, display-recovery, process-group, and window-tracker suites retain coverage of the independent recovery mechanisms and stale PID/HWND rejection.

Remaining evidence:

- actual HydraSeat GUI crash/reopen over real games and end-to-end watchdog relaunch over the new Seat lifecycle;
- physical display unplug/replug, sign-out/restart, and ordinary-Windows postcondition acceptance;
- real-game/input/audio/controller/window-helper ownership remains outside the controlled synthetic fixture.

## Phase 5 automated frontier

| Packet | State | Evidence / remaining gate |
| --- | --- | --- |
| P5-AUD-01 | CODE_COMPLETE | Read-only Core Audio render/capture inventory, stable endpoint ID, state/default-role observation, bounded notification generation, optional Seat audio validation; x64/x86 full 77/77, strict P5-AUD-01 2/2, and local native read-only inventory observed 37 endpoints; selected two-output reconnect/audible acceptance remains physical/manual |
| P5-AUD-02 | CODE_COMPLETE | Exact PID+creation-time Core Audio session ownership, late-session observation, typed apply/verify/rollback contract, explicit observe-only production fallback, x64/x86 full 78/78 and strict P5-AUD-02 1/1 pass; native read-only session enumeration succeeds, while arbitrary endpoint movement/audible two-output routing remains unsupported unless a documented safe backend is selected and physically verified |
| P5-CTRL-01 | CODE_COMPLETE | Production poll worker, runtime-only XInput slot policy, stable DirectInput GUID identity, two-Seat binding, reconnect generation, exact virtual-state/vibration ownership, x64/x86 full 80/80, strict focused 2/2, and native read-only controller diagnostic pass; no physical controller is currently attached on the dev PC, so physical state/vibration evidence remains deferred |
| P5-LAUNCH-01 | CODE_COMPLETE | Immutable exactly-two-Seat compiler/fingerprint, selected-game-only preflight, typed recovery/process/window/display/input/controller/audio activation ordering, every activation-failure index reverse rollback, retained recovery ownership, natural-exit Seat-local cleanup, and RuntimeHost independent Seat isolation; x64/x86 full 81/81 and strict focused pass; real-game/physical resource activation remains deferred |
| P5-MET-01 | CODE_COMPLETE | Versioned plan-fingerprint two-Seat evidence report, explicit Synthetic/Controlled/Physical origin, receiver-complete/loss-free isolation verdict, controller/audio outcomes, launch/stop/rollback timing and existing CPU/memory aggregate, privacy-safe JSON; x64/x86 full 82/82 and strict focused 1/1 pass; physical zero-bleed/real-game performance remains deferred |
| P5-MVP-01 | CODE_COMPLETE | Controlled production-path harness uses real owned root+descendant Job Object trees, independent Seat 2 stop/restart/natural-exit while Seat 1 exact identity survives, injected Seat 2 rollback fault, final orphan=0 and verified host return; x64/x86 full 83/83 and strict focused 1/1 pass; display/input/controller/audio evidence remains Synthetic and not physical validation |

## Phase 6 automated frontier

| Packet | State | Evidence / remaining gate |
| --- | --- | --- |
| P6-SCHEMA-01 | CODE_COMPLETE | Separate bounded version-1 Seat/Player/Game/TwoPlayerSetup/runtime schemas; stable persistence cannot represent PID/HWND/handles or provider secrets/scripts; compatibility is referenced only by logical record/provenance/revision; transactional strict JSON/Unicode/version/bounds/cross-reference tests pass; x64/x86 exact-head full 84/84 and strict focused 1/1 pass |
| P6-MIG-01 | CODE_COMPLETE | Read-only bounded legacy-v2 parse; deterministic separated six-file v1 bundle; exact source-byte backup; bounded canonical diagnostics for targetHwnd, unknown values, and shareability; staged byte/decode/backup validation; explicit replace authorization; previous-bundle rollback across injected write, staged-validation, commit, and post-commit failures; strict Windows x64 MinGW focused build/test pass |
| P6-CATALOG-01 | CODE_COMPLETE | Pure bounded candidate reconciliation with no provider/game filesystem mutation; provider+app stable identity plus normalized executable fallback; title/icon excluded from identity; duplicate provider/manual records merge deterministically and candidate-order-invariant; conflicting strong identities fail closed; icon/architecture/staleness remain catalog-only metadata; strict Windows x64 MinGW focused build and GameCatalogTests 1/1 pass |
| P6-PROV-01 | CODE_COMPLETE | Bounded typed provider descriptor/capabilities, explicit available/offline/absent states, revision-pinned discovery/account/launch/process operations, catalog revalidation, opaque account references only, executable-or-URI plus argument-vector launches, transactional malformed/stale rejection, and deterministic fake-provider tests; strict MinGW and focused MSVC x64/x86 P6 schema/catalog/provider tests pass 3/3; live providers and real launches remain later evidence |
| P6-PROV-02 | CODE_COMPLETE | Independent bounded KeyValues reader over read-only registry/library/app metadata; deterministic revision, Steam AppID/title/install/build/executable/icon candidates, normal typed steam run URI construction, no account/argument/same-instance policy guess, exact path/PID/creation process candidates remain unverified; strict focused 5/5, full x64 89/89 and x86 89/89, native read-only discovery found 2 launchable apps; actual launch/game/provider policy remains isolated VM/manual evidence |
| P6-PROV-03D | CODE_COMPLETE | Explicit absolute EXE/argument vector/optional workdir/local icon definition; read-only canonical PE identity/architecture inspection; deterministic definition+file revision; provider-neutral catalog candidate and exact executable launch request; no shell/script/process start; path-matched process evidence remains unverified; fake-source failure/stale/bounds tests and native read-only smoke pass; strict focused 7/7 and full MSVC Release x64 91/91 plus x86 91/91 pass sequentially |
| P6-PLAN-01 | CODE_COMPLETE | Pure one/two-Seat provider-aware compiler validates stable documents, exact requirement/provider revisions, account ambiguity, same-game setup, hardware/capability/high-risk preflight, and cross-Seat exclusive hardware before emitting a canonical immutable fingerprinted plan; strict MinGW target builds and focused Phase 6 batch verification applies; real launch remains unperformed |
| P6-PREFLIGHT-01 | CODE_COMPLETE | Deterministic normal-user + bounded Expert messages for all plan blockers and successful requirements/risks; typed payload-free mutation preview, approval gate, duplicate/bounds rejection; strict focused target builds and runtime test passes |
| P6-PROFILE-01 | CODE_COMPLETE | Pure automatic candidate generation plus transactional guided editor over exactly two typed recipes; absolute path/shared-data-root/stale compatibility validation; automatic/manual fixtures converge to identical runtime bindings; strict focused target builds |
| P6-UI-01 | CODE_COMPLETE | Bounded UI-independent library/Player/Seat/setup model plus default Win32 Games surface; read-only Steam refresh and typed Add EXE; create/rename/remove Player and per-Seat/Both flow; exact injected requirement evidence only, absent evidence disables Play; multiple custom AppID-scoped adapters supported fail-closed; MinGW product/focused builds, x64/x86 focused 5/5, full 98/98 each, and computer-use live read-only UI smoke pass without launching a provider/game |
| P7-SHELL-01 | CODE_COMPLETE | `hydra_seat_ui.exe`, fixed-Seat snapshot model, `SeatControl` server authorization, narrow client adapter, stale-display fail-closed placement, independent idle/playing/recovery process reports, reconnect, and state-preservation checks pass focused MinGW/MSVC x64/x86 4/4; physical display/input and real-game evidence remain pending |
| P7-LAUNCH-01 | CODE_COMPLETE | Shared Player/presentation snapshot loading and exact P6 selection/setup/compiler/preflight composition; immutable host resnapshot gate; own-Seat-only assign/start/rollback; different/same-game/failure matrix; x64/x86 controlled Job Object E2E preserves other Seat exact PID+creation time and leaves orphan=0; production evidence store/plan installer and real game remain pending |
| P7-NOTIFY-01 | CODE_COMPLETE | Bounded Seat-local stable notification IDs/actions from host/preflight state; no raw diagnostics/private paths/account material; authority generation/sequence rejects stale success; localized Seat UI consumes stable IDs; focused tests pass |
| P7-CURSOR-01 | CODE_COMPLETE | Pure Seat navigation model accepts only exact assigned display + Seat + authority pointer samples, never mutates Windows system cursor, and provides bounded controller-focus fallback; cross-Seat/stale/out-of-region cases fail closed; focused tests pass; physical no-bleed remains pending |
| P7-HOTKEY-01 | CODE_COMPLETE | Focused-window Seat hotkeys only; semantic policy rejects cross-Seat/stale authority, End Playing requires confirmation, recovery/help is Seat-local, emergency shortcut only surfaces independent reset guidance and never executes global reset; focused tests/Seat UI build pass |
| P7-I18N-01 | CODE_COMPLETE | Stable UI IDs with en-US/ko-KR/zh-CN catalogs and English fallback; Games/Seat UI + preflight/notifications localized from stable IDs while machine identifiers remain English; focused localization test and UI builds pass |
| P7-A11Y-01 | CODE_COMPLETE | Pure Management/Seat layout readiness matrix across 96–288 DPI, all three locales, declared input modalities, focus order, translated text expansion, Protected/Recovery action visibility; unsafe/off-screen/no-input layouts fail closed; physical mixed-DPI/screen-reader evidence pending |
| P7-REC-01 | CODE_COMPLETE | Disposable Seat UI process ownership, forced client crash leaves authoritative game/global state unchanged, restart/resnapshot works, controlled crash during Starting is isolated; real host-loss/physical display reconnect/Explorer coexistence remain manual acceptance |
| P6-CLI-01 | CODE_COMPLETE | `hydraseat_profilectl` list/validate/export for Game/Player/Setup/plan in human/JSON forms; stable schema reuse, redacted Player account refs, versioned redacted plan snapshot, strict malformed/Unicode/bounds handling; focused CLI test passes |
| P6-IMPORT-01 | CODE_COMPLETE | Versioned provenance package redacts every source work/data path to typed variables, requires exact local remapping, rejects undeclared/missing/relative paths and malformed/trailing data, revalidates setup transactionally; strict target builds |
| P6-REG-01 | CODE_COMPLETE | Cross-packet corpus covers providers, duplicate/moved/stale catalog data, Unicode, Seat/Player moves, provider offline/missing, same-game auto/manual setup, import/malformed packages, and Protected approval; exposed and fixed a catalog representative tie-break determinism bug; focused regression passes |

Focused Windows Phase 6 batch check on 2026-08-29: **15/15 P6-labeled tests passed**, including profile schema/migration/catalog, provider contract, Steam fixture + native read-only smoke, custom EXE fixture + native read-only smoke, provider-aware plan, preflight, game-first UI model, setup editor, portable package, cross-packet regression, and CLI. Complete MSVC Release x64 and Win32/x86 suites each pass 98/98 sequentially. This is automated/read-only/controlled evidence only; it does not replace real-game/provider-policy/physical acceptance.

## Phase 8/9 data-first automated frontier

| Packet | State | Evidence / remaining gate |
| --- | --- | --- |
| P8-PRIV-01 | CODE_COMPLETE | Narrow fixed-resource Install/Repair/Remove broker core; authenticated same-user/channel/elevation context, monotonic anti-replay sequence, broker-owned artifact trust, no command/path fields, capture/apply/verify/rollback contract, explicit RecoveryRequired; focused broker tests pass; native UAC/SCM/installer integration remains later |
| P8-BOOT-01 | CODE_COMPLETE | Manual/BackgroundIdle/AutoActivateValidatedSession policy, exact validated-session + journal/safe-mode/topology/capability/recovery preflight, Idle fallback, duplicate-host prevention, explicit user-approved fixed-target registration and transactional disable/rollback; focused startup tests pass; real logon/reboot remains manual |
| P8-TRUST-01 | CODE_COMPLETE | Data-only vs executable/driver/helper trust classes; exact hash/version/architecture, source/license/capability, signature/development exception, recovery-plan and redistribution policy; optional absence non-fatal; focused trust tests pass; production signing remains later |
| P8-DATA-01 | CODE_COMPLETE | Pure offline catalog-cache lifecycle preserves last valid cache across network-off/disabled/source-missing/tamper/stale failures, uses P8 trust for first/update, explicit rollback; focused cache tests pass |
| P8-DIAG-01 | CODE_COMPLETE | Redacted support bundle admits only public/aggregate evidence, reduces crash journal to phase/generation/counts, canonicalizes bounded event codes, revalidates mandatory redaction, and binds export to explicit approval of the exact JSON preview; focused support-bundle tests pass |
| P9-SDK-01 | CODE_COMPLETE | Bounded public result v1, strict canonical JSON/future/unknown/privacy checks, null-vs-zero semantics, setup revision, mandatory redaction/provenance, Phase 5 metrics adapter; focused tests pass; no community validation claim |
| P9-CAP-01 | CODE_COMPLETE | Exact environment/setup/backend/freshness cohorts with Success/Failure/Untested and per-subresult denominators; deterministic order, duplicate/future rejection, Protected segregation; focused aggregation tests pass |
| P9-PKG-01 | CODE_COMPLETE | Data-only manifest entries (result/setup/catalog), exact package/member hashes, schema selectors, trust/provenance/license, monotonic update and transactional rollback; focused package tests pass |
| P9-PROFILE-01 | CODE_COMPLETE | Community setup reuses P6 portable remap/validation/plan; local Protected approval remains authoritative; shell/bypass/external-resource instruction markers rejected; focused setup tests pass |
| P9-REG-01 | CODE_COMPLETE | Trusted local snapshot requires manifest-declared payload kind/selector/package identity and renders current/stale cohorts + setup candidates fully offline; unknown game remains untested/empty; focused catalog tests pass |
| P9-TEST-01 | CODE_COMPLETE | `hydraseat_community_validate` strict offline result/setup-package canonicalization workflow plus underlying malformed/private/tamper/cohort/local-preflight fixtures; executable builds under strict MinGW warnings |
| P9-RPC-01 | CODE_COMPLETE | Optional typed submission boundary canonicalizes the local result first, requires exact-preview approval, has no background retry/account/credential surface, preserves local evidence across offline/timeout/failure, and uses deterministic idempotency plus bounded accepted/duplicate receipts; focused tests pass; no live service claim |
| P9-DIAG-01 | CODE_COMPLETE | Local-first share state model covers local result, preview, decline, pending/success/failure, and superseded history; upload state never changes technical truth and Protected/Experimental state remains attached; focused tests pass |
| P9-SEC-01 | CODE_COMPLETE | Community threat model maps malformed data, command/binary/bypass text, privacy leakage, forged/stale evidence, duplicate/spam, protected semantics, external resources, catalog tamper, and transport outage to concrete validators/tests; public collection still requires a real private reporting/withdrawal route and deployment privacy/retention terms |
| P9-DOC-01 | CODE_COMPLETE | Public contributor guide documents local testing, evidence-not-certification semantics, exact redaction preview, auto/manual setup contribution, offline sharing behavior, provenance, protection warnings, no-bypass rules, and the real conformance CLI commands; README links the guide/threat model |
| P9-CLOSE-01 | CODE_COMPLETE | Dedicated automated close review confirms local-first technical truth, exact-preview opt-in sharing, segmented/Untested/Protected aggregation, data-only setup/package/catalog trust, offline cache, deferred binary SDK scope, security/contributor docs; focused P9 regression 7/7 passes and conformance CLI builds; production public service/reporting/retention/community campaign remain unvalidated |

## Manual/physical gates still pending

| Gate | State | Required evidence |
| --- | --- | --- |
| Gate A physical device observation | PENDING | Two keyboards + two pointing devices, hot-plug/composite behavior, >=10-minute trace |
| Gate B physical Seat routing | PENDING | Exclusive Seat routes plus shared/ambiguous fail-closed case |
| Gate C physical controlled-process routing | PENDING | Two physical input sets, two controlled targets, receiver-aware metrics, zero verified cross-Seat/process events |
| Gate D guarded device cloaking/suppression | CODE COMPLETE / PHYSICAL PENDING | Transaction/native boundary/recovery code exists, but actual device cloaking/suppression remains disabled and unvalidated until physical acceptance |
| First real non-protected game | NOT IMPLEMENTED | Exact game/provider/version profile and real evidence |
| Two real games concurrently | NOT IMPLEMENTED | Objective two-Seat input/controller/audio/window/display evidence |
| Same-title two-instance v1 demonstration | NOT IMPLEMENTED | One lawful game/provider setup after Phase 6 setup system; no bypass |
| Real Phase 4 display placement/reconnect | PENDING | Physical Seat display groups, DPI/window transitions, unplug/replug |
| Installer/uninstaller | NOT IMPLEMENTED | Clean-machine install/repair/uninstall and ordinary Windows postconditions |

## Current next action

Under D-051, the non-deferred automated Phase 7 Seat Launcher path, P9 data-first ecosystem (including dedicated closure), and P8 redacted diagnostics/least-privilege/startup policy are now `CODE_COMPLETE` with focused controlled evidence. The old stacked P6 PRs #35-#38 are closed, obsolete local/remote branches have been removed, and development is consolidated on the single `feat/p6-p7-consolidated` -> `main` PR #39; its previously published x64/x86/cross-architecture/P3-E checks were green before this new batch. P8 privilege/startup code is deliberately native-adapter/policy level: real UAC/SCM/logon/reboot and clean-machine installer acceptance remain pending. The next automation frontier is **P8-SIGN-01**, then installer/update integration; no `VALIDATED` claim is implied.

Deferred validation queue: P3-HW-01 Gate A/B/C remains physically unvalidated and still blocks `PhysicalDeviceCloaking`/physical zero-bleed claims, Phase 3 closure, and release validation. It no longer blocks unrelated automated coding. Later phases may be implemented against truthful controlled/fake evidence and remain `CODE_COMPLETE` until their manual gates are eventually run.

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
