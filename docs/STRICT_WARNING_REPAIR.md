# Strict warning repair (cross-platform)

## Original failing commit

`1ee11cf714a8d42b51d615028d68831df478cbab` — merge of PR #3 (`ci/warnings-as-errors`).

Failing hosted runs:

- Windows Release/Debug: https://github.com/montronedsp/gardenia-ambika-core/actions/runs/44442981871
- Linux GCC/Clang: https://github.com/montronedsp/gardenia-ambika-core/actions/runs/44442981921

`ci/warnings-as-errors` tip later held `925a571` (AbiLayout `static_assert`) that was not part of the merged PR tip; that change is included here. The RAII `[[maybe_unused]]` fix in `gardenia_core_api.cpp` (`50b717d`) was already on `main` but the test harness still needed the same attribute.

## Diagnostics and root causes

| Diagnostic | Site | Root cause |
|---|---|---|
| MSVC C4127 / C2220 | `tests/AbiLayoutTests.cpp:41` | Runtime `if` on compile-time `sizeof` invariants |
| GCC/Clang `-Wunused-variable` | `tests/HostReplayAdapter.h:564` | RAII `ScopedNoDenormals` is trivial outside MSVC |
| GCC `-Warray-bounds` | `std::sort` via `AbiGoldenTests.cpp` / `HostReplayAdapter.h` steal path | libstdc++ introsort false positive when live voice count equals fixed capacity 8 |

## Corrective changes

1. **AbiLayoutTests** — replace the constant `if` with `static_assert` on the same size floors; keep runtime size printing and ABI JSON emission.
2. **HostReplayAdapter** — mark `ScopedNoDenormals noDenormals` with `[[maybe_unused]]` so the guard object still lives for the render scope on MSVC (MXCSR FTZ/DAZ) without unused-variable errors elsewhere.
3. **Voice steal ordering** — replace `std::sort` on the fixed 8-index array with `stable_sort_indices_n` (bounded stable insertion sort). Production paths use unique `noteOnTime` counters, so ordering vs historical `std::sort` is unchanged for golden scenarios; equal timestamps become deterministic (stable by fill index).
4. **VoiceOrderingTests** — directed checks for zero/one/eight voices, repeated notes, equal timestamps, oldest-note priority, sustain-held, released-before-held, capacity steal, and deterministic event order, against an independent copy of the ABI steal rules.

Strict `/W4 /WX` and `-Wall -Wextra -Wpedantic -Werror` policies are unchanged.

## Behavior preservation

- No DSP, wavetable, fuzz, ABI layout, parameter ID, or export-symbol changes.
- Release goldens (external corpus): Synthesis **994**, Fuzz **65**, ABI **1059**, exports **15**.
- Steal priority rules (protect low/high notes, prefer same note, released, then oldest unprotected) unchanged; only the sort implementation changed.

## Local compilers tested

| Toolchain | Result |
|---|---|
| MSVC 19.38.33145 x64 Release | Build clean; CTest 12/12 |
| MSVC 19.38.33145 x64 Debug | Build clean; smoke 9/9 |
| Clang 22.1.0 (Windows, Ninja Release) | Build clean; portable smoke 7/7 |
| `python -m reuse lint` | Compliant (50/50) |

Before (hosted failing tip): C4127, unused-variable, and array-bounds errors under `-Werror`/`/WX`.  
After (local): **0** C4127 / unused-variable / array-bounds hits in MSVC and Clang build logs.

GCC 13 is covered by the Linux Actions workflow (not installed on the Windows development host).

## Publication

- Branch: `fix/strict-warnings-cross-platform`
- Pull request and final `main` SHA / hosted run URLs are recorded in the PR merge and the mission final report after checks complete.
