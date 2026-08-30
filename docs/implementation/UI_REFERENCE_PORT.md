# HydraSeat UI Reference Port

Status: implementation note for the native Win32 launcher.

This note records the UI/UX reference pass performed while the six production-hardening workers were running. It exists so later integration work does not have to rediscover the same decisions.

## Source authority used

The primary design source is the user's own local repository:

- `C:\hufs-mc-web\contracts\design\01_DESIGN_DIRECTION.md`
- `C:\hufs-mc-web\contracts\design\04_COLOR_AND_SURFACE_SYSTEM.md`
- `C:\hufs-mc-web\contracts\design\05_BORDER_AND_RADIUS_SYSTEM.md`
- `C:\hufs-mc-web\contracts\design\06_COMPONENT_INTENSITY_MATRIX.md`
- `C:\hufs-mc-web\apps\admin-source\src\styles\tokens.css`
- `C:\hufs-mc-web\apps\admin-source\src\styles\primitives.css`
- `C:\hufs-mc-web\apps\admin-source\src\styles\shell.css`
- `C:\hufs-mc-web\apps\public\src\components\ui\Primitives.tsx`
- `C:\hufs-mc-web\apps\public\src\pages\home\HomePage.tsx`

Secondary implementation references already vendored under `references/`:

- Nucleus Co-op: game-first library, selected-game detail, Play flow, visual player placement.
- Universal Split Screen: explicit readiness/activation gating and direct device feedback.
- Proto Input: expert diagnostics and low-level input state presentation.
- SplitScreen.Me Hub: game cover/list metadata and install/update state separation.

## What HydraSeat copies

### 1. Light-only structural surface system

HydraSeat now uses the same family of native equivalents as `hufs-mc-web` Admin Source:

- warm ivory application canvas;
- light raised surfaces for important grouped content;
- navy for hierarchy, primary identity, and focus;
- bronze only as a short structural accent;
- no dark page/header/hero surface;
- no glow or decorative gradient;
- no large hover-only motion;
- semantic green/warning/red/neutral only for actual state.

The Win32 palette lives in `include/hydra/launcher_layout.hpp` and intentionally mirrors the source token roles rather than copying CSS mechanics.

### 2. Surface hierarchy instead of card spam

The launcher treats the selected game as the representative panel. Seat rows and the library remain structural/open regions separated by lines and spacing. This follows the HUFS rule that not every piece of content becomes a card.

### 3. Status is not color-only

Existing text labels remain authoritative (`Ready`, `Needs attention`, launch reason, etc.). Color is supplemental:

- ready -> success;
- needs attention -> warning;
- blocking launch reason -> danger;
- no selection -> neutral/muted.

High Contrast mode keeps system colors and does not depend on the custom palette.

### 4. Game-first information architecture

Nucleus Co-op's strongest idea is retained: game selection is the main path. Expert input/process/provider details must not become the first screen. HydraSeat keeps:

`Game library -> selected game -> assign Seat(s) -> readiness -> Play`

Setup, privacy history, and diagnostics remain secondary.

### 5. Explicit action hierarchy

From HUFS Admin Source:

- controls retain a minimum 44 px target through launcher metrics;
- one primary launch action wins over secondary configuration actions;
- disabled state must still explain why via launch reason/preflight text;
- destructive/privacy actions belong to the secondary diagnostics surface, not the main game flow.

## What HydraSeat deliberately does not copy

### Nucleus Co-op

Do not copy its global `UI_Interface` control registry, timer-driven UI state, title-based process/window authority, or handler scripting model. Only the information architecture and useful presentation patterns are references.

### Universal Split Screen / Proto Input

Do not expose their expert-first hook/device controls on the ordinary launcher surface. Their explicit state feedback is useful; their default information density is not.

### SplitScreen.Me Hub

Community popularity, stars, download count, comments, and remote verification badges are not runtime authority. HydraSeat may use local trusted provider/catalog metadata for presentation, but community metadata cannot authorize activation.

### hufs-mc-web Public

Do not copy route artwork, Minecraft branding, campus identity, mascot, dark legacy hero treatment, web-only blur, or decorative effects. The reusable part is the design system and progressive information hierarchy.

## Native Win32 port completed in this pass

Files changed:

- `include/hydra/launcher_layout.hpp`
- `src/launcher_win32.cpp`

Implemented:

1. Ported the Admin Source light token roles into `LauncherPalette`.
2. Removed the large dark navy header/selected-game surfaces.
3. Added light raised header and selected-game surfaces.
4. Added a restrained bronze top accent to the representative selected-game panel.
5. Added structural borders for header, selected-game panel, Seat rows, and launch bar.
6. Added semantic text color for Seat readiness and launch/preflight state.
7. Preserved Windows High Contrast fallback.

## Current UI implementation and remaining visual acceptance

The focused launcher UI worker has since implemented the first presentation layer that was previously listed as follow-up work:

1. bounded owner-drawn game rows with local provider/version/readiness metadata;
2. native primary/secondary/quiet/danger button presentation;
3. Seat status markers using shape + text + semantic color;
4. same-HWND narrow-window layout;
5. native keyboard-focus and High Contrast rendering paths.

These automated/layout changes are not a substitute for looking at the real application. A partial computer-use acceptance pass on 2026-08-30 was interrupted by the Codex usage limit, but it produced concrete visual findings before any launcher source edit:

- the Korean selected-game eyebrow clipped at the standard 980 x 720 presentation (`"선택한 게이..."`);
- the `Both Seats` action text truncated at the standard layout;
- Seat status presentation showed a duplicated ring/marker;
- owner-drawn game warning text collapsed to an ellipsis instead of preserving a useful blocking explanation;
- the freshly rebuilt real `HydraSeat.exe` remained alive without exposing a launcher window on two launches, so the visual pass had to use a temporary launcher-only harness. Treat this as a separate startup/integration defect until reproduced after current concurrent integration settles.

### Mandatory sizing/localization rules for the next UI pass

Future UI work must treat localized text size as a layout input, not an afterthought:

- measure the actual rendered localized string using the current font and DPI before assigning critical control widths;
- do not derive Korean/Chinese button widths from English literals;
- primary actions, Seat selectors, section identity, and blocking readiness reasons must not be silently ellipsized;
- ellipsis is acceptable only for genuinely secondary metadata when the full value is available elsewhere;
- prefer adaptive width/height, wrapping, or layout reflow over shrinking important text below the established readable font size;
- verify at minimum 96/120/144/192 DPI and 100%/125%/150%/200%-class text/scaling behavior where the environment permits it;
- the standard 980 x 720 layout must fit the longest shipped localized labels without clipping;
- narrow layouts must preserve the same control state while reflowing geometry; do not create a second duplicate control hierarchy;
- status indicators must not duplicate shape markers or repeat the same semantic cue unnecessarily;
- High Contrast must continue to use system colors;
- every visual acceptance pass should include real screenshots and keyboard-only navigation, not only rectangle/unit tests.

The next visual acceptance should explicitly reproduce the four screenshot defects above and capture before/after evidence for each fix.

## Product constraint

Reference code is presentation guidance only. Trusted runtime requirement, compatibility evidence, publisher trust, Seat ownership, process/window identity, and activation policy remain authoritative in their production modules. UI state must never weaken those boundaries or manufacture a successful readiness state.
