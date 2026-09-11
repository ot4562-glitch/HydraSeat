# HydraSeat V1 Computer Use Acceptance — 2026-08-31

## Current authoritative decision

**V1 DECISION: `V1_RC_BLOCKED`**

This document records the latest authoritative GPT-5.6 Sol / high-reasoning Computer Use evidence plus central deterministic regression. Earlier runs that lacked Computer Use, used the raw CMake installer output, or misclassified the primary-window state are historical only and do not override this section.

| Gate | Current result | Authority |
|---|---|---|
| Computer Use | PARTIAL PASS | Actual Windows interaction completed substantial V1 journeys; final design/responsive re-check was interrupted by Codex/Work quota |
| Design | NOT COMPLETE | Required 21-item score/average not completed; no fabricated score |
| Controlled Product Function | CODE-INTEGRATED / COMPUTER USE RE-CHECK PENDING | Ordinary Games owns explicit risk review -> bounded local compatibility check -> canonical Controlled result -> reviewed requirement-authority publication attempt -> trusted projection refresh. If Physical authority is still absent, the UI can select an already completed P3-HW manifest without JSON editing; save and Host use fresh typed validation. Controlled evidence still cannot satisfy PhysicalOnly production trust. |
| Player Restart Persistence | PASS | Actual normal close -> new process -> Player 1 restored, Player 2 remained None |
| Player 2 Optional | PASS | Actual UI evidence plus model regression |
| Window Lifecycle | PASS | Actual Games -> Hardware -> Games round trip 3/3 after selected-game navigation-state fix |
| Play Readiness | BLOCKED — PHYSICAL/REAL-GAME ACCEPTANCE | The original missing-writer/wiring defect and P3-HW evidence-handoff gap are fixed in code; normal Play correctly remains disabled until exact Physical compatibility authority plus a release-owned reviewed game profile exist and fresh Host re-resolution succeeds. |
| Hardware Setup | PASS for observed UI/save/display flow | Actual display identity and save re-test; physical two-input isolation remains separate |
| Installer Presentation | PASS | Actual unsigned staging UI |
| Installer Mutation | BLOCKED-SIGNING | Expected: no usable production signing certificate on this machine; signature enforcement remains intact |
| x64 Regression | PASS 143/143 | Full Release build + CTest after V1 play-authority/production input-authority integration |
| x86 Regression | PASS 143/143 | Full Release build + CTest after V1 play-authority/production input-authority integration |
| Automated V1 Readiness | PASS 12/12 | Existing Automated/Mock hands-on readiness evidence only |
| V1 Play Authority | PASS 10/10 + self-test 17/17 | Automated source/contract evidence only; no manual class promoted |
| Premerge | PASS 7/7 | Automated validators only; Physical/RealGame/CleanMachine/Signing remain PENDING |
| Physical | PENDING / BLOCKED-PHYSICAL where applicable | Not synthesized from Computer Use |
| RealGame | PENDING | No real-game claim |
| CleanMachine | PENDING | No clean-machine claim |
| Signing | PENDING | No production-signed candidate claim |

## Environment and candidate

- OS observed during acceptance: Windows 11 Home, build family reported as 10.0.26200.
- UI locale: `ko-KR`.
- x64 candidate: `C:\HydraSeat\build-goal-clean-x64-20260830\Release\HydraSeat.exe`.
- x86 validation tree: `C:\HydraSeat\build-final-x86`.
- Reviewed unsigned installer presentation package: `C:\HydraSeat\qa-evidence\v1-installer-unsigned-20260831\x64\HydraSeatSetup.exe`.
- Do **not** use raw `Release\HydraSeatSetup.exe` as a distributable installer candidate.
- Actual displays observed in Hardware Setup:
  - `LG ULTRAGEAR · DISPLAY1 · 1920×1080 · 주 디스플레이`
  - `LF24T35 · DISPLAY2 · 1920×1080`
- Physical keyboard/mouse PnP inventory counts are not treated as proof of distinct physical Seat input sets.

## Actual Computer Use journeys

### 1. Startup / first window — PASS

A clean candidate launch produced one targetable primary foreground window, `HydraSeat - 게임`. The earlier automation problem was traced to Games being owned by a hidden Hardware window; central integration made Games an independent top-level product window and the actual Computer Use re-run passed.

The ordinary first screen visibly presented Player creation, required Player 1, optional Player 2, the game library, Hardware Setup, and Play/readiness without the previous ordinary-path runtime/provider/binding/Seat-use jargon.

