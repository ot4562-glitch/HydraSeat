# HydraSeat Concurrent Chunk Board

This board defines concurrent implementation batches for up to ten workers. It is not permanent agent ownership. A worker claims exactly one READY chunk, works only inside that chunk's file envelope, and returns the result to the central control tower for integration.

`docs/implementation/STATUS.md` remains the roadmap/product truth. Runtime claims live under `.ai-bridge/chunk-claims/` and are intentionally gitignored.

## Current integration baseline

Before this batch is distributed, the control tower established:

- MSVC Release x64: full solution build **PASS**, current CTest **132/133 PASS**; the only reproducible failure is `AudioRoutingTests` with a process crash/segfault;
- MSVC Release Win32/x86: `HydraSeat` and `audio_routing_tests` build **PASS**; focused `AudioRoutingTests` reproduces the same crash/segfault;
- the previously stale Process/Host/Seat UI/Gate-C test binaries were rebuilt; current focused ProcessGroup, Host IPC, Seat UI, ProductionLaunchRuntime, Window, and Gate-C process regressions all pass;
- implementation roadmap validator last established **117 packets / 0 warnings**;
- every top-level `src/*.cpp` and `tests/test_*.cpp` is referenced by `CMakeLists.txt`;
- product code contains no outstanding `TODO`/`FIXME` implementation markers;
- physical/manual/real-game/clean-machine gates remain separate and must never be synthesized by a worker.

A worker that breaks the baseline has not completed its chunk unless the break is an explicitly documented pre-existing/environmental issue accepted by the control tower.

## Mandatory worker protocol

From `C:\HydraSeat\repo`:

```text
python3 tools/chunk_claim.py list
python3 tools/chunk_claim.py claim CHUNK-... --owner <unique-worker-id> --paths <concrete-path> [more paths...] --note "short scope"
# work only inside the claimed path envelope
python3 tools/chunk_claim.py heartbeat CHUNK-... --owner <unique-worker-id> --note "current subtask"
python3 tools/chunk_claim.py done CHUNK-... --owner <unique-worker-id> --summary "result" --verification "tests/evidence" --follow-up "next action or none"
```

If work cannot finish:

```text
python3 tools/chunk_claim.py blocked CHUNK-... --owner <unique-worker-id> --reason "blocker" --verification "checks already run" --follow-up "required chunk/action"
```

Rules:

1. `list` is a mandatory refresh immediately before `claim`.
2. A failed claim means another worker owns the chunk/path. Do not edit it.
3. `--paths` must contain only the concrete files/directories actually expected to change.
4. Before every edit, reread the target file or verify its current SHA. Concurrent change -> stop and report collision.
5. One worker holds one chunk unless the control tower explicitly says otherwise.
6. DONE means ready for control-tower review, not roadmap `VALIDATED`.
7. Reference repositories under `C:\HydraSeat\references` are read-only research inputs and never build inputs.

## Control-tower-only shared files

Workers must not modify these unless the control tower explicitly delegates one exact file for one exact change:

- `CMakeLists.txt`
- `cmake/*`
- `AGENTS.md`
- `.agents/AGENTS.md`
- `.agents/CHUNKS.md`
- `.agents/WORKER_PROMPT_TEMPLATE.md`
- `README.md`, `README.ko.md`, `README.zh-CN.md`
- `docs/PRODUCT_V1.md`
- `docs/ARCHITECTURE.md`
- `docs/implementation/README.md`
- `docs/implementation/DECISIONS.md`
- `docs/implementation/STATUS.md`
- `tools/chunk_claim.py`

Workers also do not commit, reset, rebase, push, create PRs, or clean unrelated files. The control tower owns CMake/shared-document integration, broad regression, claim reconciliation and final git hygiene.

## Historical/current chunk catalog

### CHUNK-PROCESS-HANDOFF - Authoritative game-process reacquisition after launcher/loader handoff

**Primary gap:** research requirement `REF-R01`. A successful spawn PID may be a launcher/loader rather than the final authoritative game process.

**Allowed production envelope:**

- `include/hydra/process_group.hpp`
- `include/hydra/process_launcher.hpp`
- `include/hydra/seat_game_lifecycle.hpp`
- `include/hydra/production_launch_runtime.hpp`
- matching `src/process_group.cpp`, `src/process_launcher.cpp`, `src/seat_game_lifecycle.cpp`, `src/production_launch_runtime.cpp`
- matching focused tests (`test_process_group`, `test_seat_game_lifecycle`, `test_seat_game_process_lifecycle`, `test_production_launch_runtime`)
- new test-only process fixtures under `tests/` when required

**Outcome:** preserve exact owned process-tree identity while a launcher exits/descendant becomes authoritative; bind only owned descendants; reject PID reuse/unowned matches; Seat 1 survives Seat 2 handoff failure; stop/recovery leaves orphan count zero.

**Do not:** broaden process killing/search to unrelated same-name processes, use fixed sleeps as authority, or change host IPC/CMake/shared docs.

---

### CHUNK-WINDOW-REACQUIRE - Revalidate recreated/final game HWND without cross-Seat capture

**Primary gaps:** `REF-R02` and the window side of `REF-R03`. The first HWND may not be final and the Raw Input target may differ from the visual/main window.

**Allowed production envelope:**

- `include/hydra/window_identity.hpp`
- `include/hydra/window_tracker.hpp`
- `include/hydra/window_policy.hpp`
- `include/hydra/window_placement.hpp`
- matching `src/window_*`
- focused window tests and new controlled window-process fixtures under `tests/`

**Outcome:** reacquire a destroyed/recreated authoritative owned HWND using PID + creation/process ownership and bounded generation rules; never capture another Seat/unowned window; explicitly model/report when the visual window and an input-target HWND are different rather than pretending one handle proves both.

**Do not:** edit input/Gate C implementation, display routing, launcher UI, CMake or shared docs. Cross-boundary needs become integration notes.

---

### CHUNK-CONTROLLER-COMPAT - Complete controller compatibility consistency across state/capability/output paths

**Primary gaps:** `REF-R05` and `REF-R09`.

**Allowed production envelope:**

- `include/hydra/controller_runtime.hpp`
- `include/hydra/virtual_xinput_state.hpp`
- `include/hydra/directinput_policy.hpp`
- `include/hydra/gate_c_shim_api.h`
- matching `src/controller_runtime.cpp`, `src/controller_native.cpp`, `src/virtual_xinput_state.cpp`, `src/directinput_policy.cpp`, Gate-C XInput compatibility implementation files only
- matching controller/virtual-XInput/DirectInput/Gate-C XInput focused tests

**Outcome:** one Seat generation must route `XInputGetState`, applicable `XInputGetStateEx`/ordinal 100, capabilities, battery and rumble to the same logical controller; disconnect/reconnect invalidates stale generations; provider/target lifecycle is explicit and cleanup-verifiable; x86/x64 layouts stay fixed-width.

**Do not:** claim physical-controller validation from controlled tests or adopt retired ViGEm code/dependencies by copying reference source.

---

### CHUNK-COMPAT-RECIPE - Explicit compatibility timing and transactional per-instance writable materialization

**Primary gaps:** `REF-R04` and `REF-R06`.

**Allowed production envelope:**

- `include/hydra/provider_launch_plan.hpp`
- `include/hydra/game_runtime_requirement_resolver.hpp`
- `include/hydra/two_player_setup_editor.hpp`
- `include/hydra/setup_package.hpp`
- matching production sources/tests
- a new narrowly scoped `instance_materialization.*` module and focused tests if needed

**Outcome:** represent compatibility timing as typed bounded data (`PreSpawn`/`Startup`/`PostWindow`/`Runtime` or an equally narrow explicit model); reject a requirement applied at the wrong phase; implement only lawful source-safe per-instance writable-root materialization with prepare/apply/verify/reverse-rollback and interrupted-operation recovery; original game install remains unchanged.

**Do not:** add arbitrary scripts/CLR/command execution, DRM/anti-cheat/single-instance bypass, unrestricted filesystem operations, or community popularity as runtime authority.

---

### CHUNK-TRUST-UPDATE - Exact publisher trust and atomic expected-previous update mutation

**Primary code gaps:** the P8-TRUST exact-publisher follow-up and P8-UPD expected-previous compare under the same native mutation lock.

**Allowed production envelope:**

- `include/hydra/artifact_trust.hpp`
- `include/hydra/privilege_broker.hpp`
- `include/hydra/installer_transaction.hpp`
- `include/hydra/update_transaction.hpp`
- matching `src/*` and focused tests for those modules
- release validator/tool files only when the change directly verifies these contracts and does not modify control-tower-owned CMake/shared docs

**Outcome:** executable/helper/driver trust can bind to an exact configured publisher identity where required; caller-supplied signature/hash remains non-authoritative; update expected-previous comparison and mutation occur under one authoritative lock/transaction so TOCTOU cannot silently replace the installed generation; stale/tampered/mismatched state fails before mutation; rollback is verified.

**Do not:** weaken development exceptions, invoke arbitrary elevated commands, or claim clean-machine/UAC/signing validation from unit tests.

---

### CHUNK-PRIVACY-EVIDENCE - Close remaining automated P10 privacy/evidence plumbing without UI ownership collisions

**Primary code/document gap:** P10-PRIV-01 still needs installed-product retention/data-flow audit completion and end-to-end exact-payload approval evidence.

