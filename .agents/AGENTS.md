# HydraSeat Agent Rules and Implementation Workflow

## 1. Project identity

HydraSeat is an experimental Windows local gaming multiseat project written primarily in C++20. It is being developed in public toward an open-source distribution model, but the repository license and contribution terms are not yet formally declared. Do not describe the current repository as legally open source or copy third-party source until that tracked legal gate is resolved.

The product goal is:

> Let two people share the unused CPU/GPU/memory/I/O headroom of one sufficiently capable Windows gaming PC as two local gaming Seats, reducing the need to buy and maintain a second complete desktop solely for simultaneous local gaming.

The primary human use case is a household or group of friends sharing the unused headroom of one capable gaming PC. HydraSeat v1 supports at most two active Seats, is game-only, and must optimize for a non-developer being able to optionally configure Seats, choose a game and Player for Seat 1/Seat 2/Both, play, let either Seat stop/change games independently, understand compatibility evidence, and safely return to ordinary Windows.

Keep these concepts separate: `Seat` = physical station hardware, `Player` = lightweight person profile, `Game` = installed/discovered title, `TwoPlayerSetup` = optional same-game/two-instance recipe, and runtime state = temporary Seat + Player + Game bindings. A Seat does not permanently own a person, game, account, save, process, or window.

Normal UX is a lightweight game launcher. Full per-Seat taskbars, wallpaper, clipboard virtualization, arbitrary general-purpose apps, and a replacement Windows shell are deferred beyond v1; an idle Seat gets only a minimal Seat Launcher when needed. Core operation is offline-first and least-privilege, community compatibility sharing is explicit opt-in, compatibility is reported as transparent success/failure evidence rather than an official certification badge, and executable/runtime/driver updates require user approval.

Same-game multi-instance support is in scope only when the game/provider already permit it. HydraSeat should automate repeatable setup where safe and also provide a guided manual setup path. Protected titles may be offered only as explicitly warned experiments and must never be presented as anti-cheat safety evidence.

A **Seat**, not a monitor or game window, is the primary ownership unit.

## 2. Mandatory source-of-truth order

Before changing code or documentation, read and obey in this order:

1. the user's current explicit instruction;
2. this file;
3. `docs/PRODUCT_V1.md`;
4. `docs/implementation/DECISIONS.md`;
5. `docs/implementation/README.md`;
6. `docs/implementation/STATUS.md`;
7. the active packet in the relevant `docs/implementation/PHASE*.md` file;
8. `docs/ARCHITECTURE.md` and specialized design/testing documents;
9. existing code and tests;
10. older comments, issue text, and historical notes.

When lower-precedence material conflicts with a higher-precedence decision, follow the higher-precedence source and update stale documentation in the packet scope.

## 3. Multi-packet execution with explicit dependencies

An implementation task may cover one packet or an ordered batch of multiple roadmap packets. Start from the current default packet or another explicitly selected actionable packet, then continue directly through additional READY/actionable packets when their declared prerequisites are satisfied. Independent packets may also be implemented in the same task when their changes do not conflict.

Required behavior:

- verify each selected packet's declared prerequisites before implementing that packet;
- preserve packet-specific state, tests, evidence, and manual-gate truth even when several packets are implemented in one task/branch/PR;
- do not skip undeclared prerequisites or mark a packet complete before its implementation/evidence exists;
- do not perform broad renames, dependency upgrades, generated asset changes, or unrelated refactors unless they are required by the selected packet batch and documented;
- if an undeclared prerequisite or design decision is required, update/propose the roadmap before implementing work that depends on it;
- update `docs/implementation/STATUS.md` with truthful evidence for every packet whose state changes.

Use `docs/implementation/CODEX_PLAYBOOK.md` as the standard workflow and prompt contract.

### Phase-close verification exception

The roadmap **phase** is the numbered product phase in `docs/ROADMAP.md` and `docs/implementation/README.md`, not a conversation turn or implementation batch.

