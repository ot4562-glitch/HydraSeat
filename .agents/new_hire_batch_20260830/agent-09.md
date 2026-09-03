# Agent 09 — Host Protocol / Transport / Runtime

You are a brand-new HydraSeat worker. Ignore previous worker identities. Current dirty worktree is the baseline; never reset/revert unrelated changes.

Read mandatory docs plus Host protocol/transport/runtime contracts and their focused tests. Treat `src/host_main.cpp`, production launch runtime and activation bridges as read-only external composition. Then run:

```text
python3 tools/chunk_claim.py list
python3 tools/chunk_claim.py claim CHUNK-N10-HOST-RUNTIME --owner rookie-09-host-20260830 --paths include/hydra/host_protocol.hpp src/host_protocol.cpp include/hydra/host_transport.hpp src/host_transport.cpp include/hydra/runtime_host.hpp src/runtime_host.cpp tests/test_host_protocol.cpp tests/test_host_process.cpp tests/test_runtime_host.cpp tests/test_production_host_protocol.cpp --note "host replay bounds reconnect and two-seat isolation audit"
```

Write only claimed files. Adversarially test malformed/version/future/bounds handling, correlation reuse/replay, stale/duplicate/out-of-order commands, reconnect/resnapshot, exact Host session and Seat generations, provider-plan install/start transaction semantics, one-Seat failure while the other remains healthy, third-Seat rejection, client death, Host death and teardown. Client-supplied snapshots or plan claims must never become runtime authority; canonical Host state remains authoritative.

Do not edit `src/host_main.cpp`, production launch runtime, activation bridges, UI, CMake/shared docs, or weaken protocol bounds/version checks for compatibility. Preserve exact two-Seat v1 behavior and independent Seat lifecycle.

Run focused Host protocol/process/runtime/production-protocol tests, x64/x86 when available. Finish DONE/BLOCKED with concrete failing invariant, fix, and verification.