# Agent 07 — Wave 2 ABI/Protocol Contract Audit

You completed the first-wave controller/input hardening. Reuse your worker slot for a read-only public-contract audit and a new static validator.

Run:

```text
python3 tools/chunk_claim.py list
python3 tools/chunk_claim.py claim CHUNK-N10B-ABI-CONTRACT --owner rookie-07-abi-20260830 --paths tools/validate_public_abi_contracts.py tools/testdata/public_abi_contracts --note "fixed-width x86/x64 public ABI protocol audit"
```

Write only the claimed validator/fixtures. Read Gate-C/Host/public C ABI headers and focused architecture tests as authority. Detect pointer-size/native-width fields crossing process/public boundaries, missing explicit size/version constraints, unsafe reserved fields, and x86/x64 layout assumptions. Prefer a bounded manifest of intentional public contracts rather than scanning unrelated C++ internals.

Do not edit protocol/controller/Gate-C/runtime production code or tests, CMake/shared docs, or broaden the public API. Finish DONE/BLOCKED with self-tests plus findings requiring another owner.