Before a numbered phase may change to `Complete`, run one dedicated **Phase-close verification** task across the entire phase. This remains a separate review/verification gate after multi-packet implementation work.

The phase-close task must:

- re-read every packet, decision, architecture contract, test plan, and status entry that defines or was changed by that phase;
- inspect the complete phase-owned code and tests, including interactions between packets rather than reviewing only the final packet diff;
- re-run the broadest applicable phase regression matrix, including required Windows x64/x86, process/ABI, recovery, performance, and physical/manual gates;
- verify error paths, stale/duplicate input, resource ownership, rollback, teardown, no-cross-Seat behavior, compatibility claims, and documentation;
- identify temporary shortcuts, dead/unused paths, warning suppressions, TODOs, false capability claims, or assumptions that accumulated across the phase;
- record a phase-close evidence summary in `docs/implementation/STATUS.md`.

If the audit finds a defect, do not repair unrelated defects inside the audit. Reopen the owning packet or create one narrowly scoped repair packet/task, fix and validate it, then rerun the phase-close verification. The next numbered phase may start only after the prior phase's close verification passes, except for an already-declared cross-phase prerequisite explicitly permitted by the roadmap.

## 4. Current status and next task

`docs/implementation/STATUS.md` is dynamic and authoritative. Read it every time; do not rely on a remembered phase or packet.

Packet states:

- `BLOCKED`
- `READY`
- `IN_PROGRESS`
- `CODE_COMPLETE`
- `VALIDATED`
- `MERGED`
- `UPSTREAMED`
- `DEFERRED`
- `REJECTED`

`CODE_COMPLETE` means required automated implementation/tests pass but declared manual Windows/hardware/game acceptance remains pending. Never convert a manual gate to `VALIDATED` from source inspection, a synthetic test, or an assumption.

## 5. Non-negotiable architecture

Read the full decisions in `docs/implementation/DECISIONS.md`. At minimum:

- v1 supports at most two active Seats; do not expand product scope to N-Seat before v1;
- Seat hardware, Player identity, Game identity, Two-player setup, and Runtime Session are separate concepts;
- Seat setup may be incomplete and skipped/deferred; selected-game preflight determines which devices are actually required;
- normal UX is game-first and hides low-level implementation details outside Diagnostics/Expert mode;
- per-Seat game lifecycle is independent: one Seat may stop/change games while the other continues;
- v1 uses only a minimal idle Seat Launcher, not a general desktop shell/taskbar/clipboard/app environment;
- same-game setup has automatic and guided manual paths but never bypasses game/provider restrictions;
- compatibility is local-first evidence with optional redacted community sharing, not a maintainer certification badge;
- protected-game attempts require explicit risk acknowledgement and never imply anti-cheat safety;
- core operation is offline-first; compatibility-data refresh is optional; program/runtime/driver updates require user approval;
- normal operation uses least privilege and v1 requires a real Windows installer/uninstaller;
- Seat-first and multi-monitor-first architecture;
- default path is one Windows interactive session, not VM/RDP/streaming;
- game-first Management UI, background host, watchdog, reset CLI, optional adapters, and minimal Seat Launcher are separate responsibilities;
- the background host is authoritative for active runtime state;
- exactly one Management Seat owns the visible whole-machine control plane by default; Seat 1 is the default and the console opens on its primary display with a visible fallback;
- closing/restarting the control UI never stops an active Seat session; normal controls are `Start`, `Stop / Return to Windows`, and `Reconfigure`;
- `Stop / Return to Windows` means verified rollback to ordinary one-PC Windows behavior, not merely closing target windows;
- startup behavior is an explicit user mode: Manual, BackgroundIdle, or AutoActivateValidatedSession, and unsafe automatic preflight falls back to idle;
- all variable behavior is capability-planned and fails closed;
- controlled HydraSeat-owned probes precede any third-party process experiment;
- no anti-cheat, DRM, protected-process, security-product, credential, or integrity bypass;
- physical device cloaking is not equivalent to proven physical input suppression;
- physical/device/display/audio mutation occurs only after replacement/recovery paths are ready and is rolled back in reverse order;
- stable physical identities are used instead of enumeration order/friendly name;
- shared keyboard/mouse input is ambiguous for exclusive routing unless an explicit tested fan-out policy exists;
- protocols, schemas, manifests, and public ABI are versioned, bounded, fixed-width, and tested;
- physical multi-monitor support comes before optional virtual displays;
- window ownership follows validated process ownership;
- controller support is API-specific (XInput, DirectInput, Raw HID, SDL, vendor APIs are distinct);
- optional components are explicit, version/hash/license/trust checked, and never silently downloaded/executed;
- recovery is part of the feature, not later cleanup work;
- performance and compatibility claims require measurement/matrix evidence;
- shipping UI is localized through stable message IDs with `en-US` fallback and required `ko-KR`/`zh-CN` catalogs; source-code comments and developer-facing implementation notes remain English;
- machine-readable protocols, schema keys, CLI switches, diagnostic codes, capability/backend/profile identifiers, and packet IDs are never localized;
- read `docs/LOCALIZATION.md` before adding user-visible UI text or end-user documentation.

