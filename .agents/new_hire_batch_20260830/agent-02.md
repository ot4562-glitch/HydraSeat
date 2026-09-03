# Agent 02 — Recovery Attachment / Watchdog / Reset

You are a brand-new HydraSeat worker. Old worker identities are irrelevant; the current dirty worktree is the baseline. Never reset/revert unrelated changes.

Read the mandatory agent/product/decision/status docs plus `recovery_process_attachment`, watchdog protocol, crash journal, reset action contracts and their focused process tests. Then run:

```text
python3 tools/chunk_claim.py list
python3 tools/chunk_claim.py claim CHUNK-N10-RECOVERY-ATTACHMENT --owner rookie-02-recovery-20260830 --paths include/hydra/recovery_process_attachment.hpp include/hydra/watchdog_protocol.hpp src/watchdog_protocol.cpp include/hydra/crash_journal.hpp src/crash_journal.cpp include/hydra/reset_actions.hpp src/reset_actions.cpp tests/test_watchdog_protocol.cpp tests/test_crash_journal.cpp tests/test_reset_actions.cpp tests/test_watchdog_process.cpp tests/test_reset_process.cpp --note "exact-process watchdog crash-journal reset attachment audit"
```

Write only the claimed files. Adversarially verify exact Seat, Host session, session generation, Seat-game generation, PID+creation identity and recovery epoch across registration, persistence, disarm, restart and reset execution. Reject stale PID reuse, wrong Seat/session/generation, conflicting duplicate leases, downgrade/legacy ambiguity and arbitrary commands. Exact duplicate registration/disarm may remain idempotent only when the full authority tuple matches. Partial cleanup must remain recoverable and verifiable.

Do not edit production activation bridges, HidHide/Gate-C input code, production launch runtime, Host transport, CMake/shared docs, or claim physical/reboot acceptance. If a bridge consumer needs another API, return a narrow integration note instead of editing outside your claim.

Run focused watchdog/crash/reset unit and process tests on x64/x86 where available. Finish with `chunk_claim.py done` or `blocked` and exact stale/recovery evidence.