### 2. Player creation and restart persistence — PASS

Computer Use created `Codex V1 Player` through the actual UI. The application was closed normally and a new `HydraSeat.exe` process was started. The Player profile and Player 1 selection were restored, while Player 2 remained `없음` / None. No same-process refresh was accepted as restart evidence.

The durable storage boundary is `%LOCALAPPDATA%\HydraSeat` and the launcher no longer depends on the earlier ad-hoc selection text file implementation.

### 3. Add Game — PASS through real picker path; direct semantic CUA targeting remains limited

The actual `게임 추가...` command opened the native Windows executable picker. Direct Computer Use semantic targeting was unreliable:

1. pointer targeting of the file-name field remained on the search field;
2. Alt+N / Tab navigation remained on the search field;
3. UIA click/SetValue hit a target-window/UIA-cache failure.

A later visible file-name-field input path entered the exact executable path and closed the real picker without direct JSON/state injection. `hydra_window_test_app.exe` appeared in the HydraSeat library. A separate Ctrl+L experiment that launched the test app directly was explicitly discarded and is **not** product evidence.

Therefore the product Add Game path itself was exercised successfully. The remaining native-dialog semantic targeting issue is a Computer Use runtime limitation, not the current product P1.

### 4. Hardware Setup persistence — defect found, fixed, actual re-test PASS

Computer Use reproduced:

`Could not save workspace_config.json. The previously loaded Seat model was left unchanged.`

The root cause was the legacy relative working-directory `workspace_config.json` path. The Hardware configuration now resolves the same bounded user-state root used by launcher state and persists to:

`%LOCALAPPDATA%\HydraSeat\workspace_config.json`

The change reuses `launcher_state::defaultUserStateRoot`, validates/creates the user-state root, keeps the existing strict WorkspaceManager schema, and does not persist runtime HWND authority. Save/load messages were made actionable/localized. The rebuilt candidate was re-tested through the actual Hardware UI and save succeeded.

### 5. Display identity — PASS

The actual Hardware UI distinguished the two connected displays by friendly name, DISPLAY identity, resolution and primary state. The UI did not require the user to infer identity from two generic monitor labels.

### 6. Input identification UX — improved; physical authority still pending

The keyboard identification waiting state was visible and understandable. Computer Use then found a genuine interaction defect: while waiting there was no explicit Cancel action and Escape could be interpreted as identification input.

The fix changes the identify action to an explicit localized Cancel state while waiting and connects cancellation semantics. English, Korean and Simplified Chinese strings/tests were updated. The actual rebuilt UI displayed `식별 취소`.

This does **not** convert synthetic/Computer Use input into proof of two distinct physical keyboards/mice. Physical identity/isolation remains separate evidence.

### 7. Games <-> Hardware lifecycle — PASS 3/3

The first actual round trip exposed a P2: Player state survived but the selected game was lost because the Games `WindowState` was destroyed during navigation. The fix adds bounded in-process `LauncherNavigationState` carrying only selected Game identity across Games <-> Hardware transitions; it is not durable runtime authority.

After the fix, Computer Use restarted the count and completed three successive Games -> Hardware Setup -> Back to Games cycles. Each retained:

- one meaningful foreground HydraSeat window;
- Player 1;
- Player 2 = None;
- selected `hydra_window_test_app`;
- visible Hardware assignments.

No ghost/zombie product window was observed in those cycles.

### 8. Keyboard-only interaction — major flow PASS after fix

Actual keyboard navigation covered Player/library controls, Hardware Setup, identification/cancel and Back to Games. Space activated buttons, but Enter initially did nothing on focused native Buttons. This was fixed in both Games and Hardware message loops by activating the exact focused enabled Button on Enter.

The rebuilt candidate was re-tested: Enter activated the relevant focused controls on both surfaces. One UIA focus-label mismatch remained an automation metadata limitation because the visible dotted focus and actual Enter action agreed.

### 9. Responsive/minimum-size behavior — defect fixed in code; final visual re-check pending

Computer Use observed Games remaining usable at a small practical 96-DPI size, but Hardware Setup could shrink to about 636×413 and visibly clip upper controls/device content.

The final repair sets a DPI-scaled 980×750 Hardware minimum tracking size and adds end ellipsis for long selected-game presentation text. The final edit was made immediately before Codex/Work usage exhaustion, so its **post-fix visual Computer Use re-check remains pending**.

Central deterministic verification after the quota interruption rebuilt the affected candidate and passed the focused UI/localization/input/persistence/journey/process tests.

