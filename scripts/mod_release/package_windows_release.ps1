<#
.SYNOPSIS
Packages Windows Steam and NoDRM overlay releases from existing build outputs.

.DESCRIPTION
Stages release folders under release-artifacts by copying barony.exe, editor.exe
(if present), every DLL next to the executable, the packaged mod README and
changelog files from docs/mod_release, and SHA256SUMS.txt. Each staged folder
is then archived as a zip that contains the folder at its root.

.EXAMPLE
powershell -ExecutionPolicy Bypass -File scripts\mod_release\package_windows_release.ps1 `
  -Label v5.0.2-rc1 `
  -SteamBuildDir build-vs2022-x64 `
  -NoDrmBuildDir build-vs2022-x64-nodrm

.EXAMPLE
powershell -ExecutionPolicy Bypass -File scripts\mod_release\package_windows_release.ps1 `
  -SkipNoDrm `
  -SteamBuildDir build-vs2022-x64\Release `
  -Label local-test
#>
[CmdletBinding()]
param(
  [string]$SteamBuildDir = "build-vs2022-x64",
  [string]$NoDrmBuildDir = "build-vs2022-x64-nodrm",
  [string]$OutputRoot = "release-artifacts",
  [string]$Label = (Get-Date -Format "yyyyMMdd-HHmmss"),
  [switch]$SkipSteam,
  [switch]$SkipNoDrm,
  [switch]$RequireEditor,
  [switch]$Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$RepoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$ReadmeSource = Join-Path $RepoRoot "docs\mod_release\README.txt"
$ChangelogSource = Join-Path $RepoRoot "docs\mod_release\mod-changelog.txt"
$DetailedChangelogSource = Join-Path $RepoRoot "docs\mod_release\changelog_v5.0.2.md"

function Resolve-FullPath {
  param(
    [Parameter(Mandatory = $true)][string]$Path
  )

  if ([string]::IsNullOrWhiteSpace($Path)) {
    throw "Path cannot be empty."
  }

  $normalizedPath = $Path -replace "/", "\"
  if ([System.IO.Path]::IsPathRooted($normalizedPath)) {
    return [System.IO.Path]::GetFullPath($normalizedPath)
  }

  return [System.IO.Path]::GetFullPath((Join-Path $RepoRoot $normalizedPath))
}

function Resolve-ExistingPath {
  param(
    [Parameter(Mandatory = $true)][string]$Path,
    [Parameter(Mandatory = $true)][string]$Description
  )

  $fullPath = Resolve-FullPath -Path $Path
  if (-not (Test-Path -LiteralPath $fullPath)) {
    throw "$Description not found: $fullPath"
  }
  return $fullPath
}

function Resolve-ReleaseDir {
  param(
    [Parameter(Mandatory = $true)][string]$Path,
    [Parameter(Mandatory = $true)][string]$Kind
  )

  $candidate = Resolve-ExistingPath -Path $Path -Description "$Kind build path"
  $baronyExe = Join-Path $candidate "barony.exe"
  if (Test-Path -LiteralPath $baronyExe -PathType Leaf) {
    return $candidate
  }

  $releaseDir = Join-Path $candidate "Release"
  $releaseExe = Join-Path $releaseDir "barony.exe"
  if (Test-Path -LiteralPath $releaseExe -PathType Leaf) {
    return $releaseDir
  }

  throw "Could not find barony.exe under '$candidate' or '$releaseDir'."
}

function Remove-DirectoryIfExists {
  param(
    [Parameter(Mandatory = $true)][string]$Path
  )

  if (-not (Test-Path -LiteralPath $Path)) {
    return
  }

  Remove-Item -LiteralPath $Path -Recurse -Force
}

function Assert-PackageLayout {
  param(
    [Parameter(Mandatory = $true)][ValidateSet("steam", "nodrm")][string]$Kind,
    [Parameter(Mandatory = $true)][string]$ReleaseDir
  )

  $steamApiPath = Join-Path $ReleaseDir "steam_api64.dll"
  $hasSteamApi = Test-Path -LiteralPath $steamApiPath -PathType Leaf

  if ($Kind -eq "steam" -and -not $hasSteamApi) {
    throw "Steam package requires steam_api64.dll in $ReleaseDir"
  }

  if ($Kind -eq "nodrm" -and $hasSteamApi) {
    throw "NoDRM package input contains steam_api64.dll: $steamApiPath"
  }
}

function Get-StagedFiles {
  param(
    [Parameter(Mandatory = $true)][string]$ReleaseDir,
    [bool]$IncludeSteamAppId
  )

  $files = New-Object System.Collections.Generic.List[System.IO.FileInfo]

  $baronyExePath = Join-Path $ReleaseDir "barony.exe"
  if (-not (Test-Path -LiteralPath $baronyExePath -PathType Leaf)) {
    throw "Missing required release file: $baronyExePath"
  }
  [void]$files.Add((Get-Item -LiteralPath $baronyExePath))

  $editorExePath = Join-Path $ReleaseDir "editor.exe"
  if (Test-Path -LiteralPath $editorExePath -PathType Leaf) {
    [void]$files.Add((Get-Item -LiteralPath $editorExePath))
  } elseif ($RequireEditor) {
    throw "Missing required release file: $editorExePath"
  }

  $dlls = Get-ChildItem -LiteralPath $ReleaseDir -File -Filter "*.dll" | Sort-Object Name
  if (-not $dlls) {
    throw "No DLLs found in release directory: $ReleaseDir"
  }
  foreach ($dll in $dlls) {
    [void]$files.Add($dll)
  }

  if ($IncludeSteamAppId) {
    $steamAppIdPath = Join-Path $ReleaseDir "steam_appid.txt"
    if (Test-Path -LiteralPath $steamAppIdPath -PathType Leaf) {
      [void]$files.Add((Get-Item -LiteralPath $steamAppIdPath))
    }
  }

  return $files
}

function Write-Sha256Sums {
  param(
    [Parameter(Mandatory = $true)][string]$PackageDir
  )

  $hashLines = foreach ($file in (Get-ChildItem -LiteralPath $PackageDir -File | Where-Object { $_.Name -ne "SHA256SUMS.txt" } | Sort-Object Name)) {
    $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    "{0} *{1}" -f $hash, $file.Name
  }

  $hashPath = Join-Path $PackageDir "SHA256SUMS.txt"
  Set-Content -LiteralPath $hashPath -Value $hashLines -Encoding Ascii
}

function Compress-PackageDir {
  param(
    [Parameter(Mandatory = $true)][string]$PackageDir,
    [Parameter(Mandatory = $true)][string]$ZipPath
  )

  if (Test-Path -LiteralPath $ZipPath) {
    if (-not $Force) {
      throw "Zip already exists. Use -Force to overwrite: $ZipPath"
    }
    Remove-Item -LiteralPath $ZipPath -Force
  }

  Compress-Archive -Path $PackageDir -DestinationPath $ZipPath -Force
}

function New-OverlayPackage {
  param(
    [Parameter(Mandatory = $true)][ValidateSet("steam", "nodrm")][string]$Kind,
    [Parameter(Mandatory = $true)][string]$BuildPath
  )

  $releaseDir = Resolve-ReleaseDir -Path $BuildPath -Kind $Kind
  Assert-PackageLayout -Kind $Kind -ReleaseDir $releaseDir

  $packageName = "barony-8p-windows-{0}-{1}" -f $Kind, $Label
  $packageDir = Join-Path $OutputRoot $packageName
  $zipPath = "{0}.zip" -f $packageDir

  if (Test-Path -LiteralPath $packageDir) {
    if (-not $Force) {
      throw "Package directory already exists. Use -Force to overwrite: $packageDir"
    }
    Remove-DirectoryIfExists -Path $packageDir
  }

  New-Item -ItemType Directory -Force -Path $packageDir | Out-Null

  $stagedFiles = Get-StagedFiles -ReleaseDir $releaseDir -IncludeSteamAppId:($Kind -eq "steam")
  foreach ($file in $stagedFiles) {
    Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $packageDir $file.Name) -Force
  }

  Copy-Item -LiteralPath $ReadmeSource -Destination (Join-Path $packageDir "README.txt") -Force
  Copy-Item -LiteralPath $ChangelogSource -Destination (Join-Path $packageDir "mod-changelog.txt") -Force
  Copy-Item -LiteralPath $DetailedChangelogSource -Destination (Join-Path $packageDir "changelog_v5.0.2.md") -Force
  Write-Sha256Sums -PackageDir $packageDir
  Compress-PackageDir -PackageDir $packageDir -ZipPath $zipPath

  return [PSCustomObject]@{
    Kind = $Kind
    ReleaseDir = $releaseDir
    PackageDir = $packageDir
    ZipPath = $zipPath
  }
}

