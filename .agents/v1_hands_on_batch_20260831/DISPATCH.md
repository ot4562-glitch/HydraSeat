# HydraSeat V1 Hands-On / Installer Batch — 2026-08-31

## Dispatch
Send Agent 1 through Agent 5 the same form, replacing XX with that agent number:

`@CodexPro Local C:\HydraSeat\repo\.agents\v1_hands_on_batch_20260831\agent-XX.md 를 읽고 그대로 수행해. 시작 전 python3 tools/chunk_claim.py list 후 문서의 정확한 CHUNK-V1H-*를 claim하고 claimed path 밖은 수정하지 마. 기존 dirty worktree를 reset/clean/restore하지 말고, 끝나면 DONE 또는 BLOCKED와 정확한 테스트/integration note를 남겨.`

Agents 6–10 remain idle unless the control tower explicitly assigns a follow-up.

## Parallel ownership
- Agent 01: new launcher user-state persistence module only. Does not edit launcher UI.
- Agent 02: Windows installer/bootstrapper and its exact release/signing contract only.
- Agent 03: Games launcher UX and layout/model only.
- Agent 04: Hardware Setup + hardware/input identification only.
- Agent 05: new V1 mock/readiness QA artifacts only; no product source.

No worker edits `CMakeLists.txt`, README files, shared status/roadmap docs or performs Git/remote actions. Control tower integrates new targets, cross-chunk wiring, localization IDs, broad x64/x86 regression, premerge, installer package assembly and final Computer Use acceptance.

## Dependency note
Agent 01 and Agent 03 may run concurrently. Agent 03 must not duplicate Agent 01 storage code. If Agent 01 is not yet DONE, Agent 03 leaves one explicit wiring note; control tower integrates after both finish.

## Evidence policy
Automated/mock != Computer Use != Physical != RealGame != CleanMachine != Signing. No agent may promote one class to another.
