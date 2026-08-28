# Phase 6 — Game Library, Player Profiles, and Two-Player Setup

## Phase objective

Turn the proven two-Seat MVP into the repeatable user model defined by `docs/PRODUCT_V1.md`:

```text
Game -> Seat 1 / Seat 2 / Both -> Player(s) -> Play
```

Phase 6 separates persisted hardware, people, installed games, and same-game setup knowledge. It makes local installed-game discovery the normal path, manual EXE entry the fallback, and same-game two-instance setup automatic where safely possible with a guided manual path when automation cannot finish.

No packet in this phase may create a credential vault or bypass provider/game restrictions.

## Phase exit gate

Phase 6 closes only when:

1. Seat, Player, Game, TwoPlayerSetup, and RuntimeSession schemas are separate/versioned;
2. existing hardware profiles migrate transactionally;
3. locally installed games can be discovered through at least the declared initial provider set plus manual EXE fallback;
4. a Player can move between Seat 1 and Seat 2 while keeping its game/account-reference preferences;
5. provider credentials remain provider-owned;
6. selecting the same game for both Seats resolves a validated TwoPlayerSetup;
7. HydraSeat can generate a bounded candidate setup automatically where feasible;
8. the guided manual setup path can finish an otherwise unknown lawful title;
9. at least one real same-title/two-instance scenario works where game/provider rules permit it;
10. import/export is typed/redacted and cannot silently gain arbitrary code execution;
11. a Phase-close verification passes.

---

## P6-SCHEMA-01 — Versioned Seat, Player, Game, setup, and session schema family

**State:** READY

**D-051 automated-development rule**

P5-MVP-01 CODE_COMPLETE is sufficient to start the isolated schema/model implementation. P5-CLOSE-01 remains a required real-product validation gate before Phase 6 can be declared validated/closed; it no longer blocks unrelated versioned schema coding.

**Goal**

Define separate persisted/runtime schemas instead of growing one monolithic profile.

**Depends on**

- P5-MVP-01

**Phase-entry validation gate**

- P5-CLOSE-01 before Phase 6 validation/closure claims

**Required schemas/models**

- `SeatConfig` — physical station hardware only;
- `PlayerProfile` — local display identity/preferences/provider account references;
- `GameRecord` — provider/install identity and local metadata;
- `TwoPlayerSetup` — optional same-game/two-instance recipe;
- runtime session selection/binding — temporary Seat + Player + Game assignment;
- compatibility-result reference/provenance fields where needed.

**Invariants**

- v1 active Seat count <= 2;
- runtime PIDs/HWNDs/handles never persist as stable identity;
- Player is independent from Seat;
- Game is independent from Seat/Player;
- provider passwords/tokens/cookies are not schema fields;
- every schema has explicit version/count/string/path bounds;
- unknown future versions fail closed or use an explicitly safe read-only path.

**Done when**

Round-trip, malformed, migration-boundary, Unicode/path, maximum-size, unknown-version, and cross-concept isolation tests pass for the schema family.

---

## P6-MIG-01 — Transactional profile migration and backup

**State:** BLOCKED

**Goal**

Migrate legacy Seat/workspace data into the separated v1 model without destroying the last valid user configuration.

**Depends on**

- P6-SCHEMA-01

**Requirements**

- parse/validate old state read-only first;
- produce a deterministic migration report;
- create transactional backup/temp output;
- commit only after complete validation;
- preserve unmapped data in a bounded diagnostic report rather than guessing;
- rollback to previous valid state on any error.

**Done when**

Legacy fixture profiles migrate deterministically and injected write/validation failures leave the original profile usable byte-for-byte where declared.

---

## P6-CATALOG-01 — Provider-neutral local game catalog

**State:** BLOCKED

**Goal**

Create the normal HydraSeat game library from locally installed/discovered titles rather than forcing users to browse for executables first.

**Depends on**

- P6-SCHEMA-01

**Catalog fields**

- provider + provider app identity where available;
- install root/executable candidates;
- architecture/version/hash/staleness metadata where safely available;
- title and local icon source;
- protection/compatibility metadata reference;
- manual/custom origin.

**Invariants**

- discovery is read-only;
- friendly title/icon is presentation, not stable identity;
- provider metadata is untrusted bounded input;
- duplicate provider/executable records reconcile deterministically;
- missing cover art never blocks a game entry.

**Done when**

A provider-neutral catalog merges deterministic fixtures and local provider discoveries into stable GameRecords with no filesystem mutation.

---

## P6-PROV-01 — Launcher provider adapter contract

**State:** BLOCKED

**Goal**

Define lawful provider-specific discovery/launch/account-reference operations behind a small typed interface.

**Depends on**

- P6-CATALOG-01

**Contract**

Providers may expose only their supported bounded operations such as:

- read-only installed-game discovery;
- executable/launch URI/argument resolution;
- local icon metadata;
- detection of already authenticated account identities where the provider exposes a safe supported selector/reference;
- launch request construction;
- post-launch child/process identification evidence.