**Allowed production envelope:**

- `include/hydra/compatibility_local_store.hpp`
- `include/hydra/compatibility_share_model.hpp`
- `include/hydra/support_bundle.hpp`
- `include/hydra/acceptance_campaign.hpp`
- `include/hydra/acceptance_probe.hpp`
- matching production sources/tests
- `docs/PRIVACY.md`
- `docs/security/PRIVACY_DATA_FLOW_AUDIT.md`
- `schemas/v1_acceptance_campaign_v1.schema.json`
- `tools/run_v1_acceptance_campaign.ps1`
- `tools/validate_v1_acceptance_campaign.py`

**Outcome:** trace every retained/exported/shared product payload to a bounded local authority and retention/delete path; prove the exact preview bytes approved by the user are the bytes submitted/exported (or fail closed); duplicate/stale/superseded approvals cannot authorize a different payload; acceptance evidence remains exact-RC/scenario-bound and synthetic/controlled evidence can never become physical evidence.

**Do not:** edit launcher/Win32 UI directly. If UI wiring is required for final end-to-end closure, return a narrow control-tower integration note. Do not invent deployed-service retention terms when no service exists.

### CHUNK-AUDIO-ROUTING-STABILITY - Eliminate current x64/x86 AudioRoutingTests crash and ownership-lifetime UB

**Primary gap:** the synchronized 2026-08-30 build reproduces `AudioRoutingTests` as a process crash/segfault on both x64 and Win32/x86. This appeared after the process-identity/handoff record shape changed, so stale aggregate assumptions and audio transaction lifetime/verification must be audited rather than masking the crash.

**Allowed production envelope:**

- `include/hydra/audio_routing.hpp`
- `src/audio_routing.cpp`
- `tests/test_audio_routing.cpp`
- a new narrowly scoped audio test helper under `tests/` if required

**Outcome:** no UB/crash on either architecture; exact PID+creation ownership semantics remain intact; waiting/apply/verify/rollback/recovery behavior still fails closed; test fixtures initialize current `ProcessRecord` semantics explicitly rather than relying on dangerous aggregate brace elision.

**Do not:** edit `process_group.*` (owned by `CHUNK-PROCESS-HANDOFF`), weaken exact process identity, turn failures into sleeps/retries, or add undocumented native audio mutation.

---

### CHUNK-TRUSTED-RUNTIME-COMPOSITION - Wire trusted local requirement authority into the real GUI and Host

**Primary gap:** the resolver/store exists, but production composition is disconnected. `Win32App::openGameLibrary()` currently calls `showLauncherWindow(..., {}, ...)`, and `HostControlServer` constructs `HostProviderPlanRegistry` without a trusted requirement source, so normal Play remains fail-closed even when valid local evidence exists.

**Allowed production envelope:**

- `include/hydra/game_runtime_requirement_resolver.hpp`
- `src/game_runtime_requirement_resolver.cpp`
- a new narrow `production_requirement_source.*` module if useful
- `src/gui_win32.cpp`
- `src/host_main.cpp`
- `include/hydra/host_transport.hpp`
- `src/host_transport.cpp`
- focused resolver/host/launcher composition tests and new test fixtures only

**Outcome:** build one deterministic current trusted requirement snapshot from the fixed local store plus current local provider/catalog/evidence inputs; feed its `GameRuntimeRequirement` projection to the launcher; independently re-resolve the same authority in the Host immediately before plan install/activation; missing/corrupt/stale/community-only evidence keeps Play disabled and Host install denied; no client-supplied snapshot becomes authoritative.

**Do not:** edit `launcher_win32.cpp`, `production_launch_runtime.*`, CMake/shared docs, or treat community popularity/history as runtime authority. Production defaults remain physical evidence unless an explicitly controlled test source is injected.

---

### CHUNK-COMPAT-ACTIVATION-ADAPTER - Connect explicit compatibility recipe phases to the Seat activation lifecycle

**Primary gap:** `RecipeExecutionSession` and transactional per-instance materialization are implemented and tested, but current production references are definition/tests only; no actual activation lifecycle consumes PreSpawn/Startup/PostWindow/Runtime phases.

**Allowed production envelope:**

- `include/hydra/instance_materialization.hpp`
- `include/hydra/two_seat_launch.hpp`
- matching `src/two_seat_launch.cpp`
- a new narrow `production_compatibility_activation.*` adapter
- focused materialization/two-seat activation tests

**Outcome:** expose and exercise the smallest typed lifecycle hook that lets an already-compiled exact trusted materialization plan execute at the declared phases, in order, with reverse rollback and recovery-required semantics. The adapter must preserve the immutable original game install, Seat/session-specific writable roots, and exact provider/requirement/recipe fingerprints. It must be ready for control-tower insertion into `production_launch_runtime` without duplicating the runtime host or launch transaction.

**Do not:** edit `production_launch_runtime.*`, host IPC, provider resolver, CMake/shared docs, or add arbitrary scripts/shell/CLR/bypass behavior. Return one exact integration note for the final production insertion point.

---

### CHUNK-PRODUCTION-ACTIVATION-BRIDGES - Implement real fail-closed Gate-C input and recovery resource bridges

**Primary gap:** `IProductionInputResourceBridge` and `IProductionRecoveryResourceBridge` exist, but the repository has no production implementations; default Host composition therefore cannot satisfy plans that require real input isolation/recovery resources.

**Allowed production envelope:**

- a new narrow `production_activation_bridges.*` module
- existing Gate-C/recovery/watchdog/reset public headers and source files only when a concrete safe adapter requires a narrowly scoped API exposure
- focused new bridge tests plus existing Gate-C recovery/watchdog tests

**Outcome:** provide concrete factories/adapters for the existing production bridge interfaces using only already-authoritative Gate-C and recovery/watchdog contracts. Activation must capture/prepare before mutation, verify exact Seat/session/generation ownership, roll back in reverse order, and return RecoveryRequired when cleanup cannot be verified. Missing physical acceptance must remain a policy gate; controlled tests must not manufacture physical evidence.

**Do not:** edit `production_launch_runtime.*`, `host_transport.*`, compatibility materialization files, CMake/shared docs, or create a second input/recovery manager. If the existing public Gate-C/recovery API cannot safely express the bridge, add only the minimal narrow API and report the exact final composition call the control tower must make.

---

### CHUNK-LAUNCHER-UI-PRIMITIVES - Finish the HUFS-derived game-first native launcher presentation layer

**Primary gap:** the control tower already ported the user's `C:\hufs-mc-web` Admin Source design tokens into HydraSeat's Win32 launcher (warm ivory canvas, raised light surfaces, navy hierarchy/focus, restrained bronze accent, semantic status colors). The next presentation layer remains basic Win32 stock controls: the game library is a plain `LISTBOX`, action hierarchy is not owner-drawn, Seat status is mostly text-only, and the narrow/DPI behavior has not had a focused implementation pass.

**Allowed production envelope:**

- `include/hydra/launcher_layout.hpp`
- `src/launcher_layout.cpp`
- `src/launcher_win32.cpp`
- `include/hydra/launcher_ui_model.hpp`
- `src/launcher_ui_model.cpp` only if a narrow presentation-only field/helper is required
- `tests/test_launcher_ui_model.cpp`
- `tests/test_ui_accessibility.cpp`
- `tests/test_ui_localization.cpp` only for exact launcher-facing regression coverage
- one narrowly scoped new launcher presentation test/helper if required

**Outcome:** implement a bounded owner-drawn game-library row and reusable native button/status primitives consistent with `docs/implementation/UI_REFERENCE_PORT.md` and `C:\hufs-mc-web\apps\admin-source\src\styles\{tokens,primitives,shell}.css`; preserve game-first flow; add semantic Seat/readiness markers using shape/text plus color; retain keyboard/focus/disabled-reason behavior; keep Windows High Contrast on system colors; keep the same underlying launcher/model authority; support 96/120/144/192 DPI and narrow-window layout without duplicating control state.

**Do not:** edit `src/gui_win32.cpp` (currently claimed by trusted-runtime composition), Host/runtime/input/audio/compatibility code, CMake/shared docs, or move readiness authority into the paint layer. Do not copy HUFS branding/campus identity/Minecraft artwork; only reuse the user's own structural UI code/design language. Do not add web-runtime dependencies or replace the native launcher with WebView/Qt.

---

### CHUNK-ACTIVATION-CONTEXT-AUTHORITY - Expose exact Seat/session/process activation context to production bridges

**Primary gap:** `CHUNK-PRODUCTION-ACTIVATION-BRIDGES` is blocked because bridge creation currently lacks the exact host session/session-generation/Seat-game-generation and staged owned process PID+creation identity required for safe Gate-C and recovery attachment.

**Allowed production envelope:**

- `include/hydra/production_launch_runtime.hpp`
- `src/production_launch_runtime.cpp`
- focused `tests/test_production_launch_runtime.cpp`
- one narrow new activation-context header/test helper if required

**Outcome:** introduce the smallest immutable production activation-context contract that exposes exact Seat/session/game generation and authoritative owned process identity to bridge/resource construction at the correct lifecycle point; no global rediscovery, name matching, or mutable caller-owned authority; preserve prepare-before-mutation and reverse rollback ordering.

**Do not:** edit Gate-C/recovery implementation, `two_seat_launch.*`, host transport, trusted requirement composition, compatibility adapter, CMake/shared docs. The result must unblock the bridge worker via a narrow public contract, not by implementing the bridges itself.

