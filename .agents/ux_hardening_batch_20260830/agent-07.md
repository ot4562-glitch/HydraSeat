# Agent 07 — Minimal per-Seat UI

You own `CHUNK-UXH-07-SEAT-UI`.

## Start
Read `AGENTS.md`, `.agents/AGENTS.md`, the relevant `PRODUCT_V1.md` Seat UI section, your chunk section, and only the claimed files plus localization declarations read-only. No broad repository scan.

Run `python3 tools/chunk_claim.py list`, then claim exactly:

`python3 tools/chunk_claim.py claim CHUNK-UXH-07-SEAT-UI --owner uxh-07-seat-ui-20260830 --paths include/hydra/seat_launcher_model.hpp src/seat_launcher_model.cpp src/seat_ui_main.cpp tests/test_seat_launcher_model.cpp tests/test_seat_ui_process.cpp --note "minimal product Seat UI and visual cleanup"`

## Screenshot-backed problem
The current per-Seat window looks like a telemetry/debug form: repeated labels, large empty gray rows and fixture-like current/selected fields. PRODUCT_V1 calls for a minimal Seat-local launcher/status surface, and says the UI should disappear or remain non-intrusive while Playing.

## Outcome
Make the Seat surface minimal and product-like: clear Seat + Player identity, game choice when idle, concise readiness/startup state when needed, and only relevant actions such as End Playing / reconnect/retry. Remove redundant current/selected wording and developer-looking telemetry from normal presentation. During Playing, minimize visual intrusion rather than keeping a full status form. Align native colors/spacing/typography with the warm-light/navy Management launcher where practical without introducing a new UI framework.

Do not create a general desktop shell, taskbar, arbitrary app launcher or account vault. Do not change Host/session authority. If missing localized strings are required, return exact Agent 06/control-tower integration notes instead of hard-coding three languages.

## Acceptance
- idle/startup/playing/disconnected states remain truthful and distinct;
- Playing state is genuinely compact/non-intrusive;
- one obvious End Playing action when applicable;
- keyboard/accessibility behavior preserved;
- no internal generation/PID/protocol details in the normal surface.

Run `SeatLauncherModelTests`, `SeatUiProcessTests`, and build `hydra_seat_ui` on x64; x86 if practical. Finish DONE/BLOCKED with exact verification and localization notes. No Git/remote actions.
