# Agent 01 — Wave 2 Pre-Merge Gate

You completed the first-wave build graph task. Reuse your worker slot for GitHub-mainline hardening. Treat the current dirty worktree as baseline and never reset/revert unrelated changes.

Run:

```text
python3 tools/chunk_claim.py list
python3 tools/chunk_claim.py claim CHUNK-N10B-PREMERGE-GATE --owner rookie-01-premerge-20260830 --paths tools/run_premerge_gate.py tools/testdata/premerge_gate --note "deterministic pre-merge gate without product edits"
```

Write only the claimed new tool/fixture paths. Build a deterministic, non-destructive one-command pre-merge orchestrator around existing repository validators. It must report automated PASS/FAIL separately from x64/x86 build/test availability and from Physical/Real-game/Clean-machine/Signing manual gates. Any automated failure returns non-zero; missing manual evidence remains PENDING, never PASS.

Do not edit CMake, product C++/headers/tests, shared docs, Git state, or remote state. Reuse existing validators rather than duplicating their rules. Add a bounded self-test for orchestration behavior. Finish DONE/BLOCKED with exact commands and output classes.