---

### CHUNK-COMPAT-PACKAGE-ROUNDTRIP - Preserve typed compatibility/materialization semantics through package import/export

**Primary gap:** compatibility execution phases and transactional instance materialization now exist, but setup/community package transport predates those semantics. Audit and close any loss of typed `InstanceRecipe`/compatibility identity/phase data across local package export/import without adding executable scripting.

**Allowed production envelope:**

- `include/hydra/setup_package.hpp`
- `src/setup_package.cpp`
- `include/hydra/community_setup.hpp`
- `src/community_setup.cpp`
- `include/hydra/community_package.hpp`
- `src/community_package.cpp`
- matching setup/community package tests
- profile schema only if a backward-compatible bounded schema field is strictly required

**Outcome:** trusted/local package round-trip either preserves every supported typed compatibility/materialization field exactly or explicitly rejects unsupported/newer data; stale/tampered/oversized/path-escaping recipes fail closed; package import never gains arbitrary script/shell/CLR authority; old supported packages remain deterministically handled.

**Do not:** edit production activation runtime, compatibility activation adapter, resolver, launcher, CMake/shared docs, or copy Nucleus handler scripting semantics.

---

### CHUNK-RECOVERY-PROCESS-ATTACHMENT - Exact process-bound watchdog/recovery registration authority

**Primary gap:** production recovery currently has no narrow reusable authority that can bind a recovery lease/action set to an exact Seat/session/game generation plus exact owned PID+creation identity once the process exists. This blocks safe production input/recovery bridge composition.

**Allowed production envelope:**

- `include/hydra/watchdog_protocol.hpp`
- `src/watchdog_protocol.cpp`
- `include/hydra/crash_journal.hpp`
- `src/crash_journal.cpp`
- `include/hydra/reset_actions.hpp`
- `src/reset_actions.cpp`
- matching watchdog/crash/reset focused tests
- one narrow new recovery attachment module if required

**Outcome:** provide a bounded/versioned exact-process recovery attachment contract that rejects stale PID reuse, wrong Seat/session/generation, duplicate/conflicting leases, and arbitrary commands; registration/disarm/rollback remain idempotent and verifiable; no process-name rediscovery; API is ready for the production bridge to consume once `CHUNK-ACTIVATION-CONTEXT-AUTHORITY` lands.

**Do not:** edit `production_launch_runtime.*`, Gate-C input implementation, host transport, compatibility files, CMake/shared docs, or implement a second watchdog service.

---

### CHUNK-CODEX-UX-ACCEPTANCE - Real Windows computer-use launcher UX and visual acceptance

**Primary gap:** automated launcher/layout/accessibility tests cannot prove that the current native Win32 product is visually coherent and practically usable on a real Windows desktop. The HUFS-derived owner-drawn launcher primitives are now implemented, so a computer-use capable reviewer should launch the real executable, capture screenshots, navigate with mouse/keyboard, exercise error/empty/disabled states, and perform narrow launcher-only fixes when the visual or interaction defect is directly observable.

**Allowed production envelope:**

- `include/hydra/launcher_layout.hpp`
- `src/launcher_layout.cpp`
- `src/launcher_win32.cpp`
- `include/hydra/launcher_ui_model.hpp`
- `src/launcher_ui_model.cpp` only for presentation-only defects
- focused launcher/UI/accessibility/localization tests
- a new `docs/qa/CODEX_UI_ACCEPTANCE_2026-08-30.md` report and screenshot/evidence files under a dedicated ignored or QA evidence location

**Outcome:** run the current Release build as a real user; capture and evaluate normal, empty, blocked, selected-game, Seat readiness, advanced/privacy, narrow-window, keyboard-focus, DPI/text-scale where safely testable, and Windows High Contrast states; fix only clearly reproduced launcher-presentation defects; preserve game-first flow and all runtime/preflight authority; report exact screenshots, interaction steps, visual defects, fixes, and remaining manual limitations.

**Do not:** edit `src/gui_win32.cpp` while trusted-runtime composition owns it, Host/runtime/process/input/audio/compatibility/privacy authority, CMake/shared roadmap docs, Windows system policy permanently, or mark physical/real-game/manual gates VALIDATED merely because the UI looks correct. Restore any temporary Windows appearance/accessibility setting changed during testing.

---

### CHUNK-SECONDARY-INPUT-HWND - Controlled secondary input-window ownership and reacquisition evidence

**Primary gap:** the window model now distinguishes visual and input targets, but the Nucleus-derived `REF-R03` risk still needs a dedicated controlled fixture proving that an owned secondary/invisible Raw-Input-style HWND can be selected, recreated and rejected safely without collapsing it back into the visual main window.

**Allowed production envelope:**

- `include/hydra/window_identity.hpp`
- `include/hydra/window_tracker.hpp`
- `src/window_tracker.cpp`
- `include/hydra/window_policy.hpp`
- `src/window_policy.cpp`
- focused window tests plus a dedicated controlled secondary-window fixture under `tests/`

**Outcome:** prove exact process-owned visual HWND and exact process-owned input-target HWND can differ; input-target recreation/reacquisition preserves Seat/process identity; unowned/other-Seat/same-class HWNDs are rejected; target ambiguity fails closed; the resulting typed target snapshot is ready for later Gate-C consumption without editing Gate-C in this chunk.

**Do not:** edit production activation bridges, Gate-C/input code, launcher/UI, process-group authority, CMake/shared docs, or use title/class text as ownership authority.

---

### CHUNK-INSTALLER-RECOVERY-HARNESS - Controlled install/update interruption and recovery acceptance harness

**Primary gap:** installer/update transaction code is heavily hardened, but P8-INST/P8-UPD still depend on later clean-machine/UAC/reboot acceptance. Before that manual gate, add a non-destructive controlled harness that exercises staged install/update interruption, stale expected-previous state, restart recovery and rollback against isolated temporary roots without claiming clean-machine validation.

**Allowed production envelope:**

- `include/hydra/installer_transaction.hpp`
- `src/installer_transaction.cpp`
- `include/hydra/update_transaction.hpp`
- `src/update_transaction.cpp`
- focused installer/update tests
- `tools/install_hydraseat.ps1`
- `tools/validate_release_installer_contract.py`
- one new controlled acceptance script/tool under `tools/` and test fixture data if required

**Outcome:** deterministic controlled scenarios cover interruption before/after staging/commit, stale previous-state CAS, corrupted transaction journal/state, rollback and restart recovery, exact owned-file preservation, concurrent mutation rejection and no partial committed state; harness is safe for a developer machine and clearly labels itself Controlled, not Clean-machine/Physical.

**Do not:** modify signing certificates, system-wide install state, services/drivers, production Host/runtime, CMake/shared docs, or weaken exact publisher/trust checks.

---

### CHUNK-RC-EVIDENCE-ORCHESTRATION - Deterministic release-candidate evidence pack assembly without evidence-class escalation

**Primary gap:** exact approval/privacy and acceptance campaign schema are now hardened, but the repository still needs a single deterministic local orchestration path that assembles an RC evidence pack from already-produced test/manual evidence, validates exact build/profile/input identities, and reports missing classes without fabricating them.

**Allowed production envelope:**

- `include/hydra/acceptance_campaign.hpp`
- `src/acceptance_campaign.cpp`
- `src/acceptance_campaignctl_main.cpp`
- `include/hydra/acceptance_probe.hpp`
- `src/acceptance_probe.cpp`
- matching acceptance tests
- `tools/run_v1_acceptance_campaign.ps1`
- `tools/validate_v1_acceptance_campaign.py`
- one new RC pack/orchestration tool under `tools/`
- `schemas/v1_acceptance_campaign_v1.schema.json` only if the already-current schema needs a narrowly justified consistency fix

**Outcome:** one command/workflow can initialize an exact RC campaign, ingest bounded evidence receipts, verify artifact hashes/build/profile/session/scenario/evidence class, produce a deterministic summary/pack, and list unmet Physical/Manual/Real-game/Clean-machine/Signing gates as missing rather than converting Controlled/Synthetic evidence into success.

**Do not:** edit STATUS/roadmap docs, invent evidence, run destructive physical tests, alter installer/runtime/input code, or make a missing evidence class pass by default.

---

### CHUNK-COMPAT-PRODUCTION-WIRING - Insert typed compatibility activation into the real production launch path

**Primary gap:** `ProductionCompatibilityActivation` is implemented and tested, but repository references remain definition/tests only. The production `HostProviderPlanRegistry` / `PlannedSeatGameInstance` path still does not construct and attach the compatibility lifecycle hook.

**Allowed production envelope:**

- `include/hydra/production_launch_runtime.hpp`
- `src/production_launch_runtime.cpp`
- focused `tests/test_production_launch_runtime.cpp`
- `include/hydra/production_compatibility_activation.hpp` read-mostly; edit only for a narrow integration API defect
- `tests/test_production_compatibility_activation.cpp` only for integration regression coverage

**Outcome:** for an exact trusted provider/Seat plan that carries supported materialization semantics, production instance creation constructs the exact compatibility activation identity/plan and attaches the existing lifecycle hook so PreSpawn/Startup/PostWindow/Runtime execute at the real launch boundaries; plans with no compatibility mutation remain unchanged; stale/mismatched fingerprints fail before mutation; rollback composes with resource/process rollback and retains RecoveryRequired on unverifiable cleanup.

