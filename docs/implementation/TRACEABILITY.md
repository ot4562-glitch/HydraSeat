# HydraSeat v1 Product Requirement Traceability

This document maps the canonical v1 product contract in `docs/PRODUCT_V1.md` to implementation packets and evidence. It is intentionally product-focused: historical research/testing details remain in their owning documents.

| Product requirement | Owning decisions / packets | Required evidence before v1 claim |
| --- | --- | --- |
| Use spare performance of one capable Windows gaming PC for two local players instead of requiring a second complete desktop | PRODUCT_V1, D-039, P10-PERF-01 | Two concurrent real-game workload/resource measurements on reference hardware; no universal performance promise |
| Exactly two active Seats in v1 | D-039, P4-SEAT-01, P5-LAUNCH-01, P10-SCOPE-01 | Third active Seat rejected; all v1 UI/installer/test matrix capped at two |
| Seat represents physical station hardware, not a person/game/account | D-001, D-040, P6-SCHEMA-01 | Separate schema/model tests; Player/Game runtime bindings do not persist into Seat identity |
| Player is independent from Seat | D-040, P6-SCHEMA-01, P6-UI-01, P6-CLOSE-01 | Same Player moves Seat 1 <-> Seat 2 while preferences/account references follow the Player |
| Game is discovered/selected independently from Seat/Player | D-040, D-049, P6-CATALOG-01, provider packets | Local read-only discovery plus manual EXE fallback; stable provider/install identity |
| First-run Seat setup can be skipped and individual device fields may be `Set later` | D-040, D-048, P7-CLOSE-01, P8-INST-01 | Installer/first-run acceptance with skipped/partial setup; later game preflight identifies only required missing devices |
| One Seat may have one or more displays | D-002, P4-DIS-01, P4-DIS-02 | Stable physical identity, multi-display transform tests, real physical Seat grouping |
| Input is isolated by physical Seat with objective evidence | P3-HW-01, P3-D-02, P3-E-02, P3-E-03, P5-MET-01 | Real two-input physical acceptance and receiver-aware zero-cross evidence for declared game scenarios |
| Background runtime is authoritative; UI may close/reopen | D-019/D-031, P4-RUN-01, P4-IPC-01, P4-CTRL-01 | Real process/IPC reconnect tests; closing UI leaves runtime unchanged |
| One Seat can stop/change games while the other continues | D-042, P4-SEAT-01, P5-MVP-02, P7-LAUNCH-01 | `Seat 1 = Playing, Seat 2 = Idle` healthy state; stop/restart one controlled and real Seat while other process/game remains alive |
| Idle Seat remains under a minimal HydraSeat launcher while another Seat is active | D-041/D-042, P7-SHELL-01, P7-LAUNCH-01 | Two physical Seat UI flows; idle Seat selects Player/game without exposing unrestricted general desktop behavior |
| v1 is game-only, not a general independent Windows desktop | D-041, P7-TASK-01/P7-DESK-01/P7-CLIP-01/P7-EXT-01 deferred | Release scope/docs/UI contain no dependency on general taskbar/wallpaper/clipboard/arbitrary-app features |
| Game-first normal UX | D-041, P6-UI-01, P7-CLOSE-01, P10-UX-01 | Non-developer flow: Game -> Seat 1/Seat 2/Both -> Player(s) -> warnings -> Play |
| Click/tap first; drag-and-drop optional | PRODUCT_V1, D-041, P7-CLOSE-01 | UI acceptance without requiring drag-and-drop; optional drag path shares the same model |
| Automatic local game discovery with manual EXE fallback | D-049, P6-CATALOG-01, P6-PROV-02/P6-PROV-03*, P6-CLOSE-01 | Provider fixture/live discovery and custom executable path |
| Use local/provider icons where possible instead of redistributing a large artwork library | D-049, P6-CATALOG-01, P10-LIC-01 | Icon source/provenance review; missing art does not block catalog |
| Player/provider account associations without turning HydraSeat into a credential vault | D-040, P6-PROV-01/P6-PROV-02, P6-UI-01 | Provider-owned authentication; no password/token fields in schema/logs/bundles |
| Same game on both Seats uses a reusable TwoPlayerSetup | D-043, P6-PROFILE-01, P6-PLAN-01 | Setup validates against exact local game/provider/version before launch |
| HydraSeat attempts safe automatic TwoPlayerSetup generation | D-043, P6-PROFILE-01 | Read-only candidate generation, visible mutation preview, deterministic validation |
| Guided manual TwoPlayerSetup remains available | D-043, P6-PROFILE-01, P6-UI-01 | Unknown lawful fixture/title can be configured through typed UI without arbitrary script execution |
| At least one lawful same-title/two-instance real demonstration | P6-CLOSE-01, P10-COMPAT-01 | Exact game/provider/version/rules recorded; two independent instances and Seat lifecycle pass; no bypass |
| No anti-cheat/DRM/account/launcher/single-instance bypass | D-007, D-043, D-045, P9-SEC-01, P10-SEC-01 | Static/security review and negative tests; protected experiments never add bypass mechanisms |
| Protected games may be explicitly warned experiments | D-045, P6-PREFLIGHT-01, P7-NOTIFY-01, P10-COMPAT-01 | Strong warning + explicit acknowledgement; results remain Protected/Experimental |
| Successful protected-game run never proves anti-cheat safety | D-045, P9-SDK-01/P9-CAP-01, P10-COMPAT-01 | Schema/UI cannot convert technical success into anti-cheat safety/certification |
| Compatibility is evidence, not an official certification badge | D-044, P5-COMPAT-01, P9-CAP-01, P10-COMPAT-01 | Success/failure/sample/sub-result display; no required `Certified` field/badge |
| Community compatibility percentages are segmented by material environment | D-044, P9-CAP-01, P9-REG-01 | Deterministic cohort tests by game/HydraSeat/provider/Windows/path versions |
| `Untested` is distinct from failure | D-044, P9-CAP-01, P9-CLOSE-01 | UI/schema fixtures for no evidence vs failed evidence |
| Compatibility tests run locally first | D-046, P5-MET-01, P9-DIAG-01 | Local result available with network disabled before any sharing action |
| Community sharing is explicit opt-in with exact redacted JSON preview | D-046, P9-RPC-01, P9-DIAG-01, P10-PRIV-01 | Decline/submit/offline/retry tests; preview matches transmitted payload |
| Shared evidence excludes credentials/raw text/Player names/personal paths by default | D-046, P8-DIAG-01, P9-SDK-01, P10-PRIV-01 | Privacy/redaction fixture corpus and manual preview |
| Core operation works offline | D-047, P8-DATA-01, P8-SOAK-01, P9-REG-01 | Network-disabled Seat/Player/catalog-cache/setup/launch/runtime/recovery acceptance |
| Compatibility/setup catalog refresh is separate from program update | D-047, P8-DATA-01, P8-UPD-01 | Data refresh changes catalog only; program binary version unchanged |
| Program/runtime/driver updates require user approval | D-047/D-048, P8-UPD-01, P10-UX-01 | Update available -> no install until explicit approval; rollback tests |
| Normal operation is least privilege | D-048, P8-PRIV-01, P8-INST-01 | Main UI/ordinary host remain unelevated; broker rejects arbitrary admin execution |
| Real Windows installer/repair/uninstaller is mandatory | D-048, P8-INST-01, P10-RC-01 | Clean-machine install/repair/uninstall and ordinary Windows postconditions |
| Watchdog/crash/emergency recovery is independent and bounded | P8-WATCH-01, P8-JOURNAL-01, P8-RESET-01, P4-REC-01 | Existing validated watchdog/journal/reset evidence plus full production runtime fault matrix |
| Return to Windows is verified rollback, not UI close | D-019/D-031/D-042, P4-CTRL-02, P5-MVP-02, P8-SOAK-01 | Exact owned resources restored/removed, no orphans, ordinary Windows postconditions |
| Physical displays are required path; virtual display driver is not a v1 dependency | D-009/D-021, P4-DIS-01..03; P4-VID-01/02/P4-IDD-01 deferred | Complete physical display acceptance succeeds with no virtual-display component installed |
| Two different real games run concurrently | P3-E-02/P3-E-03, P5-MVP-02, P10-COMPAT-01 | Exact game/provider/version evidence, receiver-aware input, window/display/controller/audio as declared |
| One-developer scope is a design constraint | D-050, all deferred packets | v1 critical path excludes N-Seat, full shell, custom IDD, broad binary SDK unless explicitly reactivated |
| English/Korean/Simplified Chinese UI readiness | D-035, P7-I18N-01, P7-A11Y-01, P10-UX-01 | Catalog parity, localized critical journeys, DPI/accessibility acceptance |
| Project may be described as open source only after license/contribution gate | D-036, P10-LIC-01, P10-GA-01 | LICENSE/contribution/notices/provenance review complete before GA wording changes |

## Interpretation rule

A row is not satisfied because a design document exists. The owning packet's declared evidence must actually be recorded in `STATUS.md`/compatibility data/CI/manual acceptance as appropriate.

Historical controlled Phase 3 evidence proves its exact test boundary only. It does not automatically satisfy later physical/game/product requirements in this table.