**Invariants**

- adapter never bypasses account/license/single-instance/provider policy;
- HydraSeat does not collect provider passwords/tokens;
- undocumented mutation is not silently treated as a normal provider feature;
- provider absence/offline state is explicit;
- provider launch remains part of exact game compatibility evidence.

**Done when**

Fake providers prove deterministic discovery/launch-plan/account-reference behavior with malformed/stale metadata rejection.

---

## P6-PROV-02 — Steam provider adapter

**State:** BLOCKED

**Goal**

Implement the first concrete provider using local Steam installation metadata and normal Steam-supported launch behavior.

**Depends on**

- P6-PROV-01

**Requirements**

- read local library/app metadata without mutation;
- resolve installed app identity/path/executable hints;
- reuse local icon/art where safe/licensed through installed metadata rather than bundling it;
- keep Steam authentication owned by Steam;
- document what account switching/second-instance behavior Steam does or does not expose;
- fail closed when same-title/account/license rules cannot support the requested two-player plan.

**Done when**

Installed Steam fixture/live discovery works read-only and supported launches are reproducible without HydraSeat storing Steam credentials.

---

## P6-PROV-03 — Epic, EA, GOG, and custom provider packets

**State:** BLOCKED

**Goal**

Track secondary provider integrations as separate bounded work rather than one broad unverified launcher abstraction.

**Depends on**

- P6-PROV-01

**Rule**

P6-PROV-03 is complete only when the required v1 subset of its child provider packets is explicitly selected and validated. Non-required providers may remain deferred without blocking release if the published v1 scope says so.

**Done when**

The release-target provider subset is explicitly chosen and each selected child packet has truthful evidence or a documented deferred state.

---

## P6-PROV-03A — Epic launcher adapter

**State:** BLOCKED

**Depends on**

- P6-PROV-01

**Goal**

Read Epic local install metadata and build only supported provider launch requests/account references without handling credentials.

**Done when**

The declared Epic v1 discovery/launch subset passes fixtures/live smoke evidence, or the packet is explicitly deferred from v1 scope.

---

## P6-PROV-03B — EA launcher adapter

**State:** BLOCKED

**Depends on**

- P6-PROV-01

**Goal**

Read EA local install metadata and build only supported provider launch requests/account references without bypassing provider policy.

**Done when**

The declared EA v1 discovery/launch subset passes fixtures/live smoke evidence, or the packet is explicitly deferred from v1 scope.

---

## P6-PROV-03C — GOG launcher adapter

**State:** BLOCKED

**Depends on**

- P6-PROV-01

**Goal**

Read GOG/local installation metadata and construct supported launch paths while preserving offline/core use when practical.

**Done when**

The declared GOG v1 discovery/launch subset passes fixtures/live smoke evidence, or the packet is explicitly deferred from v1 scope.

---

## P6-PROV-03D — Custom executable fallback

**State:** BLOCKED

**Depends on**

- P6-PROV-01

**Goal**

Preserve the power-user `Add game / EXE` path for unsupported providers and unusual installations.

**Requirements**

- explicit executable/path/args/working-directory fields;
- local icon extraction/reference where available;
- executable identity validation;
- no arbitrary shell-string interpolation;
- any future bounded helper/script feature requires a separate typed/trust decision and is not implied by manual EXE support.

**Done when**

A manually added executable becomes a normal GameRecord/launch plan with safe path/argument handling and no implicit arbitrary command runner.

---

## P6-PLAN-01 — Immutable provider-aware game/Seat launch-plan compiler

**State:** BLOCKED

**Goal**

Compile current Game + Seat + Player + provider/setup information into an immutable runtime plan consumed by Phase 5/4 activation contracts.

**Depends on**

- P6-PROV-02
- P6-PROV-03D
- P5-LAUNCH-01

**Plan behavior**

- resolve exactly one or two active Seats;
- resolve selected Game per Seat;
- resolve Player account-reference/instance preference;
- if same Game on both Seats, require a valid TwoPlayerSetup;
- compute selected provider launches and compatibility capabilities;
- include exact requirement-aware hardware preflight;
- hash/correlate the immutable plan.

**Done when**

Equivalent input produces an identical plan/hash and any material stale/missing requirement prevents activation rather than silently changing behavior.

---

## P6-PREFLIGHT-01 — Human-readable requirements, risk, and mutation preview

**State:** BLOCKED

**Goal**

Translate the immutable plan into a normal-user summary first and an Expert technical detail view second.

**Depends on**

- P6-PLAN-01

**Normal UX examples**

- `Seat 2 needs a controller for this game`;
- `This same-game setup needs a separate data directory`;
- `This title is Protected / Experimental`;
- `Audio cannot be separated with the current setup`;
- `Two-player setup needs review`.

Technical backend/device path/plan hash details remain expandable diagnostics.

**Done when**

Every plan-blocking requirement and user-approved mutation/risk has a clear user message and deterministic expert detail without exposing secrets.

