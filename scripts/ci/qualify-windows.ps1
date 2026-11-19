# SPDX-FileCopyrightText: 2026 MontroneDSP
# SPDX-License-Identifier: GPL-3.0-or-later
[CmdletBinding()]
param(
    [ValidateSet('Release', 'Debug')]
    [string] $Configuration = 'Release',

    [ValidateSet('Smoke', 'Full')]
    [string] $Qualification = 'Smoke',

    [string] $BaselineOutput = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Invoke-Native {
    param([string] $Label, [string[]] $Arguments)
    Write-Host "==> $Label"
    & $Arguments[0] @($Arguments | Select-Object -Skip 1)
    if ($LASTEXITCODE -ne 0) {
        throw "$Label failed with exit code $LASTEXITCODE"
    }
}

$root = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$build = Join-Path $root "build/ci-$Configuration"

$golden = @(
    'GardeniaCoreFuzzGoldenTests',
    'GardeniaCoreSynthesisGoldenTests',
    'GardeniaCoreAbiGoldenTests'
)
$smoke = @(
    'GardeniaCoreMixerTests',
    'GardeniaCoreJuceScan',
    'GardeniaCoreCompileSmoke',
    'GardeniaCoreCApiSmokeTest',
    'GardeniaCoreAbiLayoutTests',
    'GardeniaCoreAbiInvalidInputTests',
    'GardeniaCoreDynamicLibrarySmokeTest',
    'GardeniaCoreSymbolAudit'
)

if ($Qualification -eq 'Full') {
    if ($Configuration -ne 'Release') {
        throw 'Bit-exact full golden qualification is supported only in Release. Use Smoke for Debug.'
    }
    if ([string]::IsNullOrWhiteSpace($BaselineOutput)) {
        throw 'Full qualification needs -BaselineOutput pointing at the verified external golden corpus.'
    }
    $BaselineOutput = (Resolve-Path -LiteralPath $BaselineOutput -ErrorAction Stop).Path
    if (-not (Test-Path -LiteralPath (Join-Path $BaselineOutput 'scenarios') -PathType Container)) {
        throw 'Invalid golden corpus: expected a scenarios directory. No reference data will be generated.'
    }
}

$args = @(
    'cmake', '-S', $root, '-B', $build,
    '-G', 'Visual Studio 17 2022', '-A', 'x64'
)
if ($Qualification -eq 'Full') {
    $args += "-DGARDENIA_BASELINE_OUTPUT=$BaselineOutput"
}
else {
    $args += '-DGARDENIA_BASELINE_OUTPUT='
}
Invoke-Native 'Configure Windows x64' $args
Invoke-Native 'Build all targets' @('cmake', '--build', $build, '--config', $Configuration, '--parallel', '2')

$expected = if ($Qualification -eq 'Full') { $smoke + $golden } else { $smoke }
$listing = & ctest --test-dir $build -C $Configuration -N
if ($LASTEXITCODE -ne 0) { throw 'CTest test discovery failed.' }
$discovered = @($listing | ForEach-Object {
    if ($_ -match '^\s*Test\s+#\d+:\s+(\S+)\s*$') { $Matches[1] }
})
$missing = @($expected | Where-Object { $_ -notin $discovered })
if ($missing.Count -gt 0) {
    throw "Required CTest targets missing: $($missing -join ', ')"
}
if ($Qualification -eq 'Full' -and $discovered.Count -ne 11) {
    throw "Expected exactly 11 tests for full qualification, found $($discovered.Count)."
}
if ($Qualification -eq 'Smoke') {
    $regex = '^(' + (($smoke | ForEach-Object { [regex]::Escape($_) }) -join '|') + ')$'
    Invoke-Native 'Run 8 baseline-independent checks' @(
        'ctest', '--test-dir', $build, '-C', $Configuration,
        '--output-on-failure', '--no-tests=error', '-R', $regex
    )
}
else {
    Invoke-Native 'Run all 11 CTest checks with genuine golden references' @(
        'ctest', '--test-dir', $build, '-C', $Configuration,
        '--output-on-failure', '--no-tests=error'
    )
}

$dll = Join-Path $build "$Configuration/gardenia-core.dll"
if (-not (Test-Path -LiteralPath $dll -PathType Leaf)) {
    throw "Missing shared library: $dll"
}
$hash = (Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash
Write-Host "PASS $Configuration $Qualification qualification"
Write-Host "DLL SHA256: $hash"
