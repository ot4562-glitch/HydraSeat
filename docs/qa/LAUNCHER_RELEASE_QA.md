# Launcher Release QA

Date: 2026-08-30
Scope: screenshot-backed native Win32 UX regression gate for `CHUNK-UXH-09-UX-REGRESSION-GATE`.

This ledger prevents a green automated count from being described as good UX when the repository has lost objective launcher contracts. Automated regression coverage is not screenshot evidence. Source/layout assertions, controlled process tests, and model tests can prove bounded behavior; they cannot score visual taste, prove physical hardware behavior, or certify a real game/provider combination.

## Objective automated contracts

The release-readiness matrix is schema v2. It fixes the shipped locale set to `en-US` / `ko-KR` / `zh-CN`, the reviewed launcher DPI set to 96/120/144/192, and the standard 980x720 plus narrow 640x640 geometry profiles. Generated build projects are not read as source authority.

| Contract ID | CTest evidence | Objective rule enforced | Explicit limit |
| --- | --- | --- | --- |
| `localization-contract` | `UiLocalizationTests` | Three shipped locales, English fallback, localized critical-label width floors, single owner-drawn status cue | Does not prove real glyph rasterization or translation quality |
| `layout-accessibility-contract` | `UiAccessibilityTests` | Locale/DPI matrix, standard+narrow reflow, Game Library-first focus, long blocking reason visibility, High Contrast system-color policy | Does not score spacing/color taste |
| `critical-action-text-contract` | `UiAccessibilityTests` | Critical Seat actions are measured from localized text, fit supported layouts, and blocking reasons wrap instead of depending on clipping/ellipsis | Does not claim every arbitrary window size is supported |
| `game-first-normal-flow-contract` | `UiAccessibilityTests` | Ordinary control flow starts from Game Library and keeps Seat selection/Play as primary actions | Setup/Diagnostics remains available but is not the happy-path starting surface |
| `hardware-assignment-contract` | `ControlSurfaceModelTests` + native hardware-setup source contract | The source contract requires click selection, Enter/Space selection, keyboard-focusable Assign Seat 1/Seat 2/Unassign controls and their command dispatch; `ControlSurfaceModelTests` separately preserves incomplete Seat hardware as a valid editable state | Automated source/model coverage does not prove a real physical device was assigned correctly; real physical identity/assignment remains PENDING |
| `seat-minimality-contract` | `UiAccessibilityTests` | Normal compact Seat state has the bounded `End Playing` action; extra safety/recovery actions appear only when required | Human review still decides whether the real surface feels visually unobtrusive |
| `advanced-detail-separation-contract` | `UiAccessibilityTests` | Diagnostics/compatibility/local-result detail remains on the advanced page while ordinary launcher controls remain separately visible | Does not ban concise user-facing readiness reasons on the normal page |
| `first-window-responsiveness-contract` | `HydraStartupWindowProbe` | Explicit `hydra_tests --startup-window-probe <HydraSeat.exe>` execution requires a bounded first management window and bounded UI-thread responsiveness rather than accepting an alive-but-windowless process | Final release-artifact/clean-machine first-window evidence stays manual |
| `disabled-readiness-contract` | `LauncherUiModelTests` | Missing trusted requirement evidence blocks Play and activation requires a validated current plan | Does not prove a provider/game can really launch |

The source-contract side also requires the reviewed CTest registrations, localized width assertions, standard/narrow critical geometry assertions, one-action compact Seat policy, localized text measurement/wrapping, the keyboard-focus Game Library entry, the explicit hardware-setup button, and primary-vs-advanced Win32 control separation. This intentionally checks observable product contracts rather than subjective styling constants.

## UX contract values

The positive fixture `tools/testdata/launcher_release_readiness/expected.json` fixes these machine-reviewable contracts:

- `criticalActionText = measured-wrap-no-critical-ellipsis`
- `primaryActionOrder = GameLibrary -> SeatSelection -> PlayerSelection -> Play`
- `hardwareAssignment = non-drag-assignment-required`
- `seatSurface = minimal-seat-local-actions`
- `advancedDetails = separate-advanced-page`
- `firstWindow = bounded-real-process-responsive`

`tools/testdata/launcher_release_readiness/bad_ux_contracts.json` contains one deterministic negative mutation for every contract above. The validator self-test must reject every mutation. There is deliberately no static rule for preferred hue, shadow, rounded-corner taste, visual density taste, or other subjective aesthetics.

## Required manual / real-surface evidence

Every item below remains `PENDING` in `tools/testdata/launcher_release_readiness/expected.json`. None may be promoted because the source validator or a synthetic/model CTest passed.

| Evidence ID | State | Required evidence |
| --- | --- | --- |
| `real-first-window` | PENDING | Run the final tracked Release `HydraSeat.exe` on the intended release environment and record first-window timing or bounded actionable startup failure. |
| `real-screenshots-standard-and-narrow` | PENDING | Capture/review normal, selected, blocked, 980x720, and narrow real screenshots in all shipped locales; human visual taste remains here. |
| `real-keyboard-only-navigation` | PENDING | Use only the keyboard on the real launcher and verify visible focus, game-first reachability, and disabled-reason discovery. |
| `real-high-contrast-visual` | PENDING | Enable Windows High Contrast temporarily, inspect real system-color/focus readability, then restore the prior setting. |
| `real-korean-chinese-glyph-rendering` | PENDING | Verify Korean and Simplified Chinese glyph rendering/readability on a real Windows desktop without private bundled fonts. |
| `real-mixed-dpi` | PENDING | Move/resize the real launcher across mixed-DPI displays/text scaling and verify no clipped/hidden critical action or stale geometry. |
| `real-hardware-assignment-without-drag` | PENDING | With real keyboards/mice, assign and reassign Seat hardware using click/keyboard without drag and verify the intended physical identity before save. |
| `real-seat-ui-visual-minimality` | PENDING | Inspect real Idle/Playing/recovery Seat surfaces and confirm they remain a minimal game surface rather than a general shell or telemetry form. |
| `real-game-happy-path` | PENDING | Exercise the real game-first flow with supported provider/game evidence and independently stop/change one Seat; synthetic UI tests are not compatibility evidence. |

## Screenshot-derived defect ledger

| Finding | Automated regression | Real visual recheck | Release interpretation |
| --- | --- | --- | --- |
| `korean-selected-game-eyebrow-clipping` | COVERED | PENDING | Localized measured geometry exists; the original screenshot defect still needs human before/after review. |
| `both-seats-label-truncation` | COVERED | PENDING | Critical action widths are localized/measured and reflowed; real rendered button readability remains unverified. |
| `duplicate-seat-status-marker` | COVERED | PENDING | Owner-drawn marker stripping is asserted; real visual duplication must still be checked. |
| `blocking-warning-ellipsis` | COVERED | PENDING | Blocking-reason geometry is wrapped and viewport-bounded; screenshot readability remains unverified. |
| `windowless-startup` | COVERED | PENDING | `HydraStartupWindowProbe` explicitly runs the real built launcher and rejects alive-but-windowless or unresponsive startup; final release-artifact first-window acceptance remains manual. |

## Diagnostics and compatibility interpretation

The ordinary launcher may show concise readiness or blocking reasons needed to make Play understandable. Detailed diagnostics, compatibility/local-result management, privacy controls, and setup administration remain behind the advanced page. The validator checks the primary/advanced Win32 control separation instead of banning all compatibility-related words from the happy path.

## High Contrast interpretation

Automated coverage verifies the native presentation contract selects `LauncherButtonSurface::System` / system-color behavior under High Contrast and retains focus semantics. This is a code-path assertion only. `real-high-contrast-visual` remains PENDING until the real application is inspected with Windows High Contrast enabled and the previous user setting is restored afterward.

## Validator

Run the source/ledger and deterministic fixture contract:

```text
python3 tools/validate_launcher_release_readiness.py
python3 tools/validate_launcher_release_readiness.py --self-test
```

On a compatible configured MSVC tree, require the reviewed CTests as well:

```text
python3 tools/validate_launcher_release_readiness.py --ctest-dir build-agent6-msvc
python3 tools/validate_launcher_release_readiness.py --ctest-dir build-agent6-msvc-x86
```

The focused CTest set is `UiLocalizationTests`, `UiAccessibilityTests`, `ControlSurfaceModelTests`, and `LauncherUiModelTests`; the same `--ctest-dir` invocation separately runs `HydraStartupWindowProbe` against that tree's real `HydraSeat.exe`. A run without `--ctest-dir` validates only source/ledger/fixture presence and explicitly reports that runtime tests/probe were not executed.

### Developer-machine verification on 2026-08-30

After explicitly rebuilding the four focused test targets, `hydra_tests`, and `HydraSeat` in both configured Release trees, the x64 gate passed 4/4 focused CTests and observed the first real launcher window in 437 ms. The Win32/x86 gate passed 4/4 focused CTests and observed the first real launcher window in 250 ms. Both probes identified `HydraSeatGameLibraryWindow` and passed the bounded UI-thread responsiveness check. These are Controlled developer-machine regressions, not clean-machine, signed-artifact, screenshot, physical-hardware, or real-game acceptance evidence.

## Pre-merge disposition

Objective source/fixture contract: **must PASS before integration**.

Reviewed focused CTests: **must PASS in the available configured Windows trees before this chunk is marked DONE**.

Human screenshot approval, visual taste, physical hardware, real-game, clean-machine, signing, and other manual release evidence: **PENDING**.

This ledger is a regression gate and evidence separator, not a claim that HydraSeat has achieved subjective visual polish or final release acceptance.
