# SonarQube Cloud CFamily setup

Project:
[https://sonarcloud.io/project/overview?id=montronedsp_gardenia-ambika-core](https://sonarcloud.io/project/overview?id=montronedsp_gardenia-ambika-core)

Organization key (verified by secretless OIDC exchange on PR analysis):
`montronedsp-1`

Project key: `montronedsp_gardenia-ambika-core`

## Analysis mode

Canonical analysis is **CI-based SonarScanner** with a CMake-generated
compilation database (**Manual Configuration**). It is not Automatic Analysis
and it is not CFamily Automatic Configuration.

Before the first authenticated CI scan succeeds as the only analysis path:

1. Open SonarQube Cloud → project → Administration → Analysis Method.
2. **Disable Automatic Analysis** for this project only.
3. Keep analysis method set to CI / GitHub Actions.

Automatic Analysis and the CFamily scanner must not run together for the same
project. Existing GCC/Clang/MSVC workflows stay unchanged; Sonar complements
them and does not replace them.

## Secretless GitHub Actions authentication (OIDC)

CI authenticates with SonarSource's secretless OIDC exchange. No long-lived
GitHub `SONAR_TOKEN` secret is required for this workflow.

Pinned auth action:
`SonarSource/sonarqube-auth-action@b372e2c2930aadfcc4f4399446da1e12772709ad`
(v1.2).

Pinned scanner action:
`SonarSource/sonarqube-scan-action@59202af334b4ba7b5db01b701b8e79f8b3bef0f4`
(v8.5.0).

Prerequisites (organization / project administrators):

1. Bind GitHub repository `montronedsp/gardenia-ambika-core` to SonarQube
   project `montronedsp_gardenia-ambika-core` through the GitHub integration.
2. Enable **Secretless analysis for GitHub Actions** in the SonarQube Cloud
   organization settings (separate from disabling project Automatic Analysis).
3. Ensure the Sonar job has `permissions: contents: read` and
   `id-token: write`.

The workflow requests a short-lived project-scoped token immediately before the
scanner (`export-env: false`), then passes
`steps.sonar-auth.outputs.sonar-token` only to the scan step together with the
OIDC-resolved organization and project key.

Fork pull requests do not receive `id-token: write`. The Sonar job is skipped
for forks (`pull_request_target` is not used). Compiler and REUSE workflows
still run.

Do not create or commit an analysis token for this pipeline. Do not revert to a
stored `SONAR_TOKEN` unless secretless analysis is organizationally unavailable
and an administrator explicitly decides otherwise.

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
Do not switch to CFamily Automatic Configuration to avoid maintaining the
database.

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

The workflow also passes organization, project key, compile-commands path, and
coverage path as scanner args so the ephemeral OIDC token scope matches the
scan. Keep properties and workflow args aligned; do not invent a second
analysis mode.

## Coverage

A separate GCC Debug build under `build/coverage/` is instrumented with
`--coverage`. Portable baseline-independent tests run, then pinned `gcovr`
exports SonarQube Generic Coverage XML:

```bash
bash scripts/ci/run-sonar-coverage.sh
```

Historical audio goldens are not regenerated and are not required for this
coverage path. Do not invent coverage percentages or exclude DSP sources merely
to improve the Quality Gate.

## Quality gate

Prefer Clean as You Code on new code:

- no new blocker/critical correctness issues
- no new security issues
- new-code duplication within the Sonar default target
- coverage on new code once the coverage import is proven (80% is a reasonable
  later target; do not invent a global 80% gate on historical DSP before the
  baseline is established)

Profile and gate edits require SonarCloud admin access and are documented as
admin steps when the session cannot change them from CI.

## Local reproduction

**Linux (canonical Cloud submission):** follow the compilation-database and
coverage commands above. Local SonarScanner CLI submission still needs a
developer analysis token; GitHub Actions uses OIDC instead and must not depend
on a repository `SONAR_TOKEN` secret.

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
| Fork PR skipped | Expected; OIDC `id-token: write` is withheld from forks |
| `No GitHub OIDC token is available` | Job missing `id-token: write`, or forked PR |
| `Access denied` on auth action | Organization has not enabled Secretless analysis for GitHub Actions |
| Repository not bound | Bind the GitHub repo to the SonarQube project via GitHub integration |
| Project key mismatch | Confirm `montronedsp_gardenia-ambika-core` is bound to this repository |
| HTTP 403 after successful exchange | Organization IP allow-list excludes the runner |

## Verification record (secretless OIDC)

| Field | Value |
| --- | --- |
| Main SHA at OIDC merge | `fe5950dcc72f61ab469137cf2df531957a4df0b8` (PR #8) |
| Docs / org-key follow-up | open PR #9 at tip `7d302a1b91a792913c508f34d51d222c4c95ab3e` |
| Auth action | `SonarSource/sonarqube-auth-action@b372e2c2930aadfcc4f4399446da1e12772709ad` (v1.2) |
| Scanner action | `SonarSource/sonarqube-scan-action@59202af334b4ba7b5db01b701b8e79f8b3bef0f4` (v8.5.0) |
| Scanner CLI (runner) | 8.4.0-build.16527 |
| Runner | `ubuntu-24.04` |
| CMake / Clang | Clang 18.1.3 (CMake Ninja Release with `CMAKE_EXPORT_COMPILE_COMMANDS=ON`) |
| Compilation database | `entries=13`, `unique_translation_units=12`, VALIDATION PASS |
| Portable CTest | 7/7 passed |
| Coverage (gcovr) | lines 49.2% (1010/2052), functions 60.8%, branches 31.2% |
| OIDC exchange | PASS — org `montronedsp-1`, project `montronedsp_gardenia-ambika-core`, 30-minute token |
| CFamily mode | Compile-Commands (Manual Configuration) |
| Main analysis URL | https://sonarcloud.io/dashboard?id=montronedsp_gardenia-ambika-core&branch=main |
| Main workflow run | https://github.com/montronedsp/gardenia-ambika-core/actions/runs/44591096875 |
| PR #8 analysis URL | https://sonarcloud.io/dashboard?id=montronedsp_gardenia-ambika-core&pullRequest=8 |
| PR #9 analysis URL | https://sonarcloud.io/dashboard?id=montronedsp_gardenia-ambika-core&pullRequest=9 |
| Quality Gate | OK on main and on PRs #8 / #9 |
| Main coverage / ncloc | coverage 43.3%, ncloc 21129, bugs 0, vulnerabilities 0, code smells 3 |
| Automatic Analysis | Disabled (`autoscanEnabled=false`; `ciName=Github Actions`) |
| Remaining admin note | No interactive SonarCloud admin browser session was available; settings verified via public SonarCloud APIs after successful CI upload |

### Verdict snapshot

```text
AUTOMATIC ANALYSIS DISABLED: YES
SECRETLESS OIDC ENABLED: YES
GITHUB PROJECT BINDING: PASS
OIDC TOKEN EXCHANGE: PASS
COMPILATION DATABASE: PASS
C++ ANALYSIS: PASS
COVERAGE IMPORT: PASS
MAIN ANALYSIS: PASS
PR ANALYSIS: PASS
QUALITY GATE: PASS
EXISTING CI REGRESSIONS: NONE
```

## Related docs

- [CI.md](CI.md) — Windows/Linux compiler qualification
- [REUSE.toml](../REUSE.toml) — SPDX annotations for new workflow/scripts
- [SonarSource secretless GitHub docs](https://docs.sonarsource.com/sonarqube-cloud/analyzing-source-code/ci-based-analysis/github-actions-for-sonarcloud/secretless-github)
- [sonarqube-auth-action](https://github.com/SonarSource/sonarqube-auth-action)