### 10. Installer presentation — PASS; mutation BLOCKED-SIGNING

The correct unsigned staging package was opened with Computer Use. It showed:

- `Current state: Not installed`;
- `Package: incomplete or unsigned`;
- destination `C:\Program Files\HydraSeat`;
- disabled Install / Repair / Uninstall controls;
- a clear Close action.

This is expected security behavior. The machine has no usable production code-signing certificate and `signtool.exe` was not available on PATH during review. Signature/provenance checks were **not** weakened to manufacture an install PASS.

## V1 Play authority — product wiring closed, acceptance authority still pending

The 2026-08-31 Computer Use run correctly found Play disabled after registering the controlled executable and saving Hardware Setup. At that time the ordinary product had no release-target/user path to produce the trusted runtime requirement authority. The 2026-09-03 control-tower integration has now closed that product-wiring defect without weakening trust.

Current integrated behavior:

- production still resolves trusted requirements from the validated per-user runtime requirement store;
- the launcher consumes only a successful current projection;
- the Host independently re-resolves trusted authority before plan installation and again before Seat activation;
- missing/corrupt/stale/untrusted evidence still fails closed;
- production trust remains `LocalEvidenceTrust::PhysicalOnly`;
- Games now owns the ordinary `selected Game -> explicit protection-risk review -> bounded exact-executable local check -> canonical local result -> requirement review -> authority publication attempt -> refresh trusted projection` flow; Protected / Experimental and Unknown risk both fail closed before process launch, and Standard is no longer hard-coded;
- the local runner records Controlled evidence only and cannot manufacture or relabel Physical evidence;
- if the trusted projection is still unavailable, Games can select an already completed `phase3-hardware-manifest.json` through the product UI instead of JSON editing; the product persists only the manifest reference, validates it through the typed P3-HW loader on save, and the Host revalidates it on every fresh production-input composition;
- Pending, stale, tampered, missing, or malformed P3-HW evidence cannot yield `ProductionInputEvidenceClass::Physical`; this prerequisite handoff is not itself a Physical compatibility verdict;
- exact Gate C game profiles remain release-owned only and the current table is intentionally empty while P3-E-02 is blocked, so valid P3-HW evidence alone still cannot enable Play;
- `tools/validate_v1_play_authority.py` and premerge now enforce the writer, runner, explicit reviewed-risk Games integration, typed Physical-input prerequisite boundary, Host re-resolution, origin boundary, and evidence-class separation contracts;
- Host production composition includes the recovery/input activation bridge and an architecture-matched exact-process Gate C receiver DLL, while a missing exact accepted game input profile still fails closed.

The original no-writer/no-Games-wiring P1 is therefore **fixed in code**. It is not yet an acceptance PASS: normal Play must remain disabled when the only evidence is Controlled, and this machine/session has not produced the required Physical compatibility/input authority for a lawful real game. No completion may be manufactured by permissive defaults, hard-coded test apps, direct JSON injection, Controlled-to-Physical relabeling, community/imported authority, or weakened Host re-resolution.

## Design review status

**NOT COMPLETE.** No numeric average is fabricated.

Actual Computer Use has already established and repaired several design/accessibility issues (primary-window behavior, Hardware save feedback, selected-game navigation persistence, identification cancellation, Enter activation, Hardware minimum-size clipping). However the required 21-item final score, post-final-resize visual check, remaining small/normal/maximized review and complete visual consistency score were interrupted by the Codex/Work quota limit.

A future GPT-5.6 Sol / high-reasoning Computer Use continuation must finish the score after the Play-authority integration so design is evaluated against the actual final V1 flow rather than an intermediate blocked UI.

## Automated regression after V1 play-authority integration

Central deterministic verification after the 2026-09-03 integration, which does not constitute new Computer Use or Physical evidence:

- x64 Release full build: **PASS**
- x64 CTest: **143/143 PASS**
- Win32/x86 Release full build: **PASS**
- Win32/x86 CTest: **143/143 PASS**
- V1 Play Authority CTest label: **4/4 PASS** on both architectures
- `python3 tools/validate_v1_play_authority.py`: **10/10 PASS**
- `python3 tools/validate_v1_play_authority.py --self-test`: **17/17 PASS**
- `python3 tools/validate_production_reachability.py`: **9 components PASS, 0 inactive**; self-test: **12/12 PASS**
- `python3 tools/validate_v1_hands_on_readiness.py`: prior **12/12 PASS** for Automated/Mock evidence only
- `python3 tools/run_premerge_gate.py ...`: **7/7 PASS** automated validators; manual gates explicitly remain PENDING

