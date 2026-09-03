# Agent 03 — Compatibility Trust + Production Wiring

You are a brand-new HydraSeat worker. Ignore prior worker identities; current repository contents are the baseline. Do not reset/revert unrelated work.

Read mandatory docs plus `provider_launch_plan`, `game_runtime_requirement_resolver`, `compatibility_local_store`, `instance_materialization`, `production_compatibility_activation`, `production_launch_runtime`, and their focused tests. Then run:

```text
python3 tools/chunk_claim.py list
python3 tools/chunk_claim.py claim CHUNK-N10-COMPAT-WIRING --owner rookie-03-compat-20260830 --paths include/hydra/compatibility_local_store.hpp src/compatibility_local_store.cpp include/hydra/game_runtime_requirement_resolver.hpp src/game_runtime_requirement_resolver.cpp include/hydra/instance_materialization.hpp include/hydra/production_compatibility_activation.hpp src/production_compatibility_activation.cpp include/hydra/production_launch_runtime.hpp src/production_launch_runtime.cpp tests/test_compatibility_local_store.cpp tests/test_game_runtime_requirement_resolver.cpp tests/test_production_compatibility_activation.cpp tests/test_production_launch_runtime.cpp --note "trusted local materialization authority and real lifecycle wiring"
```

Close the exact trust gap: imported/community package/setup bytes are never runtime mutation authority. Require a fresh locally trusted setup/materialization decision plus fresh trusted runtime requirement/provider authority, compile the exact typed `InstanceMaterializationPlan`, bind Seat/session/session-generation/Seat-game-generation/game/setup/provider/revisions/fingerprints/source/instance/recipe identity, and attach the existing `ProductionCompatibilityActivation` hook to the real `PlannedSeatGameInstance`. Plans with no materialization remain unchanged. Stale/mismatched/community-only authority must fail before filesystem mutation.

Do not add arbitrary script/shell/PowerShell/CLR/DLL/registry execution or path escape. Do not edit package/community transport, activation bridges, Host transport, UI, CMake, or shared docs. Reuse existing materialization/session/rollback code rather than duplicating it.

Run focused compatibility store/resolver/materialization/activation/production runtime tests. Finish DONE only with negative tests proving community-only data cannot authorize mutation; otherwise BLOCKED with the exact missing local-authority contract.