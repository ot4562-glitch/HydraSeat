# HydraSeat Compatibility Matrix

This matrix records only combinations with explicit evidence. A row does not imply physical input suppression, device hiding, anti-cheat support, or compatibility with other versions/builds.

| Profile | Target | Architecture | Controlled path | Evidence | State | Limits |
| --- | --- | --- | --- | --- | --- | --- |
| `glfw-3.5.1-cursor-test` | GLFW 3.5.1 upstream `tests/cursor.c`, commit `d9d6f0f1f967807ffade6598ea9a631ebaf37a56` | x64 | `hydra.controlled-external-shim`, API mask `0x0000b93a`; two owned suspended processes; Gate C adapter + polling/cursor/focus/Raw Input subset; process-local focus lifecycle | Local real-process acceptance: 4 receiver callbacks per Seat, 8 receiver-verified total, 0 cross-pattern callbacks, independent adapter state, forced Job-object cleanup PASS, target bytes unchanged, native relaunch PASS. Exact-head GitHub Windows external-app acceptance is required before `VALIDATED`. | `CODE_COMPLETE` | Synthetic routed Seat events only. No physical Gate A/B/C proof, no physical suppression/cloaking, no x86 GLFW claim, no protected/anti-cheat/DRM targets. |

## Interpretation

`CODE_COMPLETE` means the implementation and local real-process evidence exist but the packet still awaits its declared independent CI revalidation. `VALIDATED` is recorded only after the exact pushed implementation head passes the required Windows x64/x86 regression matrix and the dedicated real open-source-application CI job.
