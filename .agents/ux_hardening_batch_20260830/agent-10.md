# Agent 10 — Worktree/slop hygiene gate

You own `CHUNK-UXH-10-WORKTREE-HYGIENE`.

## Start
Read `AGENTS.md`, `.agents/AGENTS.md`, your chunk section, then only the claimed premerge/tool files and current Git status metadata. Do not read secret contents or unrelated user directories.

Run `python3 tools/chunk_claim.py list`, then claim exactly:

`python3 tools/chunk_claim.py claim CHUNK-UXH-10-WORKTREE-HYGIENE --owner uxh-10-worktree-hygiene-20260830 --paths tools/run_premerge_gate.py tools/testdata/premerge_gate tools/validate_worktree_hygiene.py tools/testdata/worktree_hygiene --note "non-destructive AI-slop worktree hygiene gate"`

## Problem to solve
Large automated coding waves can leave root `.obj` files, generated IDE/build output, scratch/evidence artifacts, personal absolute paths and throwaway test outputs even when tests are green. We need a non-destructive integration gate, not another cleanup script.

## Outcome
Create `tools/validate_worktree_hygiene.py` with deterministic fixture coverage and integrate it into the existing premerge runner. Inspect only bounded repository/Git metadata and candidate text where needed. Flag suspicious root object/binary/archive output, generated project files outside allowed ignored build roots, accidental `.ai-bridge` publication candidates, personal absolute paths in candidate changes, duplicate throwaway outputs and unexpected release payloads. Treat intentional `.agents` coordination state as internal/control-tower material, not a public artifact. Never delete or rewrite anything.

The validator must be usable on a dirty developer worktree and distinguish BLOCKER vs informational internal/ignored state so it does not punish legitimate concurrent coordination.

## Acceptance
- deterministic self-test fixtures for clean, generated-junk, personal-path, `.ai-bridge`, duplicate-output and intentional `.agents` cases;
- non-zero only for configured hygiene blockers;
- integrated premerge result is explicit and non-destructive;
- no secret scanning outside the repository and no file deletion.

Run the new validator self-test plus `run_premerge_gate.py` fixture/self-test path. Finish DONE/BLOCKED with exact verification. No Git/remote actions.
