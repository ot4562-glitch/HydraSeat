# HydraSeat New-Hire 10-Agent Dispatch — 2026-08-30

Old worker identities are retired. The current worktree is the baseline; do not reset, clean, revert, checkout, or overwrite unrelated existing changes.

Give each new agent only these three lines:

1. Open `C:\HydraSeat\repo\.agents\new_hire_batch_20260830\agent-XX.md` for your assigned number and follow it exactly.
2. Before editing, run `python3 tools/chunk_claim.py list`, then execute the exact claim command in your prompt; never edit outside the claimed paths.
3. Finish with `chunk_claim.py done` or `blocked` including focused verification and cross-chunk integration notes; do not edit CMake/shared docs unless your prompt explicitly delegates them.

Current assignments:

- Agent 01 -> `CHUNK-N10B-PREMERGE-GATE` (Wave 2; first-wave build graph DONE)
- Agent 02 -> `CHUNK-N10-RECOVERY-ATTACHMENT` (active)
- Agent 03 -> `CHUNK-N10-COMPAT-WIRING` (active)
- Agent 04 -> `CHUNK-N10-STARTUP-WINDOW` (active)
- Agent 05 -> `CHUNK-N10B-UI-RELEASE-QA` (Wave 2; first-wave launcher UX DONE)
- Agent 06 -> `CHUNK-N10B-PROCESS-SOAK` (Wave 2; first-wave process/window DONE)
- Agent 07 -> `CHUNK-N10B-ABI-CONTRACT` (Wave 2; first-wave controller/input DONE)
- Agent 08 -> `CHUNK-N10-AUDIO` (resume original audio audit; not yet DONE)
- Agent 09 -> `CHUNK-N10-HOST-RUNTIME` (BLOCKED pending Agent 03/build integration; preserve its implemented changes)
- Agent 10 -> `CHUNK-N10B-PUBLICATION-HYGIENE` (Wave 2; first-wave installer/trust DONE)

Central-manager rule: `CMakeLists.txt`/`cmake/*` returned to control-tower-only ownership after Agent 01 completed the first-wave build-graph task. No worker owns CMake, `STATUS.md`, roadmap/decision docs, README files, `AGENTS.md`, `.agents/AGENTS.md`, or `.agents/CHUNKS.md` during Wave 2 unless the control tower explicitly re-delegates one exact change. Workers report integration notes instead.

Legacy-process quarantine: a pre-reset worker process is still alive and may keep a non-`N10` claim. New hires must never edit paths protected by any active non-`N10` claim shown by `chunk_claim.py list`. The ten assignments in this directory were chosen to be disjoint from the currently observed legacy claim; if the live claim later expands into a new hire's exact path, that new hire must stop before editing and report the collision to the control tower.
