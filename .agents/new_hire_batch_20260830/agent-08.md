# Agent 08 — Audio Routing / Native Session Lifetime

You are the resumed Agent 08. The control tower reconciled your earlier collision: no currently active claim owns the audio files, and the current `tests/test_audio_routing.cpp` SHA `4f490cbbacc3c86d8f641bf9c561d44ec019f18a23c2510112a4f20312f7067c` is now the accepted baseline. Do not restore the prior SHA. Treat current repository contents as baseline; never reset/revert unrelated changes.

Read mandatory docs plus `audio_routing` and native audio-session code/tests, with process identity contracts read-only. Then run:

```text
python3 tools/chunk_claim.py list
python3 tools/chunk_claim.py claim CHUNK-N10-AUDIO --owner rookie-08-audio-20260830 --paths include/hydra/audio_routing.hpp src/audio_routing.cpp src/audio_session_native.cpp tests/test_audio_routing.cpp --note "exact-process audio lifetime rollback and x64/x86 audit"
```

Write only claimed files. First finish the already-present `testRollbackRejectsLateOwnedSessionWithoutCapturedEndpoint`: wire it into `main()` so it actually executes, reproduce the failure, then harden `verifyBeforeEndpoints` so rollback cannot claim success when a post-mutation exact owned session exists without captured pre-apply endpoint evidence. Stress exact PID+creation identity, parent/descendant ownership assumptions, late Core Audio session appearance, apply/verify/rollback/recovery sequencing, object/COM lifetime, stale PID reuse, cross-Seat attempts, teardown and unsupported native movement. Preserve explicit observe-only/fail-closed behavior when routing cannot be verified; never convert a native query/apply failure into synthetic success. Eliminate any remaining field-order-sensitive aggregate fixture or lifetime UB.

Do not edit process-group authority, endpoint inventory, production activation ordering, CMake/shared docs, or claim audible/physical two-output validation from controlled tests.

Run focused `AudioRoutingTests` on MSVC Release x64 and Win32/x86 when available, including repeated execution to expose lifetime crashes. Finish DONE/BLOCKED with exact crash/rollback evidence.