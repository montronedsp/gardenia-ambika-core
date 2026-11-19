# Strict compiler warning policy

Gardenia Ambika Core requires warning-free builds for **all of its C/C++
library and test targets**, including shared/static ABI code, DSP tests and
Windows DLL/export audits. CMake configures with strict warnings as errors by
default; disabling the policy is deliberately rejected.

| Toolchain | Policy |
|---|---|
| MSVC / compatible Windows compiler frontends | `/W4 /WX` |
| GCC | `-Wall -Wextra -Wpedantic -Werror` |
| Clang | `-Wall -Wextra -Wpedantic -Werror` |

The Windows GitHub Actions job builds both Release and Debug with MSVC.
The Linux warning-policy workflow compiles with GCC and Clang and runs the
baseline-independent tests. Windows PE export and LoadLibrary tests are
excluded on non-Windows platforms because they are inherently Windows-specific.

A green warning-only CI job is **not** a substitute for a DSP golden-parity
check. The external baseline corpus must be supplied for the three golden
tests and must not be regenerated from a candidate under test.

No project-wide diagnostic suppressions, compiler-warning exceptions or
`-Wno-error=...` flags are admitted. Compiler/version differences must be
resolved in source and validated against numerical references before merging.

These are policy settings, not a claim that every compiler configuration has
already passed. Consult GitHub Actions runs and actual build logs for evidence.
