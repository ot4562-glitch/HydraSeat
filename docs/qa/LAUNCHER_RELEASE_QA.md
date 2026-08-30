# Launcher Release QA

Date: 2026-08-30
Scope: native Win32 launcher release-facing QA ledger for `CHUNK-N10B-UI-RELEASE-QA`.

This ledger separates repository-automated evidence from evidence that requires the real Windows product surface. Automated regression coverage is not screenshot evidence, and a synthetic/layout assertion is not a real High Contrast, mixed-DPI, first-window, or computer-use acceptance result.

## Current automated matrix

The current local Windows build was refreshed for the three focused launcher targets. Each CTest was then executed separately so a zero-test filter cannot be mistaken for success.

| Area | CTest | Result | What it proves | What it does not prove |
| --- | --- | --- | --- | --- |
| Localization | `UiLocalizationTests` | PASS (1/1) | Complete `en-US` / `ko-KR` / `zh-CN` catalog coverage/fallback, critical-label width floors across launcher DPI scaling, and one owner-drawn status-marker text contract | Real glyph rasterization, screenshots, font fallback quality on a clean Windows machine |
| Layout / accessibility | `UiAccessibilityTests` | PASS (1/1) | Three-locale geometry, 96/120/144/192 DPI coverage, 980x720 and narrow reflow, Game Library-first focus order, long blocking-reason visibility, target sizing, and High Contrast system-color policy selection | Real keyboard traversal behavior, real Windows High Contrast appearance, actual screen-reader behavior, real mixed-DPI monitor movement |
| Readiness / disabled Play | `LauncherUiModelTests` | PASS (1/1) | Missing trusted requirement evidence blocks Play and activation requires a validated current plan | Real provider/game launch, first-window responsiveness, visual disabled-state quality |

Final control-tower execution used the configured MSVC Release trees `build-agent6-msvc` (x64) and `build-agent6-msvc-x86` (Win32/x86); all three focused launcher CTests passed on both architectures. This is developer-machine automated evidence, not a claim that the final signed Release artifact or clean-machine installer was exercised.

## Required manual / real-surface evidence

Every item below remains `PENDING` in `tools/testdata/launcher_release_readiness/expected.json`. The validator rejects promotion of these entries by this ledger without separately recorded execution evidence.

| Evidence ID | State | Required evidence |
| --- | --- | --- |
| `real-first-window` | PENDING | Fresh tracked `HydraSeat.exe` must expose its first management window promptly or exit with a bounded actionable startup failure after the startup/integration chunk lands. |
| `real-screenshots-standard-and-narrow` | PENDING | Capture/review real normal, selected, blocked, 980x720, and narrow launcher screenshots across the shipped locales. |
| `real-keyboard-only-navigation` | PENDING | Use only the keyboard on the real launcher and verify visible focus, game-first reachability, and discoverable disabled reasons. |
| `real-high-contrast-visual` | PENDING | Temporarily enable Windows High Contrast, inspect system-color/focus readability, and restore the prior setting. |
| `real-korean-chinese-glyph-rendering` | PENDING | Verify Korean and Simplified Chinese glyph rendering/readability on a real Windows desktop without private bundled fonts. |
| `real-mixed-dpi` | PENDING | Move/resize the real launcher across mixed-DPI displays/text scaling and verify no clipped or hidden critical action/reason. |

## Defect and remaining-evidence ledger

| Finding | Automated regression | Real visual recheck | Release interpretation |
| --- | --- | --- | --- |
| `korean-selected-game-eyebrow-clipping` | COVERED | PENDING | Layout/text-size regression exists, but the original screenshot defect still needs real before/after visual confirmation. |
| `both-seats-label-truncation` | COVERED | PENDING | Localized action sizing/reflow is asserted; real rendered button readability remains unverified. |
| `duplicate-seat-status-marker` | COVERED | PENDING | Owner-drawn status-marker stripping is asserted; real visual duplication must still be checked. |
| `blocking-warning-ellipsis` | COVERED | PENDING | Wrapped blocking-reason/game-warning geometry is asserted; real screenshot readability remains unverified. |
| `windowless-startup` | EXTERNAL-STARTUP-CHUNK | PENDING | This QA chunk does not modify Agent 04 startup files. Fresh real-product first-window evidence must come from the startup/integration owner and be reviewed after integration. |

## High Contrast interpretation

Automated coverage verifies the native presentation contract selects `LauncherButtonSurface::System` / system-color behavior under High Contrast and retains focus semantics. That is a policy/code-path assertion only. `real-high-contrast-visual` remains PENDING until the real application is inspected with Windows High Contrast enabled and the previous user setting is restored afterward.

## Validator

Run the source/ledger contract check:

```text
python3 tools/validate_launcher_release_readiness.py
python3 tools/validate_launcher_release_readiness.py --self-test
```

The validator checks the exact locale/DPI/window matrix, focused CTest registration/source coverage, defect ledger, and the rule that manual/visual items remain PENDING. It intentionally does not manufacture a runtime PASS when no build directory is supplied. On a compatible configured build environment, `--ctest-dir <build-dir>` additionally requires the exact three focused CTests to pass.

## Pre-merge disposition

Automated launcher regression matrix: **PASS for the three focused tests executed above**.

Real visual/computer-use/first-window/High Contrast/mixed-DPI evidence: **PENDING**.

This document is therefore suitable as a control-tower QA ledger, but it is not by itself a release-acceptance PASS and does not replace physical, real-game, clean-machine, signing, or other manual gates.
