param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    [switch]$All,
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
$repositoryDir = $PSScriptRoot
$buildDir = Join-Path $repositoryDir 'build-vs18'
$cacheFile = Join-Path $buildDir 'CMakeCache.txt'
if (-not (Test-Path -LiteralPath $cacheFile)) {
    throw 'Configure build-vs18 first, following AGENTS.md.'
}

if (-not $SkipBuild) {
    $cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
    $cmakeExecutable = if ($cmakeCommand) { $cmakeCommand.Source } else { Join-Path $env:ProgramFiles 'CMake/bin/cmake.exe' }
    & $cmakeExecutable --build $buildDir --target common-test --config $Configuration --parallel 6
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

$testExecutable = Join-Path $buildDir "common/test/$Configuration/common-test.exe"
if (-not (Test-Path -LiteralPath $testExecutable)) {
    throw "Test executable not found: $testExecutable"
}

# Use the Qt kit configured for this build, even if another Qt is on PATH.
$qtDirectoryLine = Get-Content -LiteralPath $cacheFile | Where-Object { $_ -match '^Qt6_DIR:[^=]+=' } | Select-Object -First 1
if (-not $qtDirectoryLine) { throw 'Qt6_DIR was not found in the build cache.' }
$qtCmakeDirectory = ($qtDirectoryLine -split '=', 2)[1]
$qtBinDirectory = [System.IO.Path]::GetFullPath((Join-Path $qtCmakeDirectory '../../../bin'))
$originalPath = $env:PATH
$testFilter = if ($All) { '*' } else { '[entity-identity]' }

Push-Location (Split-Path -Parent $testExecutable)
try {
    $env:PATH = $qtBinDirectory + [System.IO.Path]::PathSeparator + $originalPath
    & $testExecutable $testFilter --reporter compact
    $testExitCode = $LASTEXITCODE
}
finally {
    $env:PATH = $originalPath
    Pop-Location
}
exit $testExitCode