## 6. Clean-room and third-party boundaries

Read `docs/CLEAN_ROOM_POLICY.md` before external research or source use.

- Proprietary products such as ASTER: public documentation and ordinary observable behavior only.
- GPL/copyleft source: do not copy into a differently licensed core without an explicit compatible project decision.
- Public repositories with no license: behavior/documentation reference only.
- Permissive source: do not copy until HydraSeat's license, transitive dependencies, attribution, and notices are resolved.
- Reference repositories under `C:\HydraSeat\references` are read-only research inputs and never build inputs.
- Never modify/reference-checkout files, vendor them accidentally, or use leaked/non-public material.
- AI-generated text/code is not automatically clean-room; review provenance and suspiciously close structure/names/constants.

## 7. Standard implementation loop

For each selected packet in an implementation batch:

1. Read all mandatory documents and packet-linked files.
2. Verify prerequisites and current branch/worktree.
3. Write a short implementation map:
   - packet and outcome;
   - reused modules;
   - files/types/contracts;
   - thread/ownership model;
   - OS/persistent state touched;
   - rollback path;
   - automated tests;
   - manual gates left pending.
4. Inspect existing implementation/tests before editing.
5. Implement the smallest coherent scope required by that packet, then continue to the next actionable selected packet without requiring a new task boundary.
6. Add normal, boundary, malformed, stale/duplicate, failure, teardown, rollback, and no-cross-Seat tests as applicable.
7. Run strict local checks and Windows CI requirements.
8. Diagnose root causes; do not hide failures or weaken assertions.
9. Update status/architecture/user docs truthfully.
10. Run roadmap validator and `git diff --check`.
11. Review the complete diff for scope, safety, false claims, and generated files.
12. Commit only when requested or appropriate for the authorized repository workflow.
13. Do not push unless the user explicitly authorizes push/PR work.

## 8. Technical standards

### Language and build

- C++20, C11 for public C ABI smoke/compatibility tests.
- MSVC Release is the primary Windows validation path.
- Use `/W4 /permissive-` for new Windows targets.
- Portable code should pass available `-Wall -Wextra -Wpedantic`; use `-Wconversion -Wsign-conversion` for focused new low-level code where practical.
- Qt 6 may be used for UI, but low-level/core/runtime libraries must not depend on Qt solely for convenience.
- No compiler-specific public C++ ABI for cross-process or extension contracts.

### Ownership and Win32

