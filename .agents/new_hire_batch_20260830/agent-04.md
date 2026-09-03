# Agent 04 — Startup / First Window

You are a brand-new HydraSeat worker. Ignore all old worker identities. The current dirty worktree is baseline; never reset/revert unrelated changes.

Read mandatory docs plus `src/main.cpp`, `include/hydra/gui_win32.hpp`, `src/gui_win32.cpp`, current startup policy/composition APIs read-only, and `tests/test_hydra.cpp`. Then run:

```text
python3 tools/chunk_claim.py list
python3 tools/chunk_claim.py claim CHUNK-N10-STARTUP-WINDOW --owner rookie-04-startup-20260830 --paths src/main.cpp include/hydra/gui_win32.hpp src/gui_win32.cpp tests/test_hydra.cpp --note "real executable first-window responsiveness and explicit startup failure"
```

Own only those four files. Audit the real startup path for blocking work before the first management HWND. While Agent 01 repairs CMake, inspect the source and strengthen a focused startup/process regression within your envelope; once the tracked Release executable is buildable, reproduce against that exact build before making behavior changes. The product must either show its first management window promptly or fail/exit with a bounded actionable diagnostic, never remain alive invisibly because provider/catalog/evidence/Host initialization blocks the UI thread.

Do not edit launcher presentation, Host/runtime/protocol, resolver internals, production launch runtime, CMake/shared docs, or generated projects. Do not use arbitrary sleeps or a watchdog kill as the fix. If the prior hang no longer reproduces after build integration, do not invent a source fix; return evidence and any remaining test gap.

Run the focused startup test plus the real Release executable proof when available. Finish with DONE/BLOCKED and exact reproduction steps.