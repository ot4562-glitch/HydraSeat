# HydraSeat Codex Entry Point

This file is the default Codex context. Keep startup context small and load deeper docs only when the current task actually needs them.

## Product boundary

HydraSeat v1 is a Windows local, game-first, two-Seat multiseat product. Preserve these constraints unless the user explicitly changes product scope:

- at most two active Seats;
- one interactive Windows session; no VM/RDP/general desktop expansion;
- Seat hardware, Player identity, Game identity, Two-player setup, and runtime bindings are separate concepts;
- offline-first and least-privilege;
- no anti-cheat, DRM, protected-process, credential, launcher/account, single-instance, or security-product bypass;
- unsupported or ambiguous isolation behavior fails closed;
- recovery/rollback is part of every risky system mutation;
- manual physical/game/install/reboot/signing evidence stays PENDING unless a human actually performed it;
- reference repositories under `C:\HydraSeat\references` are read-only research inputs and never build inputs or source to copy blindly.

## Context budget: progressive disclosure only

Do **not** read the repository's planning/documentation corpus wholesale at task start.

Start with:

1. the user's current request;
2. this file;
3. the directly relevant source/test files found by targeted filename/symbol search.

Load other documents only when a concrete decision depends on them:

- `docs/PRODUCT_V1.md`: search for the relevant product concept, then read only the matching section.
- `docs/implementation/DECISIONS.md`: search for the affected subsystem/decision, then read only nearby lines.
- `docs/implementation/STATUS.md`: read only when roadmap/status/evidence is part of the task; search for the packet or component first.
- `docs/implementation/README.md` and `CODEX_PLAYBOOK.md`: read only for roadmap packet execution or release/integration workflows.
- relevant `docs/implementation/PHASE*.md`: read only the selected packet section, never every phase document.
- `docs/LOCALIZATION.md`: read the relevant section before adding or changing end-user text.
- `docs/CLEAN_ROOM_POLICY.md`: read the relevant section before using third-party source/reference material.
- `.agents/AGENTS.md`: worker/concurrency rules only; do not load for ordinary single-agent implementation.
- `.agents/CHUNKS.md`: never read wholesale for ordinary work. When concurrent workers are explicitly active, use `python tools/chunk_claim.py list` and targeted lookup for the exact chunk.

Do not re-read a file after editing unless exact final content is needed. Prefer bounded symbol/text searches and small line ranges over whole-file reads.

## Efficient implementation loop

1. Search narrowly for the owning symbols/files and related tests.
2. Inspect the minimum code needed to understand ownership and invariants.
3. Make the smallest coherent change; preserve unrelated dirty-worktree edits.
4. Run the smallest focused build/test set that can falsify the change.
5. Retry the same failing command at most once. On a second failure, diagnose the root cause instead of repeating variants blindly.
6. Do not perform broad drive/repository scans when a scoped `rg`/filename/symbol search can answer the question.
7. Keep command output bounded; do not dump full build/test logs unless a failure requires the relevant excerpt.
8. Review the changed files once at the end; avoid repeated full-diff reviews after every edit.

## Validation ownership and usage control

Codex should normally perform **focused implementation verification**, not spend an agent loop orchestrating the entire release matrix.

Default Codex verification:

- compile/build the directly affected targets when practical;
- run directly related tests;
- run a narrow static/contract validator if the touched area has one;
- run `git diff --check` when available.

Do **not** run both full x64 and x86 builds, both complete CTest suites, soak loops, or the complete pre-merge/release gate inside an ordinary Codex turn unless the user explicitly requests release/integration validation or the task cannot be judged safely without it. The control tower/CI can run deterministic broad verification after Codex returns the implementation.

If broad verification is explicitly required, run focused checks first and stop broad expansion on the first real failure rather than burning the remaining matrix.

## Concurrency

Only enter chunk-claim workflow when the user explicitly says multiple workers/agents may be active or gives an agent/chunk assignment. Then:

1. run `python tools/chunk_claim.py list`;
2. claim exactly the assigned/READY chunk and concrete paths;
3. stay within the claim;
4. report shared-file changes as integration notes unless the chunk explicitly owns them;
5. mark DONE or BLOCKED truthfully.

Single-agent tasks do not need to read the chunk board or claim a chunk.

## Change safety

- Never reset/clean/checkout away unrelated user work.
- Do not push, create/merge PRs, or mutate remotes unless the user explicitly authorizes it.
- Do not weaken fail-closed behavior or tests to make a gate green.
- Do not delete runtime/security/authority compatibility code merely because the current UI does not expose it.
- Keep protocols/schemas/ABIs bounded and versioned; do not serialize pointers/C++ objects across process boundaries.
- Use stable physical identities rather than enumeration order/friendly name for hardware authority.
- Preserve per-Seat independent lifecycle and verified rollback to ordinary Windows behavior.

## When to use deeper policy

Use `.agents/AGENTS.md` only for worker orchestration and specialized engineering rules not covered here. Use the detailed product/design documents as targeted references, not mandatory startup reading.
