# HydraSeat Release Signing Pipeline

P8-SIGN-01 defines a fail-closed Authenticode signing path without placing signing keys, certificates with private keys, passphrases, or provider credentials in the repository.

## Release scope

`config/release-signing-manifest.json` is the only v1 signing allowlist. It currently covers the HydraSeat-owned end-user release set for the frozen **x64 host** package. The separate x86 target-process compatibility path remains a validation/build concern and does not create an x86 HydraSeat host release package:

- main UI;
- authoritative Host;
- Seat UI;
- watchdog;
- emergency reset;
- profile diagnostic CLI;
- community conformance validator;
- the fixed `install_hydraseat.ps1` installer/repair/uninstaller script.

Test executables, labs/probes that are not shipped, third-party binaries, and drivers are excluded. If HydraSeat later selects a driver for distribution, that driver needs a separate driver-signing/trust review rather than inheriting application signing automatically.

The manifest uses explicit artifact kinds. CMake executables name one reviewed target and basename; the sole PowerShell artifact names the fixed repository source `tools/install_hydraseat.ps1` and output basename. There is no recursive/wildcard signing scope. `tools/validate_release_signing_manifest.py` rejects unknown fields/targets/kinds, path traversal, wildcards, duplicate identities/architectures, anything other than exact x64 host coverage, or any unreviewed script source. `tools/validate_release_installer_contract.py` additionally requires the manifest file set and the installer's `$OwnedFiles` allowlist to match exactly.

## Secure build/sign flow

1. Start from an exact reviewed commit SHA, not a moving branch name, and keep the checkout clean through signing.
2. Run the normal x64 host plus required x86 target-process compatibility Release build/tests for that exact commit. The x64 signing build root must be configured from that same checkout; use a fresh/protected build root on the release runner rather than reusing an untrusted developer build tree.
3. The signer revalidates the build root's `CMAKE_HOME_DIRECTORY`, performs one clean-first build of the complete reviewed x64 CMake target set from the clean checkout, then stages only those rebuilt targets plus the exact reviewed installer script.
4. On a protected Windows signing runner, import the code-signing certificate/private key into the current user's certificate store from the organization's secret-management mechanism. The repository must never contain that private material.
5. Run:

```text
powershell -NoProfile -ExecutionPolicy Bypass -File tools/sign_release_artifacts.ps1 \
  -ManifestPath config/release-signing-manifest.json \
  -BuildX64 <x64-build-root> \
  -OutputDirectory <signed-staging-root> \
  -CertificateThumbprint <selected-public-thumbprint> \
  -TimestampUrl <approved-https-rfc3161-service> \
  -ReleaseVersion <release-version> \
  -ReleaseRevision <monotonic-release-revision> \
  -Configuration Release \
  -CommitSha <exact-reviewed-commit>
```

The script:

- accepts only the reviewed repository signing manifest, not a caller-supplied alternate policy file;
- requires the exact clean checked-out commit and `Release` configuration before any signing credentials are used;
- requires the supplied x64 build root to contain one bounded `CMakeCache.txt` whose `CMAKE_HOME_DIRECTORY` resolves to that reviewed checkout;
- uses CMake `--clean-first` once for the complete exact allowlisted target set, preventing stale prior outputs from being accepted as an up-to-date release build, then rechecks that the source checkout is still clean;
- resolves executable sources under the verified x64 build root, the installer-script source under the repository root, and every destination under the staging root;
- refuses unsafe file names, unreviewed artifact kinds/script sources, unsupported architectures, non-AMD64 PE images, and already-signed CMake outputs;
- locates one explicitly selected certificate by public thumbprint;
- requires that certificate to expose a private key in the protected runner store;
- signs fixed CMake executables with SignTool SHA-256/RFC3161 and verifies them with `signtool verify /pa /all`;
- signs the fixed PowerShell installer with `Set-AuthenticodeSignature` using SHA-256/timestamping and places it in the x64 release package;
- independently checks `Get-AuthenticodeSignature` reports `Valid` and the signer thumbprint matches for every release file;
- records artifact kind, architecture, unsigned/signed SHA-256, signer, signature status, release version/revision, and reviewed commit in `signing-provenance.json`, then signs those exact provenance bytes as detached CMS `signing-provenance.json.p7s` with the same publisher certificate and verifies the detached signature before staging completes.

The private key and passphrase are never script parameters and are never written to provenance.

## GitHub Actions deployment

A production repository may connect this script to a manual `workflow_dispatch` job protected by a GitHub Environment such as `release-signing`. That environment should require human approval and expose the PFX/private-key material only to the signing step on an ephemeral Windows runner (or use an organization-managed hardware/cloud signing service).

The exact secret names/provider are intentionally not hard-coded here because no production signing service has been selected and secret material must not be committed merely to make automation appear complete.

Required properties of any eventual workflow:

- checkout by exact commit SHA;
- `permissions: contents: read` unless release publication explicitly needs more;
- protected environment approval before signing credentials are released;
- no secret echo/debug dump/artifact upload of the key container;
- ephemeral key import followed by explicit removal/runner teardown;
- fixed manifest validation before build/sign;
- exact target staging, never recursive wildcard signing;
- upload only signed artifacts plus public provenance;
- release publication is a separate explicit action after signature/hash verification.

## Development builds

Unsigned development builds must remain visibly development builds. P8 artifact trust already distinguishes the narrow explicit development exception from production trusted signing. Do not weaken signature verification because an optional component is missing or unsigned.

## Verification and provenance

Before release publication, verify:

- the signing manifest validator passes;
- the signer/installer integration validator proves the manifest and installer's exact owned-file allowlists match and rejects unsafe deletion/elevation drift, non-Release signing, missing build-root/source binding, skipped or non-clean reviewed-target rebuilds, missing x64 PE validation, and pre-signed build inputs;
- the PowerShell AST syntax validator passes for both signer and installer;
- every expected x64 host package file, including the signed installer script, is present exactly once and no x86 host package is emitted;
- every file has a valid trusted Authenticode signature from the intended publisher identity;
- timestamp verification succeeds;
- `signing-provenance.json` records the reviewed commit, release version/revision, artifact kind/architecture, and signed SHA-256 for every artifact, and `signing-provenance.json.p7s` verifies over those exact bytes with the same publisher identity;
- release notes identify the application version/commit and any deliberately excluded optional component class.

A signed file is still subject to the P8 artifact trust policy. A signature alone does not authorize a new capability, arbitrary helper, driver, or community-supplied executable.

## Current evidence and remaining gate

The fixed manifest, fail-closed manifest/integration validators, PowerShell syntax validation, and executable-plus-installer sign/verify/provenance path are repository-controlled and testable without any private key. The current Windows CTest registration runs all three static/structural release checks. Actual production certificate custody, GitHub Environment/provider integration, timestamp-service selection, and a real signed release-candidate run are deployment/manual evidence. Therefore P8-SIGN-01 remains `CODE_COMPLETE`, not `VALIDATED`, from this repository-only implementation.