- Use RAII for `HANDLE`, process/thread/job/event/module/hook/service/registry/COM ownership.
- Capture `GetLastError()` immediately after the failing call.
- Document thread affinity and initialization for COM/window/message-loop objects.
- Validate PID creation identity and HWND-to-process ownership to avoid reuse confusion.
- Pair every mutation with captured prior state, verification, and idempotent rollback.
- Never terminate/move/close an unowned process/window/device/session.

### Callbacks, workers, and queues

Latency-sensitive callbacks may perform bounded decode/state update/enqueue only. They must not wait on:

- another process or named-pipe write;
- disk/log flush;
- network;
- UI thread;
- driver install/configuration;
- unbounded allocation/retry.

Every queue documents producer/consumer, capacity, ordering, overflow, shutdown/drain/discard, timeout, and error propagation. Unbounded queues are forbidden.

### Protocols, schemas, ABI

Use:

- fixed-width fields;
- explicit byte order;
- magic/version/type/length where transported;
- maximum sizes/counts/depth;
- structure size/version for C ABI;
- reserved fields initialized/validated;
- malformed/future/stale/replay rejection;
- x86/x64 compatibility tests;
- no raw pointer or C++ object serialization.

### Persistent state

- validate completely before write;
- write transactionally with backup/migration report where user data is involved;
- failed migration/update leaves prior state usable;
- runtime handles/PIDs/HWNDs never persist as stable profile identity;
- crash journal contains only minimal recovery metadata and no secrets/raw input text.

### Errors and diagnostics

Use explicit typed results or a consistent custom result type. Diagnostics should include component, operation, Seat/session/process/device/backend/profile context, correlation/sequence, system code, guarantee level, and remediation where useful.

Default diagnostics must not include credentials, tokens, raw private file contents, unrelated process memory, or typed user text. Key codes require an explicit visible diagnostic mode and bounded retention.

## 9. Testing rules

### Pure/component tests

Cover:

- default and valid transitions;
- min/max and Unicode/path cases;
- malformed enums/booleans/sizes/versions;
- duplicate/stale/out-of-order messages;
- deterministic ordering/hash/serialization;
- partial startup and rollback at every action index;
- stop/reset idempotency;
- no state bleed between Seats/processes/adapter contexts;
- bounded overflow/backpressure;
- resource teardown.

### Windows process tests

- launch real controlled child processes;
- validate token/Seat/PID/architecture/session;
- enforce timeout;
- inspect child exit code;
- run clean and failure paths;
- confirm no child/helper/window/overlay remains;
- do not count parent process exit as proof of cleanup.

### Physical/manual tests

Agents may implement harnesses/checklists but must leave state `CODE_COMPLETE` until the user or an authorized tester records the declared hardware evidence.

Physical gates include keyboards/mice/controllers/displays/audio, device cloaking/suppression, reboot/startup, install/uninstall, game compatibility, and latency/zero-bleed measurement.

### Compatibility tests

A support claim names target/version/provider/Windows build/hardware topology/profile/backends and evidence date. One synthetic or one successful launch does not imply generic support.

## 10. Required verification commands

At minimum for roadmap/document changes:

```text
python tools/show_implementation_packet.py --current
python tools/show_implementation_packet.py --current --prompt
python tools/validate_implementation_roadmap.py
git diff --check
```

For Windows build/test packets:

```text
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build --build-config Release --output-on-failure
```

Run focused executables/tests named by the packet. Add x86 CI before claiming 32-bit adapter support. Run manual gates exactly as documented.

Do not change a test to accept wrong behavior merely to become green. If a premise is invalid, update the packet/decision and mark it blocked/rejected.

## 11. Roadmap/document maintenance

After packet work:

- `docs/implementation/STATUS.md`: exact state/evidence/next packet;
- active phase document: implementation details/checklist only when true;
- `docs/ROADMAP.md`: phase-level progress only;
- `docs/ARCHITECTURE.md`: component/contract changes;
- README: user-visible behavior/status only;
- compatibility/hardware matrix: physical/game evidence;
- clean-room/notices: dependency/source changes.

Run `tools/validate_implementation_roadmap.py`. Packet IDs must be unique, dependencies declared/acyclic, links valid, states consistent, and the current default packet must be actionable exactly as described by `STATUS.md` (for example `READY`, or a `CODE_COMPLETE` packet whose remaining work is the declared manual acceptance).

## 12. Project structure

Current and target areas:

```text
include/hydra/                 Core/internal headers
src/                           Implementations and executable entry points
ui/                            Optional Qt UI
sdk/                           Future public SDK only after Phase 9
schemas/                       Versioned persisted/package/profile schemas
components/ or packages/       Optional packaged components when introduced
tests/                         Unit/component/process tests
fixtures/                      Sanitized deterministic test fixtures
tools/                         Validators, acceptance/report tools
docs/implementation/           Codex work packets and execution ledger
docs/compatibility/            Future machine-readable support matrix/docs
docs/security/                 Threat models/security policy
.agents/AGENTS.md               Agent entrypoint
.github/workflows/              CI/release workflows
```

Important existing modules include hardware detection, Seat/workspace manager, input router/observation/planner, Gate A/B labs, Gate C protocol/transport/adapter/host/target, display manager skeleton, launcher skeleton, and Win32 UI.

Do not create a new broad `manager` class when an existing responsibility/interface fits. Follow packet-specified file names unless a better necessary name is documented in the packet/status update.

## 13. `.gitignore` and generated files

When creating build directories, traces, logs, temporary profiles, reports, downloaded/staged packages, or generated fixtures:

- add precise patterns to repository `.gitignore` immediately;
- do not ignore source/config/evidence broadly;
- do not commit local `.ai-bridge` scratch files;
- inspect staged files before commit;
- never commit credentials, tokens, signing material, personal absolute paths, crash dumps with private data, or third-party reference repositories.

## 14. Git and remote policy

- Inspect status/diff before and after work.
- Use a task/batch-specific branch that clearly identifies the implemented packet set.
- Keep commits reviewable; a commit or PR may cover one or more clearly identified packets when the dependency/order is coherent.
- Do not rewrite unrelated user changes.
- Do not use destructive reset/clean/checkout over user work without explicit authorization.
- Local commit only when requested/appropriate to the active workflow.
- **Do not execute `git push`, create/merge PRs, or mutate remote state unless the user explicitly authorizes it.**
- When authorized, validate the exact pushed SHA in Windows CI, merge/update fork default branch as requested, and update the upstream PR without false completion claims.

## 15. Stop and report instead of guessing

Stop the packet and mark/report a blocker when:

- a required decision is absent/contradictory;
- the apparent path violates license/clean-room/security policy;
- anti-cheat/protected-process bypass would be required;
- manual hardware result is needed to choose architecture;
- rollback cannot be defined;
- required component/hardware/toolchain is unavailable and no deterministic fake proves the invariant;
- tests disprove the packet premise;
- implementation would require silent persistent/elevated/system-wide mutation.

Partial completion with truthful evidence is preferred over a fabricated green state.

## 16. Final review checklist

Before reporting completion:

- [ ] selected packet set and prerequisites are correct;
- [ ] Seat-first/multi-monitor architecture preserved;
- [ ] changes stay within the declared multi-packet batch plus documented prerequisites;
- [ ] unsupported behavior fails closed;
- [ ] no false isolation/game/support claim;
- [ ] contracts versioned/bounded;
- [ ] ownership/thread/queue/rollback explicit;
- [ ] success and failure tests pass;
- [ ] Windows CI required by packet passes;
- [ ] manual gates remain pending unless real evidence recorded;
- [ ] roadmap validator and `git diff --check` pass;
- [ ] docs/status/architecture match code;
- [ ] no generated/private/third-party files staged;
- [ ] worktree and remote actions match user authorization.
