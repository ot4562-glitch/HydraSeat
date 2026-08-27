# HydraSeat Compatibility Evidence Matrix

HydraSeat does not use this file as an official `Certified`/support-badge list. It records only combinations with explicit evidence and the limits of that evidence.

A successful row means only that the named target/version/scenario passed the stated evidence path. It does not automatically imply physical input suppression, every game version, every provider, anti-cheat safety, or universal compatibility.

## Current repository evidence

| Target / profile | Scenario | Architecture | Evidence | State | Explicit limits |
| --- | --- | --- | --- | --- | --- |
| GLFW 3.5.1 upstream `tests/cursor.c`, commit `d9d6f0f1f967807ffade6598ea9a631ebaf37a56`; `glfw-3.5.1-cursor-test` | Two HydraSeat-owned external application processes using the declared controlled shim path | x64 external target; native HydraSeat x64/x86 regressions | Exact code head `12957f0`; fork PR #24 run `33038227992` passed Windows x64/x86, Gate C cross-architecture, and the dedicated pinned real-application job. The acceptance records 4 measured callbacks per Seat, 8 receiver-verified events, 0 cross-pattern callbacks, direct A/B key cross-state separation, forced Job cleanup, unchanged target bytes, zero owned orphans, and native relaunch. | `VALIDATED` | Synthetic routed Seat events are used for the external-app acceptance. This is not P3-HW physical two-keyboard/two-pointing-device proof, not physical suppression/cloaking proof, not x86 GLFW support, not a commercial-game claim, and not protected/anti-cheat/DRM evidence. |

## Interpretation

HydraSeat keeps four evidence layers separate:

1. **Controlled/synthetic evidence** — deterministic HydraSeat-owned probes and state models.
2. **Real-process/open-source application evidence** — an unmodified external application or target through an exact declared compatibility path.
3. **Physical/game evidence** — real assigned hardware and explicitly named game/provider/version scenarios.
4. **Community evidence** — optional redacted reports from users, grouped by materially relevant versions/environment.

The current matrix contains only the first real external open-source application row above. Physical Gate A/B/C acceptance and real game rows are still pending in `docs/implementation/STATUS.md`.

## Planned community presentation

The v1 product direction is to show transparent evidence such as:

```text
Community results
87% succeeded (45 reports)
39 success / 6 failure

Launch              98%
Two instances       91%
Input isolation     89%
Audio routing       96%
Clean shutdown      99%
```

Those percentages will be observations, not guarantees. Materially different game versions, HydraSeat versions, providers, Windows environments, compatibility paths, and protection states must be separated rather than blindly averaged.

`Untested` means no useful evidence exists; it is not the same as a failed test.

Known anti-cheat/DRM/protected-title experiments, if the user explicitly chooses to run them in a future supported test path, remain labeled `Protected / Experimental`. Technical launch success is never anti-cheat safety evidence, and HydraSeat does not bypass or disable protection.