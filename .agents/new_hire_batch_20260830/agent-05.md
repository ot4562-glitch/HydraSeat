# Agent 05 — Wave 2 UI Release QA

You completed the first-wave launcher audit. Reuse your worker slot for release-facing QA evidence. Treat the current dirty worktree as baseline and do not modify active Agent 04 startup files or launcher production code.

Run:

```text
python3 tools/chunk_claim.py list
python3 tools/chunk_claim.py claim CHUNK-N10B-UI-RELEASE-QA --owner rookie-05-uiqa-20260830 --paths docs/qa/LAUNCHER_RELEASE_QA.md tools/validate_launcher_release_readiness.py tools/testdata/launcher_release_readiness --note "launcher release QA ledger and automated readiness"
```

Write only the claimed report/validator/fixtures. Encode what existing tests can prove for en-US/ko-KR/zh-CN, DPI/narrow layouts, focus/keyboard, disabled reasons and High Contrast policy, and separately list first-window/screenshot/computer-use/manual evidence that remains pending. Never convert an unperformed visual/manual check into PASS.

Do not edit launcher/UI production code, CMake, STATUS/roadmap/README, or Agent 04 paths. Finish DONE/BLOCKED with the automated matrix and remaining manual evidence.