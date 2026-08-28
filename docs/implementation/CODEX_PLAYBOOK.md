# Codex Implementation Playbook

## 1. Goal

This playbook turns the roadmap into reviewable implementation batches. Codex may implement one or more actionable packets in a task, following declared dependency order and leaving separate truthful evidence for every packet whose state changes.

## 2. Mandatory read order

Before editing code, read:

1. `.agents/AGENTS.md`;
2. [`../PRODUCT_V1.md`](../PRODUCT_V1.md);
3. [DECISIONS.md](DECISIONS.md);
4. [README.md](README.md);
5. [STATUS.md](STATUS.md);
6. the packet's phase document;
7. every source/header/test file explicitly named by the packet;
8. architecture or specialized design documents linked by the packet.

Do not scan unrelated third-party reference repositories during an implementation packet unless the packet explicitly classifies the task as research. Clean-room boundaries still apply.

## 3. Multi-packet execution rule

A task may start from one packet ID, the current default packet, or an ordered set selected from the READY/actionable frontier, for example:

```text
Start with P3-API-02, then continue through additional actionable packets in dependency order.
```

After a packet reaches its truthful automated completion state, Codex may proceed immediately to another actionable packet without waiting for a new user turn. For every selected packet:

- verify declared prerequisites before editing work that depends on them;
- keep packet-specific tests/evidence/status distinguishable even when changes share a branch or PR;
- do not skip missing prerequisites or mark packets complete without implementation/evidence;
- do not reorganize unrelated modules or update unrelated dependencies unless the selected batch requires it and the reason is documented;
- write/update a roadmap packet or decision when genuinely missing prerequisite work is discovered.

## 4. Standard task prompt

Preferred: generate a starting-packet prompt from the validated roadmap and use `--ready` to identify additional actionable work:

```text
python tools/show_implementation_packet.py --current --prompt
python tools/show_implementation_packet.py <PACKET-ID> --prompt
python tools/show_implementation_packet.py --ready
```

The tool refuses to produce a task when packet IDs, dependencies, links, states, or the current packet are inconsistent.

Manual template:

```text
Start with packet <PACKET-ID> as specified in
<PHASE-DOCUMENT>.
After it is complete, continue through additional actionable packets in declared dependency order when useful.

Read and obey:
- .agents/AGENTS.md
- docs/PRODUCT_V1.md
- docs/implementation/DECISIONS.md
- docs/implementation/CODEX_PLAYBOOK.md
- docs/implementation/STATUS.md
- the selected packet sections and all linked design documents

Scope rules:
- additional actionable packets may be implemented in the same task without a new user turn;
- do not skip undeclared prerequisites;
- do not weaken fail-closed policy;
- do not mark manual acceptance complete;
- do not claim a capability not proven by the required tests;
- no anti-cheat/protected-process bypass;
- no third-party source copying outside the clean-room policy;
- no push unless explicitly authorized.

Required workflow:
1. inspect the existing implementation and tests for the selected packet(s);
2. verify each packet's prerequisites and invariants before dependent edits;
3. implement each selected packet in dependency order, continuing directly to the next actionable packet;
4. add normal, boundary, malformed, failure, teardown, and rollback tests as applicable to each packet;
5. run focused checks for every changed packet plus the applicable regression suite;
6. fix every warning/error caused by the batch;
7. update STATUS.md and relevant documentation with truthful per-packet evidence;
8. run git diff --check;
9. summarize changed files, tests, packet states, limitations, and unperformed manual gates.
```

## 5. Planning before editing

Codex must produce a short internal implementation map before modifying files:

```text
Packet:
Prerequisites verified:
Existing modules reused:
New files/types:
Thread/ownership model:
Persistent or OS state touched:
Rollback path:
Tests to add:
Manual acceptance left pending:
```

If the packet cannot be implemented without an unspecified architectural choice, stop editing and add a proposed decision/packet update rather than making the choice silently.

## 6. File and API discipline

### Public headers

Create a public header only when another component needs the contract. Public interfaces require:

- stable naming;
- ownership/lifetime documentation;
- error semantics;
- thread-safety statement;
- versioning when crossing process/language/persistence boundaries;
- unit or ABI tests.

### Internal implementation

Prefer narrow translation units and internal namespaces. Avoid “manager” classes that collect unrelated responsibilities. Extract an interface only when the packet needs multiple implementations, deterministic fakes, or a process boundary.

### Win32 and COM

- wrap `HANDLE`, event, process, thread, module, hook, service, and registry ownership in RAII;
- pair every COM initialization with the correct thread-local uninitialization;
- do not call blocking or reentrant work from a WinEvent/Raw Input/audio callback;
- capture `GetLastError()` immediately;
- use fixed-width types in transported/persisted contracts;
- treat x86/x64 pointer width and structure packing explicitly;
- restore any process/window/display/audio/device state changed by a test.

### Threads and queues

Every new queue or worker documents:

- producer and consumer threads;
- maximum capacity;
- overflow behavior;
- ordering guarantee;
- shutdown/drain/discard policy;
- timeout;
- error propagation;
- whether the producer can block.

No unbounded queue is accepted.

## 7. Test construction rules

### Unit tests

Test:

- default state;
- valid transition sequence;
- minimum/maximum values;
- invalid enums/booleans/sizes/versions;
- duplicate/stale/out-of-order events;
- partial failure;
- idempotent stop/reset/rollback;
- deterministic ordering;
- serialization/migration round trip;
- no state bleed between Seats/contexts.

### Windows process tests

For IPC/adapters/window/process work:

- launch real child processes;
- validate PID/architecture/session identity;
- enforce a timeout;
- inspect child exit code;
- confirm no child remains;
- run the failure path, not only clean shutdown;
- avoid tests that pass only because the parent exits and Windows cleans up implicitly.

### Hardware tests

Codex may create the harness and checklist, but must leave the status `CODE_COMPLETE` until a human runs the declared physical procedure and records evidence.

### Game tests

A game result must not be encoded as a generic engine unit test. Record it as a compatibility matrix entry with exact versions/topology/backend set.

## 8. Verification tiers

A normal packet runs all applicable tiers:

### Tier A — local portable checks

- strict C/C++ compiler warnings where supported;
- pure unit tests;
- schema/parser/ABI tests;
- Markdown/link/control-character checks for documentation packets;
- `git diff --check`.

### Tier B — Windows x64 CI

- configure/build Release;
- CTest with output on failure;
- actual process/DLL/window/API integration tests;
- artifact existence checks when launchers expect sibling binaries.

### Tier C — Windows x86 CI

Required before declaring a 32-bit shim/adapter/profile supported.

### Tier D — physical/manual acceptance

Required by the packet's checklist.

### Tier E — soak/recovery

Required for host/watchdog/driver/update/game support packets.

### Phase-close verification — whole-phase recalculation

This task runs once when a numbered roadmap phase appears ready to close. It is a verification pass distinct from normal single- or multi-packet implementation batches. Its purpose is to independently recalculate whether the phase's combined code actually satisfies the phase exit gate.

Required workflow:

1. enumerate every packet and commit that materially contributed to the phase;
2. map phase requirements/decisions to the final source and test surfaces;
3. review the complete relevant code, not only the last packet diff;
4. rerun the broad applicable regression set and required Windows x64/x86, process, ABI/protocol/schema, recovery/soak, performance, and manual gates;
5. inspect cross-packet interactions, failure/rollback/teardown paths, stale/duplicate handling, no-cross-Seat invariants, resource leaks, and unsupported/fail-closed behavior;
6. search for accumulated TODOs, temporary branches, warnings, dead code, stale documentation, and capability/support claims not backed by evidence;
7. write a phase-close evidence record to `STATUS.md` with reviewed SHAs, test and CI identifiers, manual evidence, findings, and the final result.

If a defect is found, stop the closeout as failed/incomplete. Fix it in the owning packet or a separate focused repair task, validate the repair, and rerun the whole phase-close verification. Do not bury feature implementation inside the audit itself.

## 9. Documentation update rules