---

## P6-PROFILE-01 — Two-player setup validator and editor model

**State:** BLOCKED

**Goal**

Make same-game/two-instance configuration a typed reusable `TwoPlayerSetup` with both automatic-generation and guided-manual edit paths.

**Depends on**

- P6-SCHEMA-01
- P6-PLAN-01

**Setup fields may include**

- exact Game/provider/version match/provenance;
- per-instance data/config directories;
- args/environment/working directories;
- provider account references where supported;
- bounded start order/waits;
- process/window matching;
- input/controller/audio/display requirements;
- known limitations/protection state;
- evidence references.

**Automatic path**

- inspect allowed local metadata read-only;
- generate a candidate setup;
- validate the candidate;
- show intended mutations before applying;
- never silently edit game/provider files outside declared approved paths.

**Manual path**

- expose typed fields and tests;
- validate continuously;
- preserve previous valid setup until Save commits transactionally;
- do not grant arbitrary script execution by default.

**Done when**

Both an automatically generated fixture setup and a manually edited fixture setup compile into the same validated runtime contract, with all invalid/unsafe combinations rejected.

---

## P6-UI-01 — Game library, Player, Seat, and two-player setup UI

**State:** BLOCKED

**Goal**

Implement the normal product flow without exposing internal schema jargon.

**Depends on**

- P6-CATALOG-01
- P6-PROFILE-01
- P6-PREFLIGHT-01

**Primary flow**

```text
Games
 -> choose title
 -> Seat 1 / Seat 2 / Both
 -> choose Player(s)
 -> if same game: resolve/create Two-player setup
 -> preflight only necessary requirements/warnings
 -> Play
```

**Player UI**

- create/rename/remove lightweight local Player;
- optional avatar;
- recent games/Seat preference;
- provider account reference selection where supported;
- never ask HydraSeat to store a provider password.

**Seat settings**

Hardware configuration remains separately accessible and can contain unset items. `Set later` is valid until a selected game actually requires the device.

**Done when**

A non-developer can discover/add a game, create two Players, select both Seats, resolve same/different-game flows, and produce a validated Play plan without editing JSON.

---

## P6-CLI-01 — Expert catalog/setup/plan command-line tools

**State:** BLOCKED

**Goal**

Provide deterministic diagnostic/admin tooling without making CLI the normal product workflow.

**Depends on**

- P6-PLAN-01
- P6-PROFILE-01

**Commands**

Read/list/validate/export catalog, Player metadata, TwoPlayerSetup, and compiled plan in human/JSON forms with redaction.

**Done when**

CLI output round-trips through stable schemas, never exposes credentials, and is sufficient for issue diagnostics/CI fixtures.

---

## P6-IMPORT-01 — Portable import/export, provenance, and redaction

**State:** BLOCKED

**Goal**

Allow users/community to share setup knowledge without sharing local secrets or machine-specific identity blindly.

**Depends on**

- P6-SCHEMA-01
- P6-PROFILE-01

**Requirements**

- versioned package/schema;
- source/provenance metadata;
- no credentials/tokens/cookies;
- no Player display names by default;
- personal absolute paths redacted or represented with typed variables;
- device identities remapped through explicit local selection;
- imported setup is validated before use;
- imported data cannot silently execute arbitrary code/download binaries.

**Done when**

A setup can be exported, privacy-reviewed, imported on another fixture machine, remapped, validated, and compiled without exposing the source machine's private data.

---

## P6-REG-01 — Game/provider/setup regression fixture suite

**State:** BLOCKED

**Goal**

Prevent provider metadata and setup-schema changes from silently breaking known game-library/two-player behavior.

**Depends on**

- P6-IMPORT-01

**Corpus**

- multiple provider fixtures;
- duplicate/moved/uninstalled games;
- Player moves between Seats;
- same-game auto/manual setup fixtures;
- provider offline/missing cases;
- protection metadata;
- malformed/imported package cases;
- Unicode/path edge cases.

**Done when**

The fixture corpus runs deterministically on CI and any intentional behavior/schema change requires an explicit migration/update.

---

## P6-CLOSE-01 — Phase 6 closure

**State:** BLOCKED

**Goal**

Verify the repeatable game-first data/model layer, including one lawful same-title real demonstration.

**Depends on**

- P6-UI-01
- P6-REG-01

**Required acceptance**

- separate Seat/Player/Game/TwoPlayerSetup persistence;
- Player swaps Seats cleanly;
- automatic installed-game discovery plus manual EXE fallback;
- no HydraSeat credential storage;
- same-game setup auto path and guided manual path;
- one real same-title/two-instance run where game/provider rules permit it;
- one Seat instance can exit/change while the other continues under D-042;
- import/export privacy review;
- x64/x86/provider regression tests;
- dedicated Phase-close review.

**Done when**

A user can repeatably launch different or lawfully same games through the Game/Player/Seat model without developer JSON editing and without widening HydraSeat into a provider/DRM bypass tool.