# Agent 03 — Bounded local compatibility check runner

You own `CHUNK-V1P-03-LOCAL-CHECK-RUNNER`.

## Start
1. Work only in `C:\HydraSeat\repo`.
2. Read `AGENTS.md`, `.agents/AGENTS.md`, only the `CHUNK-V1P-03-LOCAL-CHECK-RUNNER` section of `.agents/CHUNKS.md`, then directly relevant public APIs in `process_launcher`, `process_group`, `window_tracker`/window identity, `session_metrics`, and `input_metrics`. Do not load all implementation history.
3. Run `python3 tools/chunk_claim.py list` immediately before claim.
4. Claim exactly:

`python3 tools/chunk_claim.py claim CHUNK-V1P-03-LOCAL-CHECK-RUNNER --owner v1p-03-local-check-20260831 --paths include/hydra/local_compatibility_runner.hpp src/local_compatibility_runner.cpp tests/test_local_compatibility_runner.cpp --note "bounded user-initiated local compatibility process check"`

## Why this path exists
Normal production Play intentionally consumes trusted runtime requirement authority. That creates a first-use dead end today because the product has no release path to gather local evidence before that authority exists.

This module is **not a second normal launcher and not a permission bypass**. It is the Product V1 local compatibility-test backend: a user explicitly asks HydraSeat to test one exact local executable, HydraSeat runs a bounded owned-process observation, then returns truthful metrics. Normal Play authority remains unchanged.

## Implement
Design one narrow UI-independent runner around an already-resolved `process::ProcessLaunchSpec`.

Required behavior:
- validate that the request is one exact executable launch spec, no empty executable, invalid Seat, unbounded arguments/environment, or shell/script indirection;
- use existing `process::ProcessLauncher` / `SeatProcessGroup` ownership instead of raw `CreateProcess` ownership duplication where possible;
- launch exactly the supplied target and hold exact PID + creation-time/Job ownership;
- observe whether an authoritative owned top-level window appears using existing window/process ownership contracts; do not choose a same-name foreign process/window;
- bounded startup/window timeout supplied through a narrow limits struct with hard maximums;
- explicit cancellation that cleans only the exact owned group;
- bounded stop/cleanup with no same-name/global kill;
- verify no exact owned descendant remains before reporting returned-to-Windows success;
- produce a `metrics::SessionMetricsReport` through existing `buildSessionMetricsReport`, not a hand-written compatibility result;
- report process-start/window-ownership/clean-exit/cleanup facts truthfully;
- input isolation, controller, audio, display-placement or other facts that this runner did not actually measure must remain NotMeasured/MissingEvidence/false;
- runner-generated evidence origin is `metrics::EvidenceOrigin::ControlledProcess`.

Do not expose a parameter that lets the ordinary caller set origin=Physical. If future central integration supplies an explicit physical-observation authority, it must be a different typed input/API whose evidence is independently validated; this chunk should not fabricate it.

## Safe launch boundary
This local test may run a native executable target only. It must not:
- invoke `cmd.exe`, PowerShell, `ShellExecute` on arbitrary URI/script, or general shell strings;
- disable DRM/anti-cheat/launcher policy;
- inject Gate-C, HidHide, compatibility materialization or elevated mutation;
- provide arbitrary environment/scripting beyond the already-bounded `ProcessLaunchSpec` contract;
- launch a protected target when the request is marked/proven high-risk through an existing public signal. If the input type cannot express that safely, return an integration note rather than inventing a bypass.

A provider is responsible for resolving a supported game into the exact `ProcessLaunchSpec` before this runner. This runner is not a provider/catalog parser.

## Window observation
Reuse existing exact process-tree/HWND identity machinery if feasible without editing it. If the public window API cannot safely observe a newly launched group from this envelope, do not implement a weaker same-name/first-window scan. Return the smallest missing API as an integration note or design an injectable observer interface in your new module so tests can prove semantics while central integration later supplies the existing authoritative tracker.

## Tests
Use controlled test fixtures only. Do not modify the production test app; if an existing `window_test_app` executable can be referenced by the test target after central CMake integration, use it. Otherwise use fakes/interfaces inside the new test file.

Cover at least:
- exact owned process starts and report says ControlledProcess;
- authoritative owned window observed => windowOwnershipVerified true;
- no window before timeout => no fabricated window success;
- early process exit;
- explicit cancel;
- timeout cleanup leaves zero exact owned descendants;
- child process ownership cleanup;
- foreign same-name process/window never selected/killed;
- PID reuse/creation-time mismatch fails closed where injectable;
- input/controller/audio remain unmeasured unless a real input source explicitly supplied them;
- returned-to-Windows/clean-exit only after exact group cleanup;
- failure path never returns a Pass session verdict by default;
- hard timeout/argument/environment bounds enforced.

## Output contract
Return both a typed diagnostic and the completed `SessionMetricsReport` only on a semantically complete run. Partial/failed observation may return diagnostic details but must not be indistinguishable from a successful compatibility result.

Agent 01 will persist a valid report as local compatibility evidence after central integration. Agent 02 will bind evidence to reviewed runtime requirements. Do not write either persistence file here.

## Integration note
Do not edit CMake. Report the exact libraries/fixtures central integration must link. Do not edit launcher/Host/runtime authority or Agent 01/02 files.

## Finish
Run focused verification available in your envelope, then:

`python3 tools/chunk_claim.py done CHUNK-V1P-03-LOCAL-CHECK-RUNNER --owner v1p-03-local-check-20260831 --summary "<exact result>" --verification "<tests/builds>" --follow-up "<central UI/CMake/observer integration note or none>"`

If safely observing an exact HWND cannot be expressed with current public APIs inside the envelope, use `blocked` rather than weakening ownership. No commit/push/reset/clean/rebase and no edits outside claimed paths.
