# HydraSeat Reference Research Index

## Purpose

`C:\HydraSeat\references` is a **read-only research library**, never a HydraSeat build input. This index records what was learned from the current local snapshots so future workers do not repeatedly rediscover the same architecture or accidentally copy incompatible source.

The legal/engineering boundary is defined by [`CLEAN_ROOM_POLICY.md`](CLEAN_ROOM_POLICY.md): external implementation is translated into neutral requirements, failure modes and independently written HydraSeat tests. Reference checkouts are not modified or vendored into the product.

## Current high-value references

| Reference | Local snapshot | Classification | Primary value to HydraSeat | Direct source reuse? |
|---|---|---|---|---|
| Nucleus Co-op 2.4.2 | `references/NucleusCoop` | GPL-3.0 / copyleft | Multi-instance orchestration, process handoff, input/focus/window compatibility failure modes, cleanup inventory | **No** for HydraSeat core; requirements/tests only |
| SplitScreen.Me handler documentation | `references/splitscreenme-www-master/...` | No visible top-level license in snapshot | Taxonomy of real game-specific compatibility knobs and operator workflows | **No**; documentation/behavior requirements only |
| SplitScreen.Me Hub | `references/splitscreenme-hub-master/...` | No visible top-level license in snapshot | Handler/package metadata, versions, comments, popularity/verification separation | **No**; behavior/data-model research only |
| ProtoInput | `references/ProtoInput` | MIT in local snapshot | Process-local input/focus compatibility architecture | Study only until HydraSeat project license/dependency policy is resolved |
| Universal Split Screen | `references/UniversalSplitScreen` | MIT in local snapshot | Older process-local input/window compatibility patterns and failure modes | Study only until license gate is resolved |
| HidHide | `references/HidHide` | permissive/open-source local snapshot; see upstream license file | Device visibility/control boundary, driver/client contract, explicit system-wide mutation risk | Optional integration research; never proof of physical suppression by itself |
| ViGEmClient | `references/ViGEmClient-master/...` | MIT; project retired | Small opaque-client/target virtual-controller boundary, lifecycle and rumble callback semantics | Study only until license gate; do not select a retired dependency by inertia |
| devreorder | `references/devreorder` | No visible top-level license in captured study | DirectInput enumeration/reordering behavior and stable-identity pitfalls | No source reuse |
| GLFW 3.5.1 | `references/glfw-3.5.1` | zlib/libpng-style upstream license | Controlled external/open-source application profile for Gate C testing | Test reference only; separate from HydraSeat core |
| Duo | `references/Duo` | No visible top-level license in captured study | User-facing multiseat expectations and comparison research | Public behavior/documentation only |

## Retained early technology decisions

The former `PHASE0_RESEARCH.md` duplicated the later research inventory and was removed after its durable decisions were consolidated here:

| Technology | Retained decision | Reason |
|---|---|---|
| Interception | Do not adopt as a default dependency; experimental fallback only | Administrator/driver footprint, old toolchain signals, Windows 11/anti-cheat reports and licensing uncertainty make it unsuitable as the baseline |
| HidHide | Optional guarded candidate, never proof of isolation by itself | Device visibility is system-wide mutation; physical keyboard/mouse, composite-device, recovery and multi-process evidence is mandatory |
| ViGEmBus | Do not adopt for new development | The upstream project is retired; keep a replaceable virtual-controller provider boundary instead |
| Microsoft IddCx/IDD | First-party Windows reference path, not a v1 commitment to ship a display driver | Packaging, signing, recovery, latency and maintenance must be compared before selection; Microsoft's sample is reference code, not a production driver |

These decisions do not claim that Raw Input observation alone suppresses ordinary Windows input. Zero cross-Seat bleed remains a physical Phase 3 gate. Sources retained for the decisions are the public Interception, HidHide and ViGEmBus repositories plus Microsoft's Indirect Display Driver model and IddSample documentation; exact adoption still follows the dependency/license/security gates in the current roadmap.

## Nucleus Co-op: retained detailed analysis

See [`NUCLEUS_COOP_2_4_2_SOURCE_RESEARCH.md`](NUCLEUS_COOP_2_4_2_SOURCE_RESEARCH.md) for the detailed clean-room record, consulted files/hashes and the NCR regression candidates.

The most valuable findings are not implementation patterns to copy; they are compatibility facts that a production multiseat launcher must survive:

1. **Spawn PID is not necessarily the authoritative game PID.** Launchers/loaders may hand off to another process. HydraSeat must track process trees and reacquire the real owned target before window/input/runtime binding.
2. **The first discovered HWND is not necessarily the final game/input HWND.** Windows may be recreated or secondary windows may own Raw Input. Bindings require revalidation.
3. **Input isolation is multi-API.** Raw Input, Win32 polling, XInput, DirectInput, focus/cursor/capture and game-specific paths can coexist; success in one path is not zero-bleed evidence.
4. **Startup-time and post-launch compatibility changes are different capabilities.** A title may require one timing but not the other.
5. **Exit cleanup is a product feature.** Files, environment, windows, processes, network/user changes and input state all need explicit ownership, journals and verified rollback.
6. **A giant arbitrary handler script is flexible but unsafe as a core authority.** HydraSeat should keep typed capabilities, bounded recipes and narrow mutation services instead of importing a general executable handler language.

## SplitScreen.Me handler API: compatibility taxonomy

Key local document:

- `references/splitscreenme-www-master/splitscreenme-www-master/docs/handler-api.mdx`
- observed SHA-256: `b9fe89cb51f8f61076121e3011d597d44b8e69ccbec3ef61dc156af8af431252`

The handler API exposes a large set of knobs grouped around:

- launcher vs final executable identity;
- filesystem copy/symlink/hardlink instance materialization;
- per-instance environment/config/save paths;
- focus/deactivation behavior and startup-vs-post hooks;
- dynamic titles, border/style/position/resize behavior;
- XInput/DirectInput/Raw Input selection and filtering;
- process reacquisition/manual process selection;
- architecture selection;
- per-instance network/user/environment changes;
- cleanup actions and operator prompts.

This is useful as an **edge-case taxonomy**, not as a HydraSeat API design. HydraSeat should translate recurring cases into a small number of typed capabilities such as:

- `LaunchIdentityPolicy` (launcher PID vs authoritative game PID handoff);
- `InstanceMaterializationPolicy` (shared/read-only/copied/per-instance writable roots);
- `EnvironmentIsolationPolicy`;
- `WindowAcquisitionPolicy` and `WindowReacquisitionPolicy`;
- `InputCompatibilityRequirements` split by Raw Input / Win32 polling / XInput / DirectInput;
- `CompatibilityTiming` (`PreSpawn`, `Startup`, `PostWindow`, `Runtime`);
- `OwnedMutation` records with prepare/apply/verify/rollback.

Do **not** reproduce handler capabilities that exist primarily to defeat deliberate single-instance, DRM, anti-cheat, account or security restrictions. Those remain outside HydraSeat's product boundary.

### Immediate regression ideas from the handler taxonomy

- launcher starts successfully but exits while a child game process becomes authoritative;
- authoritative process starts before its final HWND exists;
- HWND is destroyed/recreated after launch and must be reacquired without cross-Seat capture;
- Raw Input target HWND differs from visual/main HWND;
- startup-only compatibility requirement is attempted too late and fails closed;
- runtime-only compatibility requirement does not force invasive startup mutation;
- copied per-instance writable file is changed without modifying the source install;
- interrupted instance materialization leaves prior source/install state untouched;
- unsupported network/user/system-wide mutation remains explicit/high-risk rather than silently executed.

## SplitScreen.Me Hub: metadata lessons

Key local files:

- `README.md` SHA-256 `55b298cf126c2e5ad1026ca79092996aba3324ecf3f5e9690f65e80f21785d43`
- `imports/api/Handlers/Handlers.js` SHA-256 `773a4ff4ea1cf220af33da2d37574fa7215d15f1af9d6f716145917e00079e7e`

The Hub separates handler identity/version/package metadata from user-visible popularity and moderation fields. Its schema includes owner, timestamps, game identity, current version/package, comments/reports, download/stars/trend counters, verification/publication flags and coarse controller/keyboard compatibility flags.

HydraSeat should preserve the useful separation while **not** turning popularity into runtime authority:

- immutable package/version identity is useful;
- moderation/publication state is useful;
- comments/community success evidence is useful;
- download/star/trend counts are discovery signals only;
- runtime requirements must still come from trusted local technical evidence and exact provider/game/version matching;
- community packages require signatures/hashes/schema bounds and explicit capability declarations before they can influence a plan.

## ViGEmClient: virtual-controller boundary lessons

Key local files:

