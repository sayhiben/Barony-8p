param(
    [string]$App = "",
    [string]$DataDir = "",
    [ValidateSet("sanity", "release", "full")]
    [string]$Profile = "release",
    [string]$OutDir = "",
    [switch]$FailFast,
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$ExtraArgs
)

$ErrorActionPreference = "Stop"

function Resolve-RepoRoot {
    return Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
}

function Resolve-FirstExistingPath {
    param(
        [string]$RepoRoot,
        [string[]]$Candidates
    )

    foreach ($candidate in $Candidates) {
        $fullPath = Join-Path $RepoRoot $candidate
        if (Test-Path -LiteralPath $fullPath) {
            return (Resolve-Path -LiteralPath $fullPath).Path
        }
    }
    return $null
}

function Resolve-PythonLauncher {
    if (Get-Command py -ErrorAction SilentlyContinue) {
        return @("py", "-3")
    }
    if (Get-Command python -ErrorAction SilentlyContinue) {
        return @("python")
    }
    throw "Neither 'py' nor 'python' was found in PATH."
}

$repoRoot = Resolve-RepoRoot

if (-not $App) {
    $App = Resolve-FirstExistingPath -RepoRoot $repoRoot -Candidates @(
        "build-vs2022-x64-smoke-nosteam\Release\barony.exe",
        "build-vs2022-x64-smoke\Release\barony.exe",
        "build-vs2022-x64\Release\barony.exe",
        "build\Release\barony.exe"
    )
}

if (-not $DataDir) {
    if ($env:BARONY_DATADIR) {
        $DataDir = $env:BARONY_DATADIR
    } else {
        $steamCandidates = @(
            "C:\Program Files (x86)\Steam\steamapps\common\Barony",
            "D:\SteamLibrary\steamapps\common\Barony",
            "E:\SteamLibrary\steamapps\common\Barony"
        )
        foreach ($candidate in $steamCandidates) {
            if (Test-Path -LiteralPath $candidate) {
                $DataDir = $candidate
                break
            }
        }
    }
}

if (-not $App -or -not (Test-Path -LiteralPath $App)) {
    throw "Could not locate a smoke-ready Barony executable. Pass -App explicitly."
}
if (-not $DataDir -or -not (Test-Path -LiteralPath $DataDir)) {
    throw "Could not locate a Barony data directory. Pass -DataDir explicitly or set BARONY_DATADIR."
}

$pythonLauncher = Resolve-PythonLauncher
$pythonExe = $pythonLauncher[0]
$pythonArgs = @()
if ($pythonLauncher.Length -gt 1) {
    $pythonArgs = $pythonLauncher[1..($pythonLauncher.Length - 1)]
}
$runnerArgs = @(
    "tests/smoke/smoke_runner.py",
    "release-suite",
    "--app", $App,
    "--datadir", $DataDir,
    "--profile", $Profile,
    "--platform", "windows"
)

if ($OutDir) {
    $runnerArgs += @("--outdir", $OutDir)
}
if ($FailFast) {
    $runnerArgs += "--fail-fast"
}
if ($ExtraArgs) {
    $runnerArgs += $ExtraArgs
}

Write-Host "Running release smoke suite:"
Write-Host ("  " + (($pythonLauncher + $runnerArgs) -join " "))

Push-Location $repoRoot
try {
    & $pythonExe @pythonArgs @runnerArgs
    exit $LASTEXITCODE
} finally {
    Pop-Location
}