if (-not (Test-Path -LiteralPath $ReadmeSource -PathType Leaf)) {
  throw "Mod README not found: $ReadmeSource"
}

if (-not (Test-Path -LiteralPath $ChangelogSource -PathType Leaf)) {
  throw "Mod changelog not found: $ChangelogSource"
}

if (-not (Test-Path -LiteralPath $DetailedChangelogSource -PathType Leaf)) {
  throw "Detailed changelog not found: $DetailedChangelogSource"
}

if ($SkipSteam -and $SkipNoDrm) {
  throw "Nothing to package. Remove -SkipSteam or -SkipNoDrm."
}

New-Item -ItemType Directory -Force -Path (Resolve-FullPath -Path $OutputRoot) | Out-Null
$OutputRoot = Resolve-FullPath -Path $OutputRoot

$results = New-Object System.Collections.Generic.List[object]

if (-not $SkipSteam) {
  [void]$results.Add((New-OverlayPackage -Kind "steam" -BuildPath $SteamBuildDir))
}

if (-not $SkipNoDrm) {
  [void]$results.Add((New-OverlayPackage -Kind "nodrm" -BuildPath $NoDrmBuildDir))
}

Write-Host "Packaged Windows releases:"
foreach ($result in $results) {
  Write-Host ("  {0}: {1}" -f $result.Kind, $result.ZipPath)
}