**Do not:** edit `two_seat_launch.*`, package/community transport, production input/recovery bridge implementation, Host transport, launcher/UI, CMake/shared docs, or duplicate `RecipeExecutionSession`/`ProductionCompatibilityActivation` logic inside the runtime.

---

### CHUNK-REAL-STARTUP-WINDOW - Reproduce and eliminate the real HydraSeat.exe windowless startup hang

**Primary gap:** a computer-use acceptance pass rebuilt current x64 sources and twice reproduced `HydraSeat.exe` remaining alive for minutes without exposing any targetable top-level window. A temporary launcher-only harness could display the launcher, so this is a real executable startup/composition defect until disproven after current integration.

**Allowed production envelope:**

- `src/main.cpp`
- `include/hydra/gui_win32.hpp`
- `src/gui_win32.cpp`
- narrowly related startup/bootstrap source only if tracing proves it blocks before the first management window
- focused startup/GUI process tests and one new startup probe/helper under `tests/` if required

**Outcome:** establish a bounded startup state machine where the real Release executable either shows its first management window promptly or exits/fails with an explicit actionable diagnostic; no indefinite pre-window blocking on Host/provider/catalog/evidence initialization; expensive/read-only discovery may occur after HWND creation where safe; startup failures do not leave an invisible orphan process; reproduce before fixing and verify the real executable after the fix.

**Do not:** edit launcher presentation files currently governed by the separate UI findings, production launch runtime, process/Gate-C/recovery/package/installer/RC code, CMake/shared docs, or hide the defect with arbitrary sleeps/watchdog termination.

---

### CHUNK-UI-LOCALIZATION-POLISH - Close the real screenshot-backed launcher clipping and status-marker defects

**Primary gap:** computer-use QA reached the real current launcher presentation far enough to capture four objective defects before quota exhaustion: Korean selected-game eyebrow clipping at the standard 980x720 layout, `Both Seats` truncation, duplicated Seat ring/status marker, and owner-drawn game warning text collapsing to an unhelpful ellipsis. Automated rectangle tests did not catch these visual failures.

**Allowed production envelope:**

- `include/hydra/launcher_layout.hpp`
- `src/launcher_layout.cpp`
- `src/launcher_win32.cpp`
- `include/hydra/launcher_ui_model.hpp`
- `src/launcher_ui_model.cpp` only for presentation-only metadata/string handling
- `tests/test_launcher_ui_model.cpp`
- `tests/test_ui_accessibility.cpp`
- `tests/test_ui_localization.cpp`

**Outcome:** fix all four reproduced presentation defects without weakening runtime/readiness authority; critical localized labels and blocking reasons size/reflow from actual localized text and DPI rather than English assumptions; standard 980x720 and narrow layouts preserve readable primary actions/reasons; Seat state uses one non-duplicated shape+text cue; High Contrast and keyboard focus remain intact; add deterministic tests around the sizing/role decisions and report any remaining real screenshot verification requirement.

**Do not:** edit `src/gui_win32.cpp`, Host/runtime/process/input/audio/compatibility/installer/RC code, CMake/shared docs, or solve clipping by shrinking critical text below established readable metrics or hiding blocking reasons behind ellipsis.

---

### CHUNK-PRODUCTION-BUILD-REACHABILITY - Detect source-present-but-unlinked production modules before real executable QA

**Primary gap:** recent computer-use QA had to patch a disposable generated `.vcxproj` because a newly implemented production requirement source was present in repository source but absent from the current executable's generated build graph. The repository has accumulated multiple new production modules/factories, so static top-level `*.cpp` inventory alone is no longer enough to prove that production entrypoints actually link the implementations they reference.

**Allowed production envelope:**

- a new `tools/validate_production_reachability.py`
- focused Python self-test/fixture data under `tools/testdata/` or `tests/`
- `tools/validate_implementation_roadmap.py` only if a narrow invocation hook is appropriate and does not alter roadmap semantics
- read-only inspection of `CMakeLists.txt`, `cmake/*`, production entrypoints and source/header symbols

**Outcome:** add a deterministic repository validator that maps selected production executables/libraries to their declared implementation sources and fails when a non-test production symbol/factory used by an entrypoint is implemented in repository source but absent from all relevant target source/link closure. Include regressions for the exact trusted-requirement-source omission pattern, new production compatibility/activation bridge modules, duplicate implementation registration, test-only satisfaction, and generated-build/project hacks that do not repair tracked build authority. Produce exact control-tower CMake integration notes; do not edit CMake in this chunk.

**Do not:** modify generated `.vcxproj`/Ninja files, CMake/shared docs, production runtime logic, or treat header-only declarations/tests as proof that a real executable contains the implementation.

---

### CHUNK-RELEASE-ARTIFACT-PREFLIGHT - Deterministic release manifest, SBOM, provenance and checksum preflight

**Primary gap:** `P10-PKG-01` remains legitimately BLOCKED on license/signing/clean-install dependencies, but its automatable artifact-integrity work should be completed before those manual gates. The repository needs one deterministic preflight that inventories the exact tracked release payload, produces a canonical artifact manifest/checksums/SBOM/provenance description from an exact build identity, and proves tamper/missing/extra-file detection without pretending unsigned or developer-machine output is a qualified release.

**Allowed production envelope:**

- `config/release-*` metadata used for artifact declaration
- existing release/signing manifest inputs under `config/`
- `tools/sign_release_artifacts.ps1` read-mostly; edit only if a narrow deterministic manifest handoff is required
- existing release validators under `tools/`
- one new `tools/build_release_artifact_manifest.py` / `tools/validate_release_artifact_manifest.py` pair (or equally narrow names)
- deterministic fixture data/self-tests under `tools/testdata/release_artifacts/`
- no binary artifacts committed

**Outcome:** from an explicitly supplied exact build/output root and exact source revision identity, generate and validate a stable sorted manifest containing allowlisted release files, SHA-256, byte size, architecture/component role, source/build identity, SBOM component inventory and signing/provenance state; reject missing/extra/tampered/path-escaping/duplicate artifacts and generated-project workarounds; unsigned/developer output remains explicitly `Unsigned/Controlled` and cannot satisfy P8 signing or P10 package qualification. The result must be directly consumable by the RC evidence orchestrator later.

**Do not:** mark `P10-PKG-01` complete, invent a project license, sign with development/fake certificates as production evidence, edit installer/runtime/UI/input code, modify CMake/shared roadmap docs, or include arbitrary build-directory files outside the explicit release allowlist.

---

## Cross-chunk integration note format

When a required change is outside the claimed envelope, report exactly:

```text
OWNER: <required chunk or control tower>
FILE: <path>
ISSUE: <specific defect or missing API>
WHY IT MATTERS: <runtime/user/test impact>
REQUIRED API/CHANGE: <narrow contract>
BLOCKS: <what remains incomplete>
```

Do not solve the issue by editing another chunk.

## Concurrent operating model

The control tower may launch up to ten workers, one per explicitly selected READY chunk. It does not manufacture work merely to keep slots busy. For every batch it:

1. refreshes `python3 tools/chunk_claim.py list`;
2. checks that no prior worker is active;
3. gives each worker exactly one chunk ID, concrete touched paths and one acceptance target;
4. reviews DONE/BLOCKED claims and cross-chunk integration notes;
5. integrates CMake/shared docs itself;
6. reruns focused regressions, roadmap validation, `git diff --check`, and the broad x64/x86 matrix;
7. releases reviewed claim records before the next batch.

Physical/manual/real-game/clean-machine gates are scheduled separately and are never converted into implementation chunks merely so an AI worker can mark them complete.

---

## Fresh ten-worker reset batch - 2026-08-30

The user explicitly reset worker identity for this batch. All earlier agent names/owners are historical coordination records only. New workers must claim **only** one of the `CHUNK-N10-*` chunks below. The current repository contents remain the baseline; nobody may reset, clean, revert, or rewrite unrelated existing changes.

### CHUNK-N10-BUILD-GRAPH - Repair tracked production CMake reachability

**Allowed production envelope:** `CMakeLists.txt`, `cmake/*` only.

**Outcome:** make `python3 tools/validate_production_reachability.py` pass with zero errors by repairing canonical target ownership/link closure for the trusted requirement resolver, production launch installer, production compatibility activation, and production activation bridges. Preserve one canonical owner per implementation and existing target boundaries; build the real x64 Release `HydraSeat` and `hydra_host` plus focused production tests.

**Do not:** edit production C++/headers/tests, generated `.vcxproj`/Ninja files, shared docs, or hide missing edges by directly duplicating `.cpp` files into executables.

---

### CHUNK-N10-RECOVERY-ATTACHMENT - Watchdog/crash/reset exact-process recovery attachment audit

**Allowed production envelope:** `include/hydra/recovery_process_attachment.hpp`, `include/hydra/watchdog_protocol.hpp`, `src/watchdog_protocol.cpp`, `include/hydra/crash_journal.hpp`, `src/crash_journal.cpp`, `include/hydra/reset_actions.hpp`, `src/reset_actions.cpp`, `tests/test_watchdog_protocol.cpp`, `tests/test_crash_journal.cpp`, `tests/test_reset_actions.cpp`, `tests/test_watchdog_process.cpp`, `tests/test_reset_process.cpp`.