- `README.md` SHA-256 `651dfd81143c6fc67fd66a6d9130a28076064aa36e83830d4bcb758f600b3999`
- `LICENSE` SHA-256 `445b3bccd103d39cab7e9b276dd966d60269cf5c44b4b8a118e48bc4c7c4f0db` (MIT)

Useful architecture lessons:

- expose an opaque client handle rather than leaking driver internals;
- connect to the system service/driver once, then manage explicit target lifetimes;
- allocate/add/update/remove/free are separate lifecycle operations;
- virtual target output (for example rumble) is a reverse-direction event and needs its own ownership/routing path;
- the library explicitly states it is not thread-safe, reinforcing that HydraSeat must define synchronization at the adapter boundary rather than assume third-party thread safety;
- driver/client compatibility detection belongs behind a narrow capability adapter.

The project is retired, so HydraSeat should not adopt it simply because it historically solved virtual-controller creation. The durable requirement is a **replaceable virtual-controller provider interface** with explicit version/capability/trust/cleanup semantics.

### Controller regression requirements retained from Nucleus + ViGEm research

For each Seat mapping, verify the same logical binding across:

- `XInputGetState`;
- `XInputGetStateEx` / ordinal 100 where applicable to the controlled compatibility layer;
- capabilities;
- battery information;
- vibration/rumble output routing;
- disconnect/reconnect generation;
- x86 and x64 adapter/protocol layouts.

A state-only test is insufficient because a wrong capabilities or rumble route is still cross-Seat leakage.

## Research-derived backlog (neutral requirements)

These IDs are research requirements, **not roadmap packet states**. They should be implemented through the appropriate claimed chunk and then folded into existing packet evidence by the control tower.

| ID | Requirement | Likely chunk |
|---|---|---|
| `REF-R01` | Reacquire authoritative game process after launcher/loader handoff while preserving exact ownership | `CHUNK-PROCESS-HANDOFF` |
| `REF-R02` | Reacquire/revalidate recreated HWND and prevent cross-Seat window capture | `CHUNK-WINDOW-REACQUIRE` |
| `REF-R03` | Verify main-window vs Raw-Input-target-window distinction | `CHUNK-WINDOW-REACQUIRE`; input-side changes require a later/control-tower integration note |
| `REF-R04` | Make compatibility timing explicit (startup vs post-window vs runtime) | `CHUNK-COMPAT-RECIPE` |
| `REF-R05` | XInput state/stateEx/capability/battery/rumble all follow the same Seat generation | `CHUNK-CONTROLLER-COMPAT` |
| `REF-R06` | Per-instance writable-root materialization is transactional and source-safe | `CHUNK-COMPAT-RECIPE` |
| `REF-R07` | Build an owned-mutation inventory and prove reverse rollback after partial activation | `CHUNK-TRUST-UPDATE` plus control-tower cross-module audit |
| `REF-R08` | Community popularity/moderation fields never become runtime requirement authority | already covered by current local requirement authority; regression remains control-tower/later-batch work |
| `REF-R09` | Virtual-controller provider lifecycle is replaceable, bounded and cleanup-verifiable | `CHUNK-CONTROLLER-COMPAT` |
| `REF-R10` | Cross-architecture fixed-width protocol/handle tests cover all production adapter boundaries | every current chunk must preserve x64/x86; cross-module audit remains control-tower work |

## What should not be retained as product design

The research library contains useful historical techniques that HydraSeat should deliberately avoid as defaults:

- arbitrary JavaScript/CLR handler execution as trusted runtime authority;
- one giant game-handler/orchestrator object;
- fixed sleeps as lifecycle synchronization;
- enumeration order as stable device identity;
- global cursor/input/network/account mutation without typed rollback ownership;
- undocumented memory writes or broad process manipulation;
- single-instance/DRM/anti-cheat/protection bypass techniques;
- popularity/download counts as proof that a runtime requirement is safe;
- retired third-party drivers/libraries treated as mandatory architecture.

## Reference-tree hygiene

Only source/reference snapshots and intentionally retained research fixtures belong under `C:\HydraSeat\references`. Generated build directories are disposable and should not be treated as evidence. In particular, GLFW build outputs such as `glfw-3.5.1-build-x64` and `glfw-3.5.1-build-prepared-x64` are derived artifacts; the retained research input is `glfw-3.5.1` itself.

When a future worker needs a reference build, create it outside the source snapshot (for example under `C:\HydraSeat\reference-builds\...`) and never add it to HydraSeat's repo or research provenance record as source.
