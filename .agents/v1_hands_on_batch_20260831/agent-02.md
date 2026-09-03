# Agent 02 — Real Windows installer/bootstrapper

You own `CHUNK-V1H-02-INSTALLER`.

## Start
1. Work only in `C:\HydraSeat\repo`.
2. Read `AGENTS.md`, `.agents/AGENTS.md`, the `CHUNK-V1H-02-INSTALLER` section of `.agents/CHUNKS.md`, `docs/implementation/DECISIONS.md` D-048, `docs/implementation/PHASE8_RELIABILITY_DISTRIBUTION.md` P8-INST-01, `config/release-scope-v1.json` installerPolicy, and only directly related installer/signing files. Do not reread the whole roadmap.
3. Run `python3 tools/chunk_claim.py list`.
4. Claim exactly:

`python3 tools/chunk_claim.py claim CHUNK-V1H-02-INSTALLER --owner v1h-02-installer-20260831 --paths include/hydra/installer_bootstrap.hpp src/installer_bootstrap.cpp src/installer_bootstrap_main.cpp tests/test_installer_bootstrap.cpp tools/install_hydraseat.ps1 tools/build_installer_package.ps1 tools/validate_release_installer_contract.py config/release-signing-manifest.json config/release-artifact-preflight.json tools/testdata/release_artifacts tools/testdata/installer_recovery --note "real double-click Windows installer/bootstrapper"`

## Product requirement
HydraSeat v1 explicitly requires a real Windows installer/repair/uninstaller. The existing `tools/install_hydraseat.ps1` contains substantial transactional ownership/signing/rollback authority, but a user should not need PowerShell knowledge or Visual Studio/CMake to install the product.

## Architecture constraint
Do not throw away the existing installer authority. Implement a thin native Win32 bootstrapper `HydraSeatSetup.exe` that delegates mutations to the reviewed signed PowerShell installer contract.

The bootstrapper should:
- start as normal user;
- inspect package/prerequisite state without elevation where possible;
- clearly present Install / Repair / Uninstall and current installed state;
- show concise destination/version information;
- request UAC only when the user confirms a mutation;
- launch the exact adjacent signed `install_hydraseat.ps1` with a fixed typed operation, not arbitrary shell text;
- quote paths safely and reject missing/reparse/unexpected package layout;
- propagate success/cancel/failure accurately;
- optionally offer Launch HydraSeat after successful install, without requiring elevation for ordinary product use;
- remain offline-first and have no runtime download dependency.

No WiX/NSIS/Inno runtime download and no vendored opaque installer framework. A future packaging technology can replace the thin bootstrapper only by explicit decision.

## Release contract
Update the exact release/signing/preflight allowlists within your claimed files so `HydraSeatSetup.exe` is an explicit reviewed artifact rather than an untracked extra. Keep `install_hydraseat.ps1` in the signed contract because it remains the mutation engine. Extend deterministic fixtures and validator tests for drift/rejection.

`tools/build_installer_package.ps1` should stage only the reviewed release payload from an explicit Release build/package input into a deterministic directory suitable for signing/controlled preflight. It must not recursively copy a build tree or manufacture signatures/provenance.

## Tests
Add pure/bootstrap tests for:
- fixed operation mapping;
- argument/path quoting;
- missing installer script/package;
- reparse/unexpected path rejection where applicable;
- elevation request construction without general command execution;
- Install/Repair/Uninstall/cancel result mapping;
- exact allowlist drift detection;
- package builder rejects missing/unexpected artifacts.

Run `python3 tools/validate_release_installer_contract.py` and the PowerShell syntax validator if your changes permit it. Do not claim clean-machine/UAC/reboot/signing PASS; those remain manual/deployment gates.

Do not edit CMake. Return the exact new target/source integration needed for control tower.

## Finish
Use:

`python3 tools/chunk_claim.py done CHUNK-V1H-02-INSTALLER --owner v1h-02-installer-20260831 --note "<implementation + tests + CMake/signing integration note + manual gates still pending>"`

Use `blocked` if a real architectural prerequisite is missing. No Git/remote actions and no edits outside claimed paths.