**Outcome:** fresh adversarial audit of exact Seat/session/session-generation/Seat-game-generation/PID+creation/recovery-epoch attachment authority across watchdog registration, crash journal persistence and reset execution. Stale PID reuse, conflicting duplicate leases, cross-Seat/session/generation input, downgrade/legacy ambiguity and partial rollback must fail closed; exact duplicate registration/disarm stays idempotent and safe restart/recovery remains verifiable.

**Do not:** edit production activation bridges, HidHide/Gate-C input code, production launch runtime, Host transport, CMake/shared docs, or invent physical/reboot evidence. Return integration notes if a bridge consumer needs a public API change outside this envelope.

---

### CHUNK-N10-COMPAT-WIRING - Trusted local compatibility materialization authority and production lifecycle wiring

**Allowed production envelope:** `include/hydra/compatibility_local_store.hpp`, `src/compatibility_local_store.cpp`, `include/hydra/game_runtime_requirement_resolver.hpp`, `src/game_runtime_requirement_resolver.cpp`, `include/hydra/instance_materialization.hpp`, `include/hydra/production_compatibility_activation.hpp`, `src/production_compatibility_activation.cpp`, `include/hydra/production_launch_runtime.hpp`, `src/production_launch_runtime.cpp`, `tests/test_compatibility_local_store.cpp`, `tests/test_game_runtime_requirement_resolver.cpp`, `tests/test_production_compatibility_activation.cpp`, `tests/test_production_launch_runtime.cpp`.

**Outcome:** close the authority gap without treating imported/community setup bytes as runtime mutation authority. A fresh locally trusted setup/materialization decision plus fresh trusted requirement snapshot must compile the exact Seat/session/provider/revision/fingerprint-bound `InstanceMaterializationPlan`, then attach the existing `ProductionCompatibilityActivation` lifecycle hook to the real `PlannedSeatGameInstance`. No-materialization plans remain unchanged; stale/mismatched/community-only authority fails before mutation.

**Do not:** add arbitrary scripts/shell/CLR/registry execution, edit package/community transport, CMake, Host transport, activation bridges, UI, or shared docs.

---

### CHUNK-N10-STARTUP-WINDOW - Real executable first-window responsiveness and explicit startup failure

**Allowed production envelope:** `src/main.cpp`, `include/hydra/gui_win32.hpp`, `src/gui_win32.cpp`, `tests/test_hydra.cpp`.

**Outcome:** after the tracked build graph is usable, reproduce the current real Release startup behavior. Ensure the product either exposes its first management HWND promptly or exits/fails with a bounded actionable diagnostic; no indefinite pre-window blocking on provider/catalog/evidence/Host initialization and no invisible orphan process. Only fix a defect actually reproduced against the real tracked build.

**Do not:** edit launcher presentation, Host/runtime/protocol, resolver internals, production launch runtime, CMake/shared docs, or mask the problem with arbitrary sleeps/timeouts that merely kill the process.

---

### CHUNK-N10-LAUNCHER-UX - Native launcher localization/accessibility/DPI regression pass

**Allowed production envelope:** `include/hydra/launcher_layout.hpp`, `src/launcher_layout.cpp`, `include/hydra/launcher_ui_model.hpp`, `src/launcher_ui_model.cpp`, `src/launcher_win32.cpp`, `tests/test_launcher_ui_model.cpp`, `tests/test_ui_accessibility.cpp`, `tests/test_ui_localization.cpp`.

**Outcome:** independently audit the current owner-drawn launcher after the recent polish. Preserve readable critical labels/reasons and one shape+text Seat cue across `en-US`/`ko-KR`/`zh-CN`, 96/120/144/192 DPI, standard and narrow layouts, keyboard focus, disabled reasons, and High Contrast. Fix only presentation defects; readiness/launch authority remains in existing models/runtime.

**Do not:** edit `src/gui_win32.cpp`, runtime/Host/input/audio/compatibility code, CMake/shared docs, or hide blocking text behind ellipsis/shrinking below readable metrics.

---

### CHUNK-N10-PROCESS-WINDOW - Process handoff and HWND reacquisition authority audit

**Allowed production envelope:** `include/hydra/process_group.hpp`, `src/process_group.cpp`, `include/hydra/process_launcher.hpp`, `src/process_launcher.cpp`, `include/hydra/window_identity.hpp`, `include/hydra/window_tracker.hpp`, `src/window_tracker.cpp`, `include/hydra/window_policy.hpp`, `src/window_policy.cpp`, `include/hydra/window_placement.hpp`, `src/window_placement.cpp`, `tests/test_process_group.cpp`, `tests/process_tree_child.cpp`, `tests/test_window_tracker.cpp`, `tests/test_window_policy.cpp`, `tests/window_test_app.cpp`.

**Outcome:** fresh adversarial audit of launcher/loader handoff and visual/input HWND recreation. Exact PID+creation/owned Job lineage remains authoritative; PID reuse, same-name/unowned helpers, cross-Seat HWNDs, ambiguous secondary input HWNDs and stale generations fail closed; stop/recovery leaves zero owned orphans and no stolen windows.

**Do not:** edit Seat lifecycle/production runtime, Gate-C, display routing, launcher UI, CMake/shared docs, or broaden process/window discovery by name/title/class as authority.

---

### CHUNK-N10-CONTROLLER-INPUT - Controller/XInput/DirectInput consistency hardening

**Allowed production envelope:** `include/hydra/controller_runtime.hpp`, `src/controller_runtime.cpp`, `src/controller_native.cpp`, `include/hydra/virtual_xinput_state.hpp`, `src/virtual_xinput_state.cpp`, `include/hydra/directinput_policy.hpp`, `src/directinput_policy.cpp`, `tests/test_controller_runtime.cpp`, `tests/test_virtual_xinput_state.cpp`, `tests/test_directinput_policy.cpp`, `tests/test_gate_c_xinput_adapter.cpp`.

**Outcome:** verify one current Seat generation consistently governs state/capabilities/battery/rumble and DirectInput visibility/order. Disconnect/reconnect/stale generation/cross-Seat output must fail closed; x86/x64 public layouts and ordinal-compatible XInput behavior stay deterministic. Add/fix tests only where a real invariant gap exists.

**Do not:** claim physical controller validation, edit Gate-C transport/shim internals outside the listed focused test, adopt retired ViGEm/reference code, or edit CMake/shared docs.

---

### CHUNK-N10-AUDIO - Exact-process Core Audio routing lifetime and rollback audit

**Allowed production envelope:** `include/hydra/audio_routing.hpp`, `src/audio_routing.cpp`, `src/audio_session_native.cpp`, `tests/test_audio_routing.cpp`.

**Outcome:** independently stress exact PID+creation audio-session ownership, late-session observation, apply/verify/rollback/recovery and object lifetime on x64/x86. No aggregate-initialization UB, stale PID reuse, cross-Seat routing, dangling native lifetime, or false success when the native backend cannot verify movement. Preserve observe-only fallback when unsupported.

**Do not:** edit process authority, endpoint inventory, runtime activation ordering, CMake/shared docs, or invent physical audible-routing evidence.

---

### CHUNK-N10-HOST-RUNTIME - Host protocol/transport/runtime isolation and replay audit

**Allowed production envelope:** `include/hydra/host_protocol.hpp`, `src/host_protocol.cpp`, `include/hydra/host_transport.hpp`, `src/host_transport.cpp`, `include/hydra/runtime_host.hpp`, `src/runtime_host.cpp`, `tests/test_host_protocol.cpp`, `tests/test_host_process.cpp`, `tests/test_runtime_host.cpp`, `tests/test_production_host_protocol.cpp`.

**Outcome:** adversarially verify bounded protocol parsing, correlation/replay handling, reconnect/resnapshot, exact Seat/session generation, one-Seat failure isolation, provider-plan transaction authority and third-Seat rejection. Client messages/snapshots must never become runtime authority; stale/duplicate/out-of-order input must be deterministic and fail closed without stopping the healthy Seat.

**Do not:** edit `src/host_main.cpp`, production launch runtime, activation bridges, UI, CMake/shared docs, or weaken protocol version/bounds to make tests pass.

---

### CHUNK-N10-INSTALL-TRUST - Installer/update/trust/release boundary hardening

**Allowed production envelope:** `include/hydra/artifact_trust.hpp`, `src/artifact_trust.cpp`, `include/hydra/privilege_broker.hpp`, `src/privilege_broker.cpp`, `include/hydra/installer_transaction.hpp`, `src/installer_transaction.cpp`, `include/hydra/update_transaction.hpp`, `src/update_transaction.cpp`, `include/hydra/production_launch_installer.hpp`, `src/production_launch_installer.cpp`, `tests/test_artifact_trust.cpp`, `tests/test_privilege_broker.cpp`, `tests/test_installer_transaction.cpp`, `tests/test_update_transaction.cpp`, `tools/install_hydraseat.ps1`, `tools/validate_release_installer_contract.py`, `tools/run_installer_recovery_harness.py`, `tools/build_release_artifact_manifest.py`, `tools/validate_release_artifact_manifest.py`, `config/release-artifact-preflight.json`, `tools/testdata/installer_recovery/*`, `tools/testdata/release_artifacts/*`.

**Outcome:** audit exact publisher/hash/architecture trust, same-lock expected-previous update CAS, interrupted install/update recovery, reparse/path boundaries, owned-file preservation, production launch-installer reachability assumptions, and deterministic release artifact manifest validation. Controlled/developer output must stay clearly non-production and unsigned evidence cannot satisfy signing/clean-machine gates.

**Do not:** touch real certificates/signing secrets, system-wide install state, Host/runtime/UI/input code, CMake/shared docs, or mark clean-machine/UAC/reboot/signing gates validated.

---

## Wave 2 - GitHub mainline hardening

These chunks reuse completed workers while Agents 02/03/04 continue their active core work. Wave-2 workers must stay off every active core claim and focus on integration confidence rather than expanding product scope.

### CHUNK-N10B-PREMERGE-GATE - Deterministic one-command pre-merge gate

**Allowed production envelope:** `tools/run_premerge_gate.py`, `tools/testdata/premerge_gate/*`, and one focused Python self-test under `tools/` if needed.

**Outcome:** build a deterministic, non-destructive pre-merge orchestrator that runs the repository's existing roadmap, production-reachability, release-scope/artifact, syntax/static validators and reports which x64/x86 build/test/manual classes are PASS, FAIL, SKIPPED or still external/manual. It must never convert missing physical/real-game/clean-machine/signing evidence into success and must return non-zero on an automated gate failure.

**Do not:** edit CMake, product C++/headers/tests, shared docs, Git state, remote state, or invent new product requirements.

---

### CHUNK-N10B-UI-RELEASE-QA - Release-facing launcher QA ledger and automated readiness checks

**Allowed production envelope:** `docs/qa/LAUNCHER_RELEASE_QA.md`, `tools/validate_launcher_release_readiness.py`, `tools/testdata/launcher_release_readiness/*`.

**Outcome:** codify the current launcher acceptance matrix for en-US/ko-KR/zh-CN, DPI/narrow layout, keyboard/focus, disabled reasons, High Contrast and first-window visibility. Automatically verify what repository tests can prove and explicitly leave screenshot/computer-use/physical items pending. Produce a concise defect/remaining-evidence ledger suitable for control-tower pre-merge review.

**Do not:** edit launcher/UI production code, shared roadmap/status docs, CMake, or claim screenshot/High-Contrast/manual acceptance unless actually performed.

---

### CHUNK-N10B-PROCESS-SOAK - Repeated process/window lifecycle soak harness

**Allowed production envelope:** `tools/run_process_window_soak.py`, `tools/testdata/process_window_soak/*`.

**Outcome:** create a bounded repeatable developer-machine soak runner around the existing process-group/window-tracker/window-policy controlled tests. Detect intermittent PID-reuse, handoff, HWND-recreation, teardown/orphan or timeout failures across repeated iterations and emit a deterministic summary without changing production authority.

**Do not:** edit process/window production code or tests, CMake/shared docs, kill unrelated processes, or treat controlled soak as real-game/physical evidence.

---

### CHUNK-N10B-ABI-CONTRACT - Public protocol/ABI fixed-width and architecture audit

**Allowed production envelope:** `tools/validate_public_abi_contracts.py`, `tools/testdata/public_abi_contracts/*`.

**Outcome:** statically audit selected Gate-C/Host/public C ABI contracts for fixed-width fields, explicit version/size bounds, architecture-sensitive type leakage, reserved-field handling and x86/x64 parity expectations. Reuse existing headers/tests as read-only authority and fail on newly introduced pointer-size/native-ABI hazards.

**Do not:** edit protocol/runtime/controller/Gate-C production code or tests, CMake/shared docs, or broaden the public API.

---

### CHUNK-N10B-PUBLICATION-HYGIENE - GitHub publication and repository hygiene preflight

**Allowed production envelope:** `tools/validate_repository_publication_hygiene.py`, `tools/testdata/repository_publication_hygiene/*`.

**Outcome:** add a deterministic repository preflight that detects credentials/private keys, accidental absolute personal paths, generated build/project files, reference-repository leakage, unexpected binaries/archives, forbidden `.ai-bridge` artifacts, and release/publication claims inconsistent with the still-unresolved license/manual release gates. It reports blockers without deleting or rewriting user work.

**Do not:** edit Git history, `.gitignore`, README/license/shared docs, remove files automatically, access secrets, or push/create releases.

---

## UX / product hardening rescue batch - 2026-08-30

This batch is an explicit user-authorized exception to the earlier design-only pause. Ten workers may run concurrently, but this is **not** a feature-expansion wave. Every change must remain inside the current `README.md` and `docs/PRODUCT_V1.md` contract: exactly two Seats, game-first normal UX, minimal Seat UI, Windows local gaming, no VM/RDP/general desktop scope, no protection bypass, no new provider ecosystem, and no false release claims.

The screenshots captured on 2026-08-30 are the product baseline for this batch. The visible defects to address are: one physical keyboard/mouse appearing as many Raw Input/HID entries; drag-heavy hardware assignment; localized text clipping; inconsistent visual language between launcher/hardware/Seat surfaces; internal compatibility/diagnostic jargon in ordinary UI; Steam redistributable/tool entries appearing as games; and insufficient automated guards against UI/worktree regression.

Workers must prefer deleting/simplifying duplicated presentation logic over adding abstractions. Do not introduce a new broad manager/service, new framework, WebView/Qt rewrite, generic scripting system, third-Seat generalization, or speculative feature. If a correct fix requires another chunk's file, return an integration note instead of crossing the envelope.

### CHUNK-UXH-01-INPUT-IDENTITY - Collapse Raw Input/HID collections to stable physical keyboard/mouse inventory

**Allowed production envelope:** `include/hydra/hardware_identity.hpp`, `include/hydra/raw_input_utils.hpp`, `src/raw_input_utils.cpp`, `include/hydra/hardware_detector.hpp`, `src/hardware_detector.cpp`, `tests/test_hardware_identity.cpp`, `tests/test_hydra.cpp`.

**Outcome:** make the user-visible keyboard/mouse inventory represent stable physical devices rather than every Raw Input/HID collection/interface. Resolve a deterministic physical/container/ancestor identity using existing Windows SetupAPI/ConfigMgr authority, choose one representative runtime handle/path per physical device, preserve stable IDs across enumeration order, keep distinct physical devices distinct, and continue filtering obvious remote/synthetic devices. Add adversarial tests for multiple interfaces under one physical device, unrelated devices with similar names, missing property fallbacks, and deterministic ordering. Do not claim the screenshot machine is fixed until the user rechecks it physically.

**Do not:** use friendly name or enumeration order as identity, merge unrelated devices merely because VID/PID match, edit input routing/GUI/workspace persistence, add a driver, or change CMake/shared docs.

---

### CHUNK-UXH-02-INPUT-IDENTIFY - Intentional press/click-to-identify input capture semantics

**Allowed production envelope:** `include/hydra/input_observation.hpp`, `src/input_observation.cpp`, `tests/test_input_observation.cpp`.

**Outcome:** add the smallest reusable state/model needed for a Seat setup flow that says "press a key", "click a mouse button", or "press a controller button" and resolves exactly one stable device ID from an intentional event. Keyboard capture must require a key transition; mouse capture must ignore movement-only/wheel noise unless explicitly selected and prefer button transitions; cancellation/timeout/device removal/stale event/ambiguous shared-device cases must be deterministic and fail closed. The model must not mutate Seat configuration itself; it returns an exact candidate/result for the UI/control tower to apply.

**Do not:** edit Win32 UI, hardware enumeration, routing authority, persistence, Gate C, or introduce global hooks/raw typed-text logging.

---

### CHUNK-UXH-03-HARDWARE-SETUP-UI - Make Seat hardware setup human-usable without drag-only interaction

**Allowed production envelope:** `include/hydra/gui_win32.hpp`, `src/gui_win32.cpp`, `include/hydra/control_surface_model.hpp`, `src/control_surface_model.cpp`, `tests/test_control_surface_model.cpp`.

**Outcome:** repair the screenshot-backed hardware setup surface while preserving current runtime authority. A non-developer must be able to select a device tile and assign/unassign it to Seat 1 or Seat 2 without drag-and-drop; drag remains an optional shortcut. Use the existing launcher visual language where practical (clear hierarchy, consistent spacing/buttons/status, no raw device interface/PID/protocol text in normal state), distinguish selected/assigned/unassigned states by shape/text as well as color, and keep display/input/controller/audio concepts intact. If `CHUNK-UXH-02` exposes a usable identification API before this chunk finishes, integrate it only if that requires no cross-envelope edit; otherwise return the exact wiring note rather than duplicating identification logic.

**Do not:** change Host/runtime authority, persist runtime handles, add a wizard framework, localize by hard-coding three copies of strings, edit launcher files, CMake/shared docs, or make complete Seat setup mandatory before the user can save.

---

### CHUNK-UXH-04-LAUNCHER-LAYOUT - Locale/DPI text measurement and clipping-proof launcher geometry

**Allowed production envelope:** `include/hydra/launcher_layout.hpp`, `src/launcher_layout.cpp`, `tests/test_ui_accessibility.cpp`.

**Outcome:** make layout geometry derive from measured/wrapped critical text instead of English-sized fixed rectangles. Cover en-US/ko-KR/zh-CN length classes, 96/120/144/192 DPI, 980x720 and narrow supported widths, multiline actions such as Korean export/delete labels, selected-game title/status blocks, Seat actions, library rows and bottom launch reason. Critical actions/reasons must remain readable rather than ellipsized or shrunk below minimum metrics. Preserve one source of geometry truth and current native Win32 architecture.

