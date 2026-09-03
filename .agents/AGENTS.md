# HydraSeat Worker / Concurrency Rules

Use this file only when a task is explicitly running as a worker/chunk or needs specialized worker coordination. Ordinary single-agent Codex tasks should follow the root `AGENTS.md` and avoid loading this file.

## Worker startup

1. Read the user's current worker assignment and root `AGENTS.md`.
2. Run `python tools/chunk_claim.py list` instead of reading `.agents/CHUNKS.md` wholesale.
3. Locate the exact assigned/READY chunk by ID. Read only that chunk's definition if additional detail is needed.
4. Atomically claim exactly that chunk and concrete touched paths before editing.
5. Inspect only the source/tests directly relevant to the claimed outcome.

If a claim fails, another worker owns the path. Do not edit it.

## Ownership

- Stay inside claimed paths.
- CMake, root/product docs, broad status/roadmap files, and other shared integration surfaces remain control-tower owned unless the chunk explicitly assigns them.
- Report required out-of-scope/shared changes as an integration note instead of editing them.
- Runtime claim files under `.ai-bridge/` are coordination state, not product evidence.
- Preserve unrelated dirty-worktree changes. Never reset/clean/checkout them away.

## Product constraints every worker must preserve

- HydraSeat v1 supports at most two active Seats.
- One interactive Windows session; no VM/RDP/general-desktop expansion.
- Seat, Player, Game, TwoPlayerSetup, and runtime binding are distinct concepts.
- One Seat may stop/change games while the other continues.
- Normal UX is game-first; low-level details belong outside the ordinary path.
- Offline-first and least-privilege.
- No anti-cheat, DRM, protected-process, security-product, credential, launcher/account, or single-instance bypass.
- Ambiguous/unsupported isolation fails closed.
- Risky persistent/system mutations require recovery/rollback.
- Manual physical/game/install/reboot/signing gates cannot be synthesized from code or tests.
- Reference repositories under `C:\HydraSeat\references` are read-only research inputs and never build inputs. Follow `docs/CLEAN_ROOM_POLICY.md` when third-party material is relevant.

## Progressive disclosure for workers

Do not preload the planning corpus.

Read deeper material only when the claimed chunk needs it:

- product semantics: search `docs/PRODUCT_V1.md` for the affected concept;
- architecture authority: search `docs/implementation/DECISIONS.md` for the subsystem;
- current packet/evidence: search `docs/implementation/STATUS.md` for the exact packet/component;
- roadmap packet details: read only the exact section in the relevant `PHASE*.md`;
- localization: targeted section of `docs/LOCALIZATION.md`;
- third-party/reference use: targeted section of `docs/CLEAN_ROOM_POLICY.md`.

Do not read all phase documents, all status history, or all chunk history for a narrow task.

## Implementation discipline

- Reuse existing responsibility/interfaces; do not create broad `manager`/wrapper layers without a real ownership need.
- For Win32/system resources use RAII and capture system errors immediately.
- Persist user state transactionally; validate before write and preserve the prior usable state on failure.
- Cross-process protocols/ABIs stay versioned, bounded, fixed-width, and pointer-free.
- Stable physical identity, not enumeration order/friendly name, owns hardware authority.
- Latency-sensitive callbacks perform bounded work only; no disk/network/UI waits or unbounded queues.
- Do not delete compatibility/security/authority code just because a UI path stopped exposing it.

If a claimed change touches one of these areas and the precise rule matters, search the relevant decision/architecture doc instead of loading every engineering rule up front.

## Verification budget

Workers run the smallest tests that prove their chunk and report exact commands/results.

Default worker verification:

1. directly affected compile target(s), when practical;
2. focused unit/component/process tests for changed behavior;
3. a targeted validator if the chunk owns a contract with one;
4. `git diff --check` when available.

Workers should **not** run the full dual-architecture release matrix, soak loops, or pre-merge suite unless their chunk explicitly owns that verification. Broad x64/x86 regression and final integration belong to the control tower.

On failure:

- do not weaken assertions;
- retry the same command at most once;
- after a second failure, diagnose and either repair within claim scope or report BLOCKED;
- do not burn time/usage by cycling shell variants or broad filesystem scans.

## Completion

Before releasing the chunk:

- review only the claimed diff;
- confirm no out-of-scope paths were edited;
- leave real manual gates PENDING;
- mark the claim `DONE` or `BLOCKED` with focused test evidence and a concise integration note;
- do not push/create/merge PRs unless the user explicitly authorized remote mutation.

## Control-tower responsibilities

The control tower, not ordinary workers, owns:

- shared-file reconciliation;
- cross-chunk integration;
- broad x64/x86 CTest matrices;
- soak/performance/release gates;
- roadmap/status-wide evidence reconciliation;
- final worktree hygiene and remote actions.

This split is intentional: deterministic broad validation should not consume every worker's model context and usage budget.
