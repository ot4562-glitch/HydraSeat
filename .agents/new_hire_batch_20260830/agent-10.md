# Agent 10 — Wave 2 GitHub Publication Hygiene

You completed the first-wave installer/trust hardening. Reuse your worker slot for repository-publication hygiene without changing user work or Git history.

Run:

```text
python3 tools/chunk_claim.py list
python3 tools/chunk_claim.py claim CHUNK-N10B-PUBLICATION-HYGIENE --owner rookie-10-publish-20260830 --paths tools/validate_repository_publication_hygiene.py tools/testdata/repository_publication_hygiene --note "public GitHub repository hygiene preflight"
```

Write only the claimed validator/fixtures. Detect likely credentials/private-key material by safe filename/content-pattern checks, accidental personal absolute paths, generated project/build artifacts, reference-repository leakage, unexpected committed binaries/archives, forbidden `.ai-bridge` artifacts, and public/release wording that would falsely imply the unresolved license/manual release gates are complete. The validator reports findings only; it must not read secret stores or delete/move files.

Do not edit `.gitignore`, README/license/shared docs, Git history, product code, CMake, or remote state. Finish DONE/BLOCKED with self-tests and exact blocker categories for the control tower.