During this final matrix the new trusted-seat-scope contract exposed seven stale deterministic fixtures whose `GameRuntimeRequirement::validatedSeatCount` remained at the fail-closed default `0`. Those fixtures were updated to the exact one- or two-Seat scope they already intended to model; production trust logic was not loosened. The later production-input-authority work added one dedicated CTest covering missing/Pending/tampered/malformed P3-HW selections and fresh typed revalidation. The rebuilt full matrix now passes 143/143 on both architectures.

Known MSVC `MSB8029` intermediate/output-directory warnings remain pre-existing and are not represented as warning-free builds. CodexPro `show_changes` could not produce its normal diff review because its workspace selector reverted to the configured `C:\` root, but `open_workspace(C:\HydraSeat\repo)` did confirm the repository Git status and the control tower did not reset or discard the pre-existing agent worktree.

## Bugs found in authoritative Computer Use run

| ID | Severity | Defect | Current state |
|---|---|---|---|
| CUA-V1-001 | P0/P1 startup usability | Games was owned by hidden Hardware HWND, confusing primary-window automation/Windows semantics | FIXED + actual re-test PASS |
| CUA-V1-002 | P1 | Hardware workspace save depended on working directory | FIXED + actual re-test PASS |
| CUA-V1-003 | P2 | Games selection lost across Hardware navigation | FIXED + lifecycle actual 3/3 PASS |
| CUA-V1-004 | P2 | Identification waiting state lacked explicit Cancel / Escape semantics | FIXED + visible Cancel re-check |
| CUA-V1-005 | P2 accessibility | Focused buttons activated with Space but not Enter | FIXED + actual re-test PASS |
| CUA-V1-006 | P2 responsive UI | Hardware could shrink until important UI clipped | FIXED in candidate; final visual re-check pending |
| CUA-V1-007 | P1 | No ordinary first-use path created trusted runtime requirement authority, so Play could not become ready | FIXED IN CODE — Games/writer/runner/authority/refresh path integrated; post-integration Computer Use + Physical/RealGame authority still pending |

## Remaining RC work

1. Re-run the actual Games compatibility/review flow with Computer Use against the rebuilt 2026-09-03 candidate and confirm the controlled state plus completed-P3-HW manifest selection are understandable without JSON editing.
2. Complete P3-HW-01 with the required real two-keyboard/two-pointing-device physical evidence; the new product manifest-selection path only imports that already accepted prerequisite by reference and Controlled/ComputerUse evidence cannot substitute for it.
3. Establish the first explicit lawful non-protected real-game Gate C profile/approval after the physical suppression/isolation path is proven; do not infer API mask/attach/cloaking permission from an unreviewed game.
4. Complete the 21-item Computer Use design score and post-resize visual re-check.
5. Complete independent RealGame, clean-machine installer/UAC/reboot/uninstall, and production Signing gates.

The full deterministic x64/x86 gates have already been re-run after the integration and are green. Physical two-input isolation, actual RealGame acceptance, clean-machine acceptance, and production signing remain independent release gates and are not synthesized by this report.

## Historical supersessions

- An early run had no Computer Use plugin and was `BLOCKED_BY_ENVIRONMENT`; it provides no current product verdict.
- A later startup run mixed stale processes/build-tree state with a real owned-window defect; the defect was fixed and the actual clean Computer Use startup passed.
- An installer run opened raw `Release\HydraSeatSetup.exe`; this was the wrong package boundary. Current installer presentation evidence uses the reviewed unsigned `x64\HydraSeatSetup.exe` staging package.
- Native file-picker direct semantic targeting remains imperfect in the Computer Use runtime, but the actual picker path later registered the controlled executable without direct state injection. It is no longer the authoritative Controlled Product Function blocker.

## Final current verdict

**`V1_RC_BLOCKED`**. The original first-use no-writer/no-Games-wiring P1 is fixed in code, but the rebuilt flow has not yet received post-integration Computer Use acceptance or the Physical authority required by normal production Play; the 21-item design score is also unfinished.

The current blocker is not x64/x86 automated stability: both Release builds and both 143/143 suites are green, V1 authority is 10/10 with a 17/17 fixture self-test, production reachability is 9 components, and premerge is 7/7. The remaining blockers are truthful acceptance/evidence gates: actual Physical input/isolation, first lawful RealGame profile/session evidence, final Computer Use/design review, clean-machine installation, and production Signing.
