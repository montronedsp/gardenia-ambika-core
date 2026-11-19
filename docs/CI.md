# Continuous integration

Gardenia Ambika Core uses a Windows Server 2022 runner with Visual Studio 2022
to build the shared library and run eight baseline-independent CTest checks in
both Release and Debug. The workflow runs on pull requests, pushes to `main`,
and manual dispatch.

The three bit-exact synthesis/fuzz/ABI golden tests rely on an external
reference corpus that is **not** included in this repository. A green hosted
workflow means the build and the eight independent checks passed; it does
**not** mean the golden suite ran.

## Local qualification

Windows PowerShell:

```powershell
./scripts/ci/qualify-windows.ps1 -Configuration Release
./scripts/ci/qualify-windows.ps1 -Configuration Debug
```

With the genuine external reference corpus, run the complete Release suite:

```powershell
./scripts/ci/qualify-windows.ps1 -Configuration Release -Qualification Full -BaselineOutput 'D:/path/to/baseline-output'
```

Full qualification requires the baseline corpus's `scenarios/` directory and
runs all 11 CTest targets. It never fabricates or regenerates reference values.
Debug goldens are excluded because they have known last-bit differences from
the authoritative Release reference.

CI artifacts are unsigned diagnostic outputs, not published customer releases.
Other operating systems are not claimed to be qualified by this Windows workflow.
