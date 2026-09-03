# Agent 06 — Wave 2 Process/Window Soak

You completed the first-wave process/window authority audit. Reuse your worker slot for flake/soak detection without touching production files.

Run:

```text
python3 tools/chunk_claim.py list
python3 tools/chunk_claim.py claim CHUNK-N10B-PROCESS-SOAK --owner rookie-06-soak-20260830 --paths tools/run_process_window_soak.py tools/testdata/process_window_soak --note "bounded repeated process-window lifecycle soak"
```

Write only the claimed new tool/fixtures. Build a bounded developer-machine soak runner around the existing process_group/window_tracker/window_policy controlled test executables. Repeat them with explicit iteration/timeout limits and summarize pass/fail iteration, duration and orphan/timeout signal from the underlying tests. The runner must stop cleanly and never kill unrelated processes.

Do not edit process/window production code or tests, CMake/shared docs, Git state, or claim real-game/physical evidence. Finish DONE/BLOCKED with repeated-run evidence and any flaky test identity.