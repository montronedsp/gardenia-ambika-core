#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 MontroneDSP
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD="${ROOT}/build/coverage"
REPORT="${BUILD}/coverage-sonar.xml"
GCOVR_VERSION="${GCOVR_VERSION:-8.3}"

mkdir -p "${BUILD}"

cmake -S "${ROOT}" -B "${BUILD}" -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER=gcc \
  -DCMAKE_CXX_COMPILER=g++ \
  -DCMAKE_C_FLAGS="--coverage -O0 -g" \
  -DCMAKE_CXX_FLAGS="--coverage -O0 -g" \
  -DCMAKE_EXE_LINKER_FLAGS="--coverage" \
  -DCMAKE_SHARED_LINKER_FLAGS="--coverage" \
  -DGARDENIA_WARNINGS_AS_ERRORS=ON

cmake --build "${BUILD}" --parallel 2

ctest --test-dir "${BUILD}" --output-on-failure --no-tests=error \
  -R '^(GardeniaCoreMixerTests|GardeniaCoreVoiceOrderingTests|GardeniaCoreJuceScan|GardeniaCoreCompileSmoke|GardeniaCoreCApiSmokeTest|GardeniaCoreAbiLayoutTests|GardeniaCoreAbiInvalidInputTests)$'

VENV="${BUILD}/.gcovr-venv"
python3 -m venv "${VENV}"
# shellcheck disable=SC1091
source "${VENV}/bin/activate"
python -m pip install --disable-pip-version-check "gcovr==${GCOVR_VERSION}"

gcovr \
  --root "${ROOT}" \
  --filter "${ROOT}/include/" \
  --filter "${ROOT}/src/" \
  --exclude '.*/WaveEditWavetableData\.h$' \
  --exclude-unreachable-branches \
  --exclude-throw-branches \
  --sonarqube "${REPORT}" \
  --print-summary \
  "${BUILD}"

deactivate
test -s "${REPORT}"
echo "coverage_report=${REPORT}"
