# Clean-Room and Third-Party Source Policy

## Purpose

HydraSeat studies commercial products, open-source tools, Windows documentation and observable application behavior. This policy keeps that research legally and technically separated from HydraSeat's implementation.

It applies to human contributors, AI coding agents, generated patches, copied snippets, vendored libraries and optional external integrations.

## 1. Source classes

Every external source must be classified before it influences a patch.

### A. Official platform documentation and SDK samples

Examples: Microsoft Learn, Windows SDK headers, official Windows driver samples.

Allowed use:

- implement documented APIs independently;
- adapt small sample patterns subject to the sample's license;
- cite the exact documentation/sample and retain required notices.

### B. Permissive open source

Examples currently studied: ProtoInput, Universal Split Screen and HidHide, whose top-level repositories carry MIT licenses.

Allowed use only after all of the following:

1. HydraSeat has an explicit tracked project license.
2. The exact upstream commit is recorded.
3. The component and all transitive/bundled dependencies are audited.
4. Copyright and license notices are preserved.
5. Imported or adapted code is isolated and clearly identified.
6. A `THIRD_PARTY_NOTICES` entry describes the source and modifications.
7. Security, maintenance and architecture review is complete.

Until those conditions are met, permissive source may be studied for architecture but not copied.

### C. Copyleft open source

Example: Nucleus Co-op under GPL-3.0.

Default rule:

- do not copy implementation code into the HydraSeat core;
- do not translate GPL source line-by-line into another language;
- use public behavior, concepts, interfaces and independently written tests as requirements;
- integration must occur through a legally reviewed external-process or data boundary, or the entire affected component must deliberately adopt a compatible license.

### D. Unlicensed public repositories

Examples found during this study: the cloned devreorder and Duo repositories did not contain a visible top-level license file.

Rule:

- public visibility is not permission to copy, modify or redistribute;
- use only public documentation and ordinary behavior as requirements;
- do not copy source or binaries into HydraSeat;
- do not imply endorsement or compatibility.

### E. Proprietary software

Example: ASTER.

Rule:

- no decompilation, disassembly, memory extraction, protection bypass or private-source acquisition;
- no use of leaked/non-public material;
- no line-by-line recreation from binary output;
- public documentation and ordinary black-box behavior may define user-facing requirements;
- implementation must be independently designed from official Windows APIs and legitimately licensed sources.

## 2. Required evidence record

Any patch based on external research must state, in the commit/PR or a design document:

- product/repository name;
- source URL and exact commit/version where available;
- license classification;
- files or documentation sections consulted;
- whether any code was copied, adapted, wrapped or only behaviorally studied;
- required attribution/notices;
- differences in HydraSeat's implementation;
- tests proving independent behavior.

## 3. AI-agent rules

Prompts to an AI coding agent must identify source boundaries explicitly.

Required language for mixed-license research tasks:

- reference repositories are read-only;
- proprietary and unlicensed sources are behavior/documentation references only;
- GPL source must not be copied into a non-GPL core;
- permissive source must not be copied until project and dependency licenses are resolved;
- the agent must produce an independent design and cite consulted source areas;
- the agent must not modify reference checkouts.

AI-generated code is not presumed clean-room merely because an AI wrote it. Reviewers must compare architecture, names, comments and unusual constants against the research sources and reject suspiciously close reproduction.

## 4. Clean-room workflow

For incompatible, unclear or proprietary sources:

1. **Researcher role** documents externally observable behavior, API requirements, failure modes and test cases without writing implementation code.
2. **Specification** converts findings into neutral capability requirements and black-box tests.
3. **Implementer role** works from that specification plus official platform documentation, without consulting prohibited implementation source while writing the component.
4. **Reviewer role** checks provenance, naming, structure and tests.
5. **Evidence** is retained in design documents and PR discussion.

For this small project, roles may be performed at different times by the same person or agent, but the written specification must exist before implementation and prohibited source must not be copied.

## 5. Dependency and binary policy

- Do not silently download or execute third-party binaries.
- Optional adapters must require an explicit configured path and version/hash verification.
- Do not redistribute an external binary unless its license permits redistribution and notices are included.
- Injection DLLs, drivers and wrappers must be isolated from HydraSeat core and independently disableable.
- Driver installation or system-wide DLL replacement is never an automatic default.
- Reference repositories under `C:\HydraSeat\references` are not build inputs and must never be committed accidentally.

## 6. Security boundaries

Research does not authorize:

- anti-cheat or DRM bypass;
- stealth injection intended to evade detection;
- disabling security software;
- using kernel drivers with unclear provenance;
- modifying system DLLs;
- hiding processes/modules from security products;
- extracting credentials or unrelated process data.

HydraSeat must default to unsupported for protected processes when a documented non-invasive path cannot provide required isolation.

## 7. Current project-license blocker

The repository README advertises MIT, but the repository currently lacks a tracked `LICENSE` file. Before any third-party code is imported, maintainers must:

1. confirm ownership/permission for the existing HydraSeat code;
2. add an explicit license file or document the chosen licensing model;
3. determine whether contributions can be accepted under that license;
4. add third-party notice infrastructure.

Until then, Phase 3 implementation should use independent code written against official Windows APIs and abstract optional integrations behind clean interfaces.

## 8. Review checklist

A reviewer must answer yes to all applicable questions:

- Is the source and license identified?
- Is direct reuse permitted?
- Are transitive dependencies audited?
- Are required notices present?
- Is GPL/unlicensed/proprietary implementation absent from the core?
- Is the design independently expressed in HydraSeat terminology?
- Are risky integration paths optional and user-approved?
- Are tests based on public behavior rather than copied internals?
- Can the feature be removed/disabled without damaging the host system?
- Does the PR avoid claiming support that was not measured?

Failure on any item blocks merge until resolved.