**Do not:** edit `src/launcher_win32.cpp`, localization catalogs, runtime/model authority, add responsive-web abstractions, or solve clipping by silently increasing the product minimum size beyond a reasonable desktop window.

---

### CHUNK-UXH-05-LAUNCHER-IA - Simplify normal/advanced launcher information architecture and jargon

**Allowed production envelope:** `include/hydra/launcher_ui_model.hpp`, `src/launcher_ui_model.cpp`, `src/launcher_win32.cpp`, `tests/test_launcher_ui_model.cpp`.

**Outcome:** keep the ordinary path visibly centered on game -> Seat(s) -> Player(s) -> only blocking warning -> Play. Move or demote privacy retention, local compatibility result management, export/delete/clear controls and diagnostic detail so they do not compete with the happy path. Replace implementation-facing wording such as "compatibility evidence" in ordinary status with concise user-action language while preserving exact detail in advanced/diagnostic views. Reduce control density and duplicated status presentation rather than adding more panels. Preserve readiness/launch authority and accessibility semantics.

**Do not:** add new TextId definitions in localization files, hard-code untranslated replacement strings, edit launcher geometry files, hardware setup, Host/runtime, compatibility authority, CMake/shared docs, or remove capabilities that remain required by the README/product contract. Return localization integration notes for any missing message IDs.

---

### CHUNK-UXH-06-LOCALIZATION - Complete and normalize shipped EN/KO/ZH user-facing message catalog

**Allowed production envelope:** `include/hydra/ui_localization.hpp`, `src/ui_localization.cpp`, `tests/test_ui_localization.cpp`.

**Outcome:** audit the stable user-facing message catalog used by normal launcher/Seat/setup surfaces, ensure every shipping TextId has en-US fallback plus ko-KR and zh-CN, add narrowly necessary IDs for the screenshot-backed UX cleanup, and improve translations that are technically correct but too long/implementation-oriented for buttons/status. Keep machine-readable IDs/codes/CLI strings English and unlocalized. Tests must detect missing shipped-locale entries, placeholder/format mismatch and unreasonable critical-button length regressions without encoding one exact pixel width.

**Do not:** edit consuming UI source files, translate developer diagnostics/CLI/protocol identifiers, invent marketing claims, or change product behavior.

---

### CHUNK-UXH-07-SEAT-UI - Reduce per-Seat UI to the minimal v1 product surface and align visual language

**Allowed production envelope:** `include/hydra/seat_launcher_model.hpp`, `src/seat_launcher_model.cpp`, `src/seat_ui_main.cpp`, `tests/test_seat_launcher_model.cpp`, `tests/test_seat_ui_process.cpp`.

**Outcome:** make `hydra_seat_ui.exe` match `PRODUCT_V1.md`: compact Seat identity, Player, current/available game choice when idle, concise startup/readiness when needed, and End Playing/Restart or reconnect only when relevant. During Playing it should disappear or remain genuinely non-intrusive rather than showing a developer telemetry form. Remove redundant current/selected labels and internal fixture-looking presentation from the normal view, use the same warm-light/navy visual hierarchy as the Management launcher where native controls allow, and retain keyboard/accessibility behavior. Missing localization IDs become integration notes, not hard-coded multilingual strings.

**Do not:** create a desktop shell/taskbar/app launcher, edit Host/session authority, store account credentials, add arbitrary apps, or change CMake/shared docs.

---

### CHUNK-UXH-08-GAME-CATALOG - Hide Steam runtimes/tools/non-games from the normal game library

**Allowed production envelope:** `include/hydra/steam_provider.hpp`, `src/steam_provider.cpp`, `include/hydra/game_catalog.hpp`, `src/game_catalog.cpp`, `tests/test_steam_provider.cpp`, `tests/test_game_catalog.cpp`.

**Outcome:** prevent entries such as Steamworks Common Redistributables and other clearly non-playable runtime/tool/dedicated-server/support packages from appearing as ordinary games. Prefer bounded provider metadata/classification over brittle title-name blacklists; preserve unknown legitimate games conservatively, keep manual Add EXE behavior, and make filtering deterministic/tested. Provider/catalog parsing remains read-only and bounded.

**Do not:** scrape network metadata, hard-code the screenshot title as the only fix, hide games solely because compatibility is unknown, alter launch authority, or edit UI/CMake/shared docs.

---

### CHUNK-UXH-09-UX-REGRESSION-GATE - Turn screenshot findings into an automated UX readiness contract

**Allowed production envelope:** `docs/qa/LAUNCHER_RELEASE_QA.md`, `tools/validate_launcher_release_readiness.py`, `tools/testdata/launcher_release_readiness/*`.

**Outcome:** extend the release-facing QA gate so future green test counts cannot be misreported as good UX. Encode what can be checked automatically: three shipped locales, supported DPI/narrow geometry policy, no critical-button clipping contract, game-first primary action order, non-drag hardware assignment availability once wired, minimal Seat UI contract, diagnostics/compatibility-detail separation, and first-window responsiveness. Keep screenshot/human visual approval explicitly manual and PENDING. Fixtures must prove failures are detected without reading build-output/generated authority.

**Do not:** modify product source, CMake/shared roadmap/status docs, fabricate screenshot acceptance, or make subjective color/style taste a brittle static test.

---

### CHUNK-UXH-10-WORKTREE-HYGIENE - Non-destructive worktree/slop hygiene gate for integration

**Allowed production envelope:** `tools/run_premerge_gate.py`, `tools/testdata/premerge_gate/*`, new `tools/validate_worktree_hygiene.py`, new `tools/testdata/worktree_hygiene/*`.

**Outcome:** add a bounded non-destructive integration check for repository junk that automated coding waves tend to leave behind: root `.obj`/temporary binaries, generated IDE/build files outside ignored build roots, accidental scratch/evidence files, personal absolute paths in candidate changes, `.ai-bridge` artifacts, duplicate throwaway test outputs and suspicious untracked release payloads. Integrate the check into the existing pre-merge runner without deleting anything. Distinguish intentional internal `.agents` coordination files from publication payload, and never scan secrets or unrelated user directories.

**Do not:** run `git clean/reset`, modify `.gitignore`, delete/move user files, inspect secret contents, edit product source/shared docs, or treat a clean worktree as proof of product correctness.

---

### CHUNK-V1H-01-PLAYER-STATE - Durable Player profile and last-selection persistence boundary

**Allowed production envelope:** new `include/hydra/launcher_user_state.hpp`, new `src/launcher_user_state.cpp`, new `tests/test_launcher_user_state.cpp` only.

**Outcome:** create one small UI-independent persistence boundary for launcher user state. It must support transactional PlayerProfileDocument load/save plus last Player 1 / optional Player 2 selection load/save under an injectable root for deterministic temp-directory tests. Existing production paths are `%LOCALAPPDATA%\HydraSeat\players.json` and a bounded selection-state file, but the module must not hard-wire the GUI or HWNDs. Writes stage + flush + atomic replace, malformed/oversize/stale IDs fail closed, and selection restore never invents a Player. This chunk prepares the reusable storage authority; launcher wiring remains control-tower/CHUNK-V1H-03 work.

**Do not:** edit `src/launcher_win32.cpp`, CMake, shared docs, runtime/Host authority, hardware UI, or Git state. Do not duplicate profile schema validation or introduce a database/framework.

---

### CHUNK-V1H-02-INSTALLER - Real double-click Windows installer/bootstrapper over the existing transactional contract

**Allowed production envelope:** new `include/hydra/installer_bootstrap.hpp`, new `src/installer_bootstrap.cpp`, new `src/installer_bootstrap_main.cpp`, new `tests/test_installer_bootstrap.cpp`, `tools/install_hydraseat.ps1`, new `tools/build_installer_package.ps1`, `tools/validate_release_installer_contract.py`, `config/release-signing-manifest.json`, `config/release-artifact-preflight.json`, `tools/testdata/release_artifacts/*`, `tools/testdata/installer_recovery/*`.

**Outcome:** close the gap between the existing signed PowerShell transaction engine and `realWindowsInstallerRequired: true`. Implement a thin native Win32 `HydraSeatSetup.exe` bootstrapper suitable for double-click use that presents Install/Repair/Uninstall clearly, performs non-elevated package/prerequisite inspection first, requests elevation only for mutation, and invokes the existing signed/validated PowerShell installer contract rather than reimplementing ownership/rollback logic. Add deterministic packaging support for the exact x64 release payload and extend release/signing/preflight contracts so the setup bootstrapper is an explicit reviewed artifact. No network installer dependency is allowed. The result may remain clean-machine/signing PENDING, but source/build/package contracts must be real rather than a mock UI.

**Do not:** replace the transactional installer with NSIS/WiX/Inno downloaded at runtime, weaken signature/provenance checks, add a general elevated command runner, modify CMake/shared README/status docs, or claim clean-machine validation.

---

### CHUNK-V1H-03-GAMES-UX - Bring the ordinary Games screen to V1 hands-on quality

**Allowed production envelope:** `include/hydra/launcher_layout.hpp`, `src/launcher_layout.cpp`, `src/launcher_win32.cpp`, `include/hydra/launcher_win32.hpp`, `include/hydra/launcher_ui_model.hpp`, `src/launcher_ui_model.cpp`, `tests/test_launcher_ui_model.cpp`, `tests/test_ui_accessibility.cpp`.

