[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')][string] $Configuration = 'Release',
    [string] $Target = 'game',
    [switch] $KeepGoing
)
$ErrorActionPreference = 'Stop'
$env:PATH = 'C:\msys64\ucrt64\bin;' + $env:PATH

function Invoke-Tool([string] $Tool, [string[]] $Arguments) {
    & $Tool @Arguments
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

$availableMiB = [int] ((Get-CimInstance Win32_OperatingSystem).FreePhysicalMemory / 1024)
$buildJobs = [Math]::Max(1, [Math]::Min([Environment]::ProcessorCount, [Math]::Floor($availableMiB / 1024)))
if ($env:SIMCOASTER_BUILD_JOBS) {
    $overrideJobs = 0
    if (![int]::TryParse($env:SIMCOASTER_BUILD_JOBS, [ref] $overrideJobs) -or $overrideJobs -lt 1) {
        throw 'SIMCOASTER_BUILD_JOBS must be a positive integer.'
    }
    $buildJobs = $overrideJobs
}

$optimized = if ($Configuration -eq 'Release') { 'ON' } else { 'OFF' }
$buildDirectory = Join-Path $PSScriptRoot ".build\$Configuration"
$configure = @('-S', $PSScriptRoot, '-B', $buildDirectory, '-G', 'Ninja',
    '-DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/g++.exe',
    '-DCMAKE_RC_COMPILER=C:/msys64/ucrt64/bin/windres.exe',
    '-DCMAKE_AR=C:/msys64/ucrt64/bin/gcc-ar.exe',
    '-DCMAKE_RANLIB=C:/msys64/ucrt64/bin/gcc-ranlib.exe',
    '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON',
    "-DCMAKE_BUILD_TYPE=$Configuration", "-DSIMCOASTER_GAME_RELEASE=$optimized")
$cacheFile = Join-Path $buildDirectory 'CMakeCache.txt'
$configured = Test-Path -LiteralPath $cacheFile
if ($configured) {
    $cacheValues = @{}
    foreach ($line in [IO.File]::ReadLines($cacheFile)) {
        if ($line -match '^([^#/:][^:]*):[^=]+=(.*)$') { $cacheValues[$Matches[1]] = $Matches[2] }
    }
    $configured = $cacheValues['CMAKE_GENERATOR'] -eq 'Ninja' -and $cacheValues['CMAKE_HOME_DIRECTORY'] -and
        [IO.Path]::GetFullPath($cacheValues['CMAKE_HOME_DIRECTORY']) -ieq [IO.Path]::GetFullPath($PSScriptRoot)
    foreach ($option in $configure) {
        if ($option -match '^-D([^=]+)=(.*)$' -and $cacheValues[$Matches[1]] -ine $Matches[2]) { $configured = $false }
    }
}
if ($configured) {
    Invoke-Tool cmake @('--build', $buildDirectory, '--target', 'build.ninja')
} else {
    Invoke-Tool cmake $configure
}

if ($Target -in @('game', 'all', 'simcoaster_runtime')) { $Target = 'simcoaster_runtime' }
$build = @('--build', $buildDirectory, '--parallel', "$buildJobs", '--target', $Target)
if ($KeepGoing) { $build += @('--', '-k', '0') }
Invoke-Tool cmake $build
if ($Target -eq 'simcoaster_runtime') {
    Write-Host "Built $(Join-Path $PSScriptRoot 'Game\Game.exe')"
}