Update only the documents affected by behavior:

- [STATUS.md](STATUS.md): packet state, evidence, next packet;
- phase document: packet checkbox/details only after true completion;
- `docs/ROADMAP.md`: phase-level progress, not every internal commit;
- `docs/ARCHITECTURE.md`: component or contract changes;
- README: user-visible executable/behavior/status only;
- compatibility/hardware matrix: manual evidence;
- clean-room/third-party notices: source/dependency changes.

Never replace a precise limitation with marketing language.

## 10. Status evidence format

A packet entry in `STATUS.md` should contain:

```text
Packet: P4-WIN-02
State: CODE_COMPLETE
Branch/commit: <branch or SHA>
Automated evidence:
- test name / CI run
Manual evidence:
- pending: exact procedure
Known limits:
- explicit unsupported cases
Next dependency:
- packet ID
```

Do not insert secrets, local tokens, or private file paths into evidence.

## 11. Commit and branch conventions

Recommended branches:

```text
feat/p3-api-polling-shim
feat/p4-display-topology
fix/p4-window-hotplug-recovery
docs/master-implementation-roadmap
test/p5-zero-bleed-metrics
```

Recommended commit prefixes:

```text
feat: implement P3-API-02 polling shim
fix: make P4-DIS-03 rollback idempotent
test: add P5-MET-01 bleed detector
docs: define Phase 6 launcher packets
refactor: extract P4-RUN-01 host protocol
```

The packet ID appears in the PR title/body even when the commit summary remains natural.

## 12. Stop conditions

Stop implementation and report the blocker when:

- a required decision is absent or contradictory;
- the only apparent path violates clean-room/license policy;
- a protected/anti-cheat process would require bypass behavior;
- a manual physical result is necessary to choose the architecture;
- required hardware/driver/toolchain is unavailable and no deterministic fake can prove the invariant;
- the patch would need silent persistent system changes;
- rollback cannot be defined;
- test evidence contradicts the packet's premise.

A stopped packet becomes `BLOCKED`, `DEFERRED`, or `REJECTED` with an explanation. Do not force a green checkbox.

## 13. Review checklist for Codex output

Before considering a selected packet or multi-packet batch complete, verify:

- [ ] changes stay within the declared packet batch plus documented prerequisites;
- [ ] decisions and naming use Seat-first architecture;
- [ ] unsupported behavior fails closed;
- [ ] no false isolation/support claim;
- [ ] public contracts are versioned where required;
- [ ] all resources and threads have deterministic teardown;
- [ ] callback paths remain bounded/nonblocking;
- [ ] success and failure tests exist;
- [ ] manual gates remain pending unless evidence exists;
- [ ] status and architecture docs match code;
- [ ] Windows CI passed;
- [ ] worktree is clean after commit;
- [ ] no push occurred without authorization.

## 14. Packet-authoring template

When a new packet is needed, add this structure to the phase document:

```markdown
### P<phase>-<area>-<number> — Title

**State:** BLOCKED | READY | IN_PROGRESS | CODE_COMPLETE | VALIDATED

**Goal**
- one observable result

**Depends on**
- packet IDs and decisions

**Create/modify**
- exact files/targets

**Public contracts**
- types, schemas, IPC, ABI, CLI

**Implementation skeleton**
1. ordered internal steps

**Invariants**
- properties that must always hold

**Automated tests**
- exact test targets/cases

**Manual acceptance**
- exact procedure or `none`

**Rollback/failure behavior**
- how previous state is restored

**Non-goals**
- explicitly excluded or unsupported behavior

**Done when**
- objective completion conditions

**Suggested commit**
- `feat: ...`
```

## 15. Documentation-only packet verification

Even documentation packets run checks:

- all relative links resolve;
- Markdown fences are balanced;
- no NUL/backspace/replacement characters;
- packet IDs are unique;
- dependencies reference real packet IDs;
- status values are valid;
- the critical path has no undeclared cycle;
- `git diff --check` passes.

A future packet should add a machine-readable roadmap linter rather than relying permanently on manual inspection.
