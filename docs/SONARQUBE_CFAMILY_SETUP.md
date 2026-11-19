# SonarQube Cloud CFamily setup

Project:
[https://sonarcloud.io/project/overview?id=montronedsp_gardenia-ambika-core](https://sonarcloud.io/project/overview?id=montronedsp_gardenia-ambika-core)

Organization key (verified from the public SonarCloud URL and project id prefix):
`montronedsp`

Project key: `montronedsp_gardenia-ambika-core`

## Analysis mode

Canonical analysis is **CI-based SonarScanner** with a CMake-generated
compilation database. It is not Automatic Analysis.

Before the first authenticated CI scan succeeds as the only analysis path:

1. Open SonarQube Cloud → project → Administration → Analysis Method.
2. **Disable Automatic Analysis** for this project only.
3. Keep analysis method set to CI / GitHub Actions.

Automatic Analysis and the CFamily scanner must not run together for the same
project. Existing GCC/Clang/MSVC workflows stay unchanged; Sonar complements
them and does not replace them.

## Compilation database

On Ubuntu 24.04 (GitHub Actions or a local Linux machine):

```bash
cmake -S . -B build/sonar \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=clang \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DGARDENIA_WARNINGS_AS_ERRORS=ON
cmake --build build/sonar --parallel 2
python3 scripts/ci/validate-compile-commands.py build/sonar/compile_commands.json
```

The scanner must run on the same runner and checkout that produced
`build/sonar/compile_commands.json`. Do not copy a Windows database onto Linux.

Production translation units that instantiate the header-only DSP include
`src/api/gardenia_core_api.cpp` and `src/internal/gardenia_core_dummy.cpp`, plus
the portable test executables that include the DSP headers.

## Scanner configuration

[sonar-project.properties](../sonar-project.properties) sets:

- `sonar.sources=include,src`
- `sonar.tests=tests`
- `sonar.cfamily.compile-commands=build/sonar/compile_commands.json`
- `sonar.coverageReportPaths=build/coverage/coverage-sonar.xml`
- narrow exclusions for `WaveEditWavetableData.h` (coverage and CPD only)

Workflow: [.github/workflows/sonarqube-cfamily.yml](../.github/workflows/sonarqube-cfamily.yml)

Pinned scanner action: `SonarSource/sonarqube-scan-action@59202af…` (v8.5.0).

## Coverage

A separate GCC Debug build under `build/coverage/` is instrumented with
`--coverage`. Portable baseline-independent tests run, then pinned `gcovr`
exports SonarQube Generic Coverage XML:

```bash
bash scripts/ci/run-sonar-coverage.sh
```

Historical audio goldens are not regenerated and are not required for this
coverage path.

## GitHub token setup

1. In SonarQube Cloud, create an analysis token with permission to analyze this
   project.
2. Add it as repository secret `SONAR_TOKEN` on
   `montronedsp/gardenia-ambika-core`.
3. Do not put the token in YAML, properties, logs, or remotes.

Fork pull requests do not receive the secret. The Sonar job is skipped for
forks (`pull_request_target` is not used). Compiler and REUSE workflows still
run.

## Quality gate

Prefer Clean as You Code on new code:

- no new blocker/critical correctness issues
- no new security issues
- new-code duplication within the Sonar default target
- coverage on new code once the coverage import is proven (80% is a reasonable
  later target; do not invent a global 80% gate on historical DSP before the
  baseline is established)

Profile and gate edits require SonarCloud admin access and are documented as
admin steps when the token holder cannot change them from CI.

## Local reproduction

**Linux (canonical Cloud submission):** follow the compilation-database and
coverage commands above, then run the SonarScanner CLI with `SONAR_TOKEN` set
and the same property file.

**Windows workstation:** keep mission builds under
`Z:\MontroneDSP_Studio_Builds`. Visual Studio multi-config generators do not
reliably emit a portable `compile_commands.json` for every configuration. For
local Windows inspection, use Ninja + Clang if available, or SonarSource Build
Wrapper for MSVC when Windows-specific analysis is required. Do not submit a
Windows absolute-path database to the Linux Cloud job.

Never commit `compile_commands.json`, `.gcda`/`.gcno`, `.scannerwork/`, or
coverage XML.

## Troubleshooting

| Symptom | Action |
| --- | --- |
| Compilation database not found | Confirm `build/sonar` was configured with `CMAKE_EXPORT_COMPILE_COMMANDS=ON` on the same job |
| No C/C++ units analyzed | Inspect `validate-compile-commands.py` output and scanner CFamily logs |
| Coverage report missing | Ensure `run-sonar-coverage.sh` wrote a non-empty `coverage-sonar.xml` |
| Automatic analysis conflicts | Disable Automatic Analysis for this project |
| Fork PR skipped | Expected; secrets are withheld from forks |
| Token missing | Configure `SONAR_TOKEN` and re-run |

## Related docs

- [CI.md](CI.md) — Windows/Linux compiler qualification
- [REUSE.toml](../REUSE.toml) — SPDX annotations for new workflow/scripts