**Outcome:** make the first screen self-explanatory for a non-developer: quick Player creation, required Player 1, clearly optional Player 2, game library, Seat/Display Setup navigation and Play/readiness. Remove or delete unreachable legacy Seat-use/Settings/Diagnostics remnants that still distort layout/state. Preserve the recently fixed Korean clipping and game-first flow. Read the Nucleus reference only for interaction/flow ideas under the clean-room rule; no GPL code/algorithm copying. Microsoft WinUI/Windows App SDK samples may inform layout/control behavior but do not rewrite the shell to WinUI in this chunk. Integrate the CHUNK-V1H-01 persistence boundary only if that module is already present and stable; otherwise leave one exact integration note for control tower rather than duplicating storage logic.

**Do not:** modify Hardware Setup implementation, installer/release files, Host/runtime authority, protection boundaries, CMake/shared docs, or hard-code untranslated new strings. Missing TextIds become integration notes.

---

### CHUNK-V1H-04-HARDWARE-UX - Finish Display/input assignment as a human-readable two-station setup screen

**Allowed production envelope:** `include/hydra/gui_win32.hpp`, `src/gui_win32.cpp`, `include/hydra/hardware_detector.hpp`, `src/hardware_detector.cpp`, `include/hydra/input_observation.hpp`, `src/input_observation.cpp`, `tests/test_hardware_identity.cpp`, `tests/test_input_observation.cpp`, `tests/test_control_surface_model.cpp`.

**Outcome:** preserve the working press/click identification and real monitor names, then make the two-display setup understandable without device IDs. Active/attached displays should be distinguishable by real friendly name, DISPLAY number, resolution and primary state; identical monitor models must remain distinguishable. Keyboard/mouse identification must have explicit waiting/success/ambiguous/timeout/removal feedback and flow directly into assignment. Repeated Games <-> Setup navigation must not leave stale selection or windows. Do not fake physical counts: one physical device with multiple HID collections collapses, two identical real devices remain distinct.

**Do not:** edit Games launcher files, CMake/shared docs, blacklist VID/PID or product names, force monitor/device counts, weaken stable identity, or manufacture physical validation evidence.

---

### CHUNK-V1H-05-UX-ACCEPTANCE - Mock-first V1 user-journey and release-facing hands-on regression gate

**Allowed production envelope:** new `tests/test_v1_user_journey.cpp`, new `tools/validate_v1_hands_on_readiness.py`, new `tools/testdata/v1_hands_on_readiness/*`, new `docs/qa/V1_HANDS_ON_QA.md` only.

**Outcome:** build a deterministic regression gate around the actual non-developer journey rather than existing implementation internals. Cover first launch, Player creation/persistence projection, required Player 1 / optional Player 2, Play readiness, real-display presentation requirements, keyboard/mouse intentional-identification states, Games <-> Hardware Setup lifecycle contract, Steam non-game filtering and installer-visible expectations where source authority can be checked. Separate Automated/Mock evidence from OpenAI Codex Computer Use and Physical/Real-game/Clean-machine evidence; never promote one class into another. The validator should fail when old Seat-use/diagnostic normal-flow controls, clipped critical text contracts, missing display distinguishability, or missing persistence wiring reappear.

**Do not:** modify product source, CMake, installer implementation, shared README/status, synthesize screenshots, or report Computer Use/physical/manual PASS without such evidence.

---

## V1 Play-authority closure batch - 2026-08-31

Computer Use established a real V1 P1 after Add Game and Hardware Setup succeeded: the production resolver reads `%LOCALAPPDATA%\\HydraSeat\\runtime-requirements.json`, but no ordinary release-target flow writes trusted requirement authority. The launcher therefore remains disabled even for a correctly registered controlled target. This batch must close the missing writer/test plumbing without weakening the already-shipped trust model. `PhysicalOnly` remains production Play authority; ControlledProcess evidence may support a local test workflow but must never be relabeled or silently promoted to Physical.

### CHUNK-V1P-01-LOCAL-EVIDENCE-WRITER - Release-target local compatibility evidence persistence

**Allowed production envelope:** new `include/hydra/local_compatibility_evidence.hpp`, new `src/local_compatibility_evidence.cpp`, new `tests/test_local_compatibility_evidence.cpp` only. Existing compatibility/session-metrics/store modules are read-only dependencies.

**Outcome:** create one UI-independent release-target writer that takes an exact `compat::LocalEvidenceContext` plus a completed `metrics::SessionMetricsReport`, derives the canonical `compat::CompatibilityResult` through the existing conversion/validation contract, loads the bounded local compatibility history, records the result through `CompatibilityShareModel`, and atomically persists the updated history through `CompatibilityLocalStore`. Preserve existing history and retention semantics. ControlledProcess and Physical origins must remain exact; Synthetic evidence must not become a trusted release result. No caller may override the derived result origin/status after metrics conversion. All paths are injectable for tests; a production convenience path may use the existing fixed LocalAppData store.

**Do not:** launch processes, edit launcher/Host/runtime code, create requirement authority, modify compatibility schemas, weaken canonicalization/redaction, translate ControlledProcess into Physical, edit CMake/shared docs, or directly write ad-hoc JSON.

---

### CHUNK-V1P-02-REQUIREMENT-AUTHORITY-WRITER - Exact local requirement review and transactional authority publication

**Allowed production envelope:** new `include/hydra/runtime_requirement_authority.hpp`, new `src/runtime_requirement_authority.cpp`, new `tests/test_runtime_requirement_authority.cpp` only. Existing resolver/catalog/provider/session-metrics/compatibility types and stores are read-only dependencies.

**Outcome:** implement the missing bounded writer for `RequirementEvidenceDocument`. It must accept one exact current local catalog Game identity, exact current provider descriptor/revision, the canonical local `CompatibilityResult` that will be referenced, the originating `SessionMetricsReport`, and explicit reviewed `launch::Requirements`. Derive `launch::Capabilities` from actual metrics/evidence rather than trusting caller-supplied capability booleans; bind game/provider/AppID/version/hash/evidence/provenance revisions exactly; preserve unrelated records; replace only the exact Game authority with a monotonic bounded revision; validate the whole document; then use `GameRuntimeRequirementStore` transactional save. ControlledProcess records may be retained as controlled evidence but production `PhysicalOnly` resolution must continue rejecting them. Physical authority is publishable only when the supplied report/result are genuinely Physical and internally eligible; never synthesize physical eligibility.

**Do not:** change resolver `PhysicalOnly`, accept imported/community/synthetic evidence, infer unsupported capabilities, bypass high-risk approval, edit launcher/Host/CMake/shared docs, or make direct unvalidated JSON writes.

---

### CHUNK-V1P-03-LOCAL-CHECK-RUNNER - Bounded user-initiated local compatibility process check

**Allowed production envelope:** new `include/hydra/local_compatibility_runner.hpp`, new `src/local_compatibility_runner.cpp`, new `tests/test_local_compatibility_runner.cpp` only. Existing process-group/process-launcher/window/session-metrics APIs are read-only dependencies.

**Outcome:** create the smallest UI-independent backend for the Product V1 `manual/automatic local compatibility testing` promise. Given an already-resolved exact local executable `ProcessLaunchSpec`, run only a user-initiated bounded local check: exact owned process launch, bounded authoritative-window observation using existing ownership APIs, deterministic timeout/cancel, exact owned-process stop/cleanup, and a truthful `SessionMetricsReport`. This path exists to collect evidence before normal trusted Play, so it must not require `runtime-requirements.json` itself. It must never activate Gate-C/input isolation, audio/controller mutation, compatibility materialization, arbitrary shell commands, or protected-game bypass. Default result origin is ControlledProcess; Physical may only be represented by a separate explicit caller-supplied physical-observation authority that the runner itself cannot fabricate. Failure/timeout must clean only exact owned processes and report insufficient/failed evidence rather than success.

**Do not:** add a second general launcher, run arbitrary scripts/shells, scan/kill same-name unrelated processes, treat a visible window as proof of physical input isolation, edit normal Play/Host authority, persist evidence directly, edit CMake/shared docs, or hard-code `hydra_window_test_app.exe` into production behavior.

---

### CHUNK-V1P-04-PLAY-AUTHORITY-QA - Fail-closed regression gate for first-use Play authority

**Allowed production envelope:** new `tools/validate_v1_play_authority.py`, new `tools/testdata/v1_play_authority/*`, new `docs/qa/V1_PLAY_AUTHORITY_QA.md` only.

**Outcome:** encode the product gap Computer Use found so green unit tests cannot hide it again. The validator must distinguish: local compatibility evidence writer present; runtime requirement writer present; controlled evidence cannot become Physical; production resolver remains `PhysicalOnly`; missing/corrupt/stale authority still blocks normal Play; a user-facing local-check integration point is still required before RC; and Automated/Controlled/ComputerUse/Physical/RealGame evidence classes stay separate. Fixtures must prove the validator fails for no release writer, direct JSON injection, ControlledProcess-to-Physical promotion, permissive missing-authority fallback, or test-target hardcoding. The QA ledger should state exactly what future central UI integration and Computer Use must demonstrate.

**Do not:** modify product C++, CMake, shared roadmap/status/README, create fake runtime-requirements evidence, or claim the product-flow P1 is closed merely because writer modules exist.
