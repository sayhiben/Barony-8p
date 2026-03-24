[CmdletBinding()]
param(
  [string]$BaseInstallDir = "",
  [string]$OutputRoot = "tests/smoke/artifacts",
  [string]$Label = (Get-Date -Format "yyyyMMdd-HHmmss"),
  [string[]]$ZipPaths = @(),
  [int]$LaunchSeconds = 20,
  [int]$InputIdleSeconds = 15
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$RepoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))

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

function Resolve-FirstExistingPath {
  param(
    [Parameter(Mandatory = $true)][string[]]$Candidates,
    [Parameter(Mandatory = $true)][string]$Description
  )

  foreach ($candidate in $Candidates) {
    if ([string]::IsNullOrWhiteSpace($candidate)) {
      continue
    }
    if (Test-Path -LiteralPath $candidate) {
      return [System.IO.Path]::GetFullPath($candidate)
    }
  }

  throw "Could not locate $Description."
}

function Resolve-BaseInstallDir {
  param(
    [string]$Path
  )

  if ($Path) {
    $fullPath = Resolve-FullPath -Path $Path
    if (-not (Test-Path -LiteralPath $fullPath)) {
      throw "Base install path not found: $fullPath"
    }
    return $fullPath
  }

  return Resolve-FirstExistingPath -Description "a Windows Barony install" -Candidates @(
    "C:\Program Files (x86)\Steam\steamapps\common\Barony",
    "D:\SteamLibrary\steamapps\common\Barony",
    "E:\SteamLibrary\steamapps\common\Barony"
  )
}

function Resolve-PackagePaths {
  param(
    [string[]]$RequestedZipPaths
  )

  if ($RequestedZipPaths -and $RequestedZipPaths.Count -gt 0) {
    return @($RequestedZipPaths | ForEach-Object { Resolve-FullPath -Path $_ })
  }

  $releaseDir = Resolve-FullPath -Path "release-artifacts"
  $resolved = New-Object System.Collections.Generic.List[string]
  foreach ($kind in @("steam", "nodrm")) {
    $match = Get-ChildItem -LiteralPath $releaseDir -File -Filter ("barony-8p-windows-{0}-*.zip" -f $kind) |
      Sort-Object LastWriteTime -Descending |
      Select-Object -First 1
    if (-not $match) {
      throw "Could not locate latest $kind package zip under $releaseDir"
    }
    [void]$resolved.Add($match.FullName)
  }
  return $resolved.ToArray()
}

function Ensure-Directory {
  param(
    [Parameter(Mandatory = $true)][string]$Path
  )

  New-Item -ItemType Directory -Force -Path $Path | Out-Null
}

function Write-SummaryEnv {
  param(
    [Parameter(Mandatory = $true)][string]$Path,
    [Parameter(Mandatory = $true)][hashtable]$Values
  )

  $lines = foreach ($entry in $Values.GetEnumerator() | Sort-Object Name) {
    "{0}={1}" -f $entry.Key, [string]$entry.Value
  }
  Set-Content -LiteralPath $Path -Value $lines -Encoding Ascii
}

function Get-PackageKind {
  param(
    [Parameter(Mandatory = $true)][string]$ZipPath
  )

  $name = [System.IO.Path]::GetFileName($ZipPath).ToLowerInvariant()
  if ($name.Contains("-windows-steam-")) {
    return "steam"
  }
  if ($name.Contains("-windows-nodrm-")) {
    return "nodrm"
  }
  throw "Could not infer package kind from zip path: $ZipPath"
}

function Read-ShaManifest {
  param(
    [Parameter(Mandatory = $true)][string]$ManifestPath
  )

  $entries = New-Object System.Collections.Generic.List[object]
  $lineNo = 0
  foreach ($line in Get-Content -LiteralPath $ManifestPath) {
    $lineNo += 1
    if ([string]::IsNullOrWhiteSpace($line)) {
      continue
    }
    if ($line -notmatch '^([0-9a-f]{64}) \*(.+)$') {
      throw "Malformed SHA256SUMS entry at ${ManifestPath}:$lineNo"
    }
    [void]$entries.Add([PSCustomObject]@{
      Hash = $matches[1]
      Name = $matches[2]
    })
  }
  return $entries
}

function Test-ManifestAgainstDir {
  param(
    [Parameter(Mandatory = $true)]$ManifestEntries,
    [Parameter(Mandatory = $true)][string]$TargetDir,
    [Parameter(Mandatory = $true)][string]$ReportPath
  )

  $report = New-Object System.Collections.Generic.List[string]
  $mismatchCount = 0
  foreach ($entry in $ManifestEntries) {
    $candidate = Join-Path $TargetDir $entry.Name
    if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) {
      [void]$report.Add(("missing {0}" -f $entry.Name))
      $mismatchCount += 1
      continue
    }

    $actualHash = (Get-FileHash -LiteralPath $candidate -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actualHash -ne $entry.Hash) {
      [void]$report.Add(("hash mismatch {0} expected={1} actual={2}" -f $entry.Name, $entry.Hash, $actualHash))
      $mismatchCount += 1
      continue
    }

    [void]$report.Add(("ok {0}" -f $entry.Name))
  }

  Set-Content -LiteralPath $ReportPath -Value $report -Encoding Ascii
  return [PSCustomObject]@{
    Passed = ($mismatchCount -eq 0)
    Count = $ManifestEntries.Count
    MismatchCount = $mismatchCount
    ReportPath = $ReportPath
  }
}

function Invoke-Robocopy {
  param(
    [Parameter(Mandatory = $true)][string]$SourceDir,
    [Parameter(Mandatory = $true)][string]$DestDir,
    [Parameter(Mandatory = $true)][string]$StdoutPath,
    [Parameter(Mandatory = $true)][string]$StderrPath
  )

  Ensure-Directory -Path $DestDir
  $args = @(
    $SourceDir,
    $DestDir,
    "/E",
    "/R:2",
    "/W:1",
    "/NP",
    "/NFL",
    "/NDL",
    "/NJH",
    "/NJS",
    "/XD",
    "crashlogs",
    "logfiles",
    "mods",
    "savegames",
    "scores",
    "workshop_cache",
    "/XF",
    "log.txt",
    "models.cache",
    "scores.dat",
    "scores_multiplayer.dat"
  )

  $process = Start-Process -FilePath "robocopy.exe" `
    -ArgumentList $args `
    -PassThru `
    -Wait `
    -NoNewWindow `
    -RedirectStandardOutput $StdoutPath `
    -RedirectStandardError $StderrPath

  if ($process.ExitCode -gt 7) {
    throw "robocopy failed with exit code $($process.ExitCode). See $StdoutPath"
  }

  return $process.ExitCode
}

function Sanitize-InstallCopy {
  param(
    [Parameter(Mandatory = $true)][string]$InstallDir,
    [Parameter(Mandatory = $true)][ValidateSet("steam", "nodrm")][string]$Kind
  )

  foreach ($name in @("log.txt", "models.cache")) {
    $candidate = Join-Path $InstallDir $name
    if (Test-Path -LiteralPath $candidate) {
      Remove-Item -LiteralPath $candidate -Force
    }
  }

  if ($Kind -eq "nodrm") {
    foreach ($name in @("steam_api64.dll", "steam_appid.txt", "steam_autocloud.vdf")) {
      $candidate = Join-Path $InstallDir $name
      if (Test-Path -LiteralPath $candidate) {
        Remove-Item -LiteralPath $candidate -Force
      }
    }
  }

  $configPath = Join-Path $InstallDir "config\config.json"
  if (Test-Path -LiteralPath $configPath -PathType Leaf) {
    $config = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
    $config.mods = @()
    $config.skipintro = $true
    $config.use_model_cache = $false
    if ($null -ne $config.video) {
      $config.video.window_mode = 0
      $config.video.resolution_x = 1280
      $config.video.resolution_y = 720
    }
    $config | ConvertTo-Json -Depth 64 | Set-Content -LiteralPath $configPath -Encoding Ascii
  }
}

function Expand-PackageZip {
  param(
    [Parameter(Mandatory = $true)][string]$ZipPath,
    [Parameter(Mandatory = $true)][string]$ExtractRoot
  )

  Ensure-Directory -Path $ExtractRoot
  Expand-Archive -LiteralPath $ZipPath -DestinationPath $ExtractRoot -Force
  $dirs = @(Get-ChildItem -LiteralPath $ExtractRoot -Directory)
  if ($dirs.Count -ne 1) {
    throw "Expected exactly one package root directory in $ExtractRoot"
  }
  return $dirs[0].FullName
}

function Copy-PackageIntoInstall {
  param(
    [Parameter(Mandatory = $true)][string]$PackageDir,
    [Parameter(Mandatory = $true)][string]$InstallDir
  )

  foreach ($file in Get-ChildItem -LiteralPath $PackageDir -File) {
    Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $InstallDir $file.Name) -Force
  }
}

function Invoke-LaunchValidation {
  param(
    [Parameter(Mandatory = $true)][string]$InstallDir,
    [Parameter(Mandatory = $true)][int]$LaunchDurationSeconds,
    [Parameter(Mandatory = $true)][int]$InputIdleDurationSeconds,
    [Parameter(Mandatory = $true)][string]$TailPath
  )

  $exePath = Join-Path $InstallDir "barony.exe"
  $logPath = Join-Path $InstallDir "log.txt"
  if (Test-Path -LiteralPath $logPath) {
    Remove-Item -LiteralPath $logPath -Force
  }

  $process = Start-Process -FilePath $exePath `
    -WorkingDirectory $InstallDir `
    -ArgumentList @("-windowed", "-size=1280x720", "-nosound") `
    -PassThru

  $waitForInputIdle = $false
  $waitForInputIdleError = ""
  try {
    $waitForInputIdle = $process.WaitForInputIdle($InputIdleDurationSeconds * 1000)
  } catch {
    $waitForInputIdleError = $_.Exception.Message
  }

  $mainWindowSeen = $false
  $survivedLaunchWindow = $true
  $exitCode = ""
  $deadline = (Get-Date).AddSeconds($LaunchDurationSeconds)
  while ((Get-Date) -lt $deadline) {
    Start-Sleep -Seconds 1
    $process.Refresh()
    if ($process.HasExited) {
      $survivedLaunchWindow = $false
      $exitCode = [string]$process.ExitCode
      break
    }
    if ($process.MainWindowHandle -ne 0) {
      $mainWindowSeen = $true
    }
  }

  $killedAfterValidation = $false
  if (-not $process.HasExited) {
    Stop-Process -Id $process.Id -Force
    $killedAfterValidation = $true
    Wait-Process -Id $process.Id -Timeout 15 -ErrorAction SilentlyContinue
  }

  $logExists = Test-Path -LiteralPath $logPath -PathType Leaf
  $logLength = 0
  $mainMenuLoaded = $false
  $logTail = @()
  if ($logExists) {
    $logItem = Get-Item -LiteralPath $logPath
    $logLength = [int64]$logItem.Length
    $mainMenuLoaded = Select-String -Path $logPath -Pattern 'LoadMap .*maps[\\/]+mainmenu3\.lmp' -Quiet
    $logTail = @(Get-Content -LiteralPath $logPath -Tail 200)
  }
  Set-Content -LiteralPath $TailPath -Value $logTail -Encoding Ascii

  $passed = $logExists -and $survivedLaunchWindow -and ($mainMenuLoaded -or $waitForInputIdle -or $mainWindowSeen)

  return [PSCustomObject]@{
    Passed = $passed
    ExePath = $exePath
    LogPath = $logPath
    LogExists = $logExists
    LogLength = $logLength
    MainMenuLoaded = $mainMenuLoaded
    WaitForInputIdle = $waitForInputIdle
    WaitForInputIdleError = $waitForInputIdleError
    MainWindowSeen = $mainWindowSeen
    SurvivedLaunchWindow = $survivedLaunchWindow
    ExitCode = $exitCode
    KilledAfterValidation = $killedAfterValidation
    TailPath = $TailPath
  }
}

$baseInstallDir = Resolve-BaseInstallDir -Path $BaseInstallDir
$packagePaths = Resolve-PackagePaths -RequestedZipPaths $ZipPaths
$artifactRoot = Resolve-FullPath -Path (Join-Path $OutputRoot ("windows-install-validation-{0}" -f $Label))
Ensure-Directory -Path $artifactRoot

$results = New-Object System.Collections.Generic.List[object]
$baseInstallKind = if (Test-Path -LiteralPath (Join-Path $baseInstallDir "steam_api64.dll")) { "steam" } else { "generic" }

foreach ($packagePath in $packagePaths) {
  $packagePath = [System.IO.Path]::GetFullPath($packagePath)
  $kind = Get-PackageKind -ZipPath $packagePath
  $targetRoot = Join-Path $artifactRoot $kind
  $extractRoot = Join-Path $targetRoot "extracted"
  $installRoot = Join-Path $targetRoot "install"
  Ensure-Directory -Path $targetRoot

  $copyStdout = Join-Path $targetRoot "copy.stdout.log"
  $copyStderr = Join-Path $targetRoot "copy.stderr.log"
  $packageManifestReport = Join-Path $targetRoot "package_manifest_check.txt"
  $installManifestReport = Join-Path $targetRoot "install_manifest_check.txt"
  $launchTailPath = Join-Path $targetRoot "launch_log_tail.txt"
  $status = "pass"
  $errorMessage = ""
  $launchResult = $null
  $notes = ""

  try {
    Invoke-Robocopy -SourceDir $baseInstallDir -DestDir $installRoot -StdoutPath $copyStdout -StderrPath $copyStderr | Out-Null
    Sanitize-InstallCopy -InstallDir $installRoot -Kind $kind

    $packageDir = Expand-PackageZip -ZipPath $packagePath -ExtractRoot $extractRoot
    $manifestPath = Join-Path $packageDir "SHA256SUMS.txt"
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
      throw "Package manifest not found: $manifestPath"
    }
    $manifestEntries = Read-ShaManifest -ManifestPath $manifestPath

    $packageManifestResult = Test-ManifestAgainstDir -ManifestEntries $manifestEntries -TargetDir $packageDir -ReportPath $packageManifestReport
    if (-not $packageManifestResult.Passed) {
      throw "Package manifest verification failed for $packagePath"
    }

    Copy-PackageIntoInstall -PackageDir $packageDir -InstallDir $installRoot

    $installManifestResult = Test-ManifestAgainstDir -ManifestEntries $manifestEntries -TargetDir $installRoot -ReportPath $installManifestReport
    if (-not $installManifestResult.Passed) {
      throw "Installed overlay manifest verification failed for $packagePath"
    }

    $launchResult = Invoke-LaunchValidation -InstallDir $installRoot -LaunchDurationSeconds $LaunchSeconds -InputIdleDurationSeconds $InputIdleSeconds -TailPath $launchTailPath
    if (-not $launchResult.Passed) {
      throw "Startup validation failed for $packagePath"
    }

    $notes = if ($kind -eq "nodrm" -and $baseInstallKind -eq "steam") {
      "Validated against a sanitized copy of the local Steam install with steam-only root files removed before overlay."
    } else {
      ""
    }

    $result = [PSCustomObject]@{
      Kind = $kind
      Status = "pass"
      PackagePath = $packagePath
      PackageDir = $packageDir
      InstallDir = $installRoot
      PackageManifestReport = $packageManifestReport
      InstallManifestReport = $installManifestReport
      LaunchTailPath = $launchTailPath
      LaunchLogPath = $launchResult.LogPath
      MainMenuLoaded = $launchResult.MainMenuLoaded
      WaitForInputIdle = $launchResult.WaitForInputIdle
      MainWindowSeen = $launchResult.MainWindowSeen
      SurvivedLaunchWindow = $launchResult.SurvivedLaunchWindow
      BaseInstallKind = $baseInstallKind
      BaseInstallDir = $baseInstallDir
      Notes = $notes
    }
    [void]$results.Add($result)
  } catch {
    $status = "fail"
    $errorMessage = $_.Exception.Message
    $notes = $errorMessage
    $result = [PSCustomObject]@{
      Kind = $kind
      Status = $status
      PackagePath = $packagePath
      PackageDir = if (Test-Path -LiteralPath $extractRoot) { $extractRoot } else { "" }
      InstallDir = $installRoot
      PackageManifestReport = $packageManifestReport
      InstallManifestReport = $installManifestReport
      LaunchTailPath = $launchTailPath
      LaunchLogPath = Join-Path $installRoot "log.txt"
      MainMenuLoaded = $false
      WaitForInputIdle = $false
      MainWindowSeen = $false
      SurvivedLaunchWindow = $false
      BaseInstallKind = $baseInstallKind
      BaseInstallDir = $baseInstallDir
      Notes = $notes
    }
    [void]$results.Add($result)
  }

  Write-SummaryEnv -Path (Join-Path $targetRoot "summary.env") -Values @{
    RESULT = $status
    KIND = $kind
    PACKAGE_PATH = $packagePath
    BASE_INSTALL_DIR = $baseInstallDir
    BASE_INSTALL_KIND = $baseInstallKind
    INSTALL_DIR = $installRoot
    EXTRACT_DIR = $extractRoot
    COPY_STDOUT_LOG = $copyStdout
    COPY_STDERR_LOG = $copyStderr
    PACKAGE_MANIFEST_REPORT = $packageManifestReport
    INSTALL_MANIFEST_REPORT = $installManifestReport
    LAUNCH_LOG_PATH = (Join-Path $installRoot "log.txt")
    LAUNCH_LOG_TAIL = $launchTailPath
    LAUNCH_LOG_EXISTS = if ($null -ne $launchResult) { $launchResult.LogExists } else { $false }
    LAUNCH_LOG_LENGTH = if ($null -ne $launchResult) { $launchResult.LogLength } else { 0 }
    MAIN_MENU_LOADED = if ($null -ne $launchResult) { $launchResult.MainMenuLoaded } else { $false }
    WAIT_FOR_INPUT_IDLE = if ($null -ne $launchResult) { $launchResult.WaitForInputIdle } else { $false }
    WAIT_FOR_INPUT_IDLE_ERROR = if ($null -ne $launchResult) { $launchResult.WaitForInputIdleError } else { "" }
    MAIN_WINDOW_SEEN = if ($null -ne $launchResult) { $launchResult.MainWindowSeen } else { $false }
    SURVIVED_LAUNCH_WINDOW = if ($null -ne $launchResult) { $launchResult.SurvivedLaunchWindow } else { $false }
    LAUNCH_EXIT_CODE = if ($null -ne $launchResult) { $launchResult.ExitCode } else { "" }
    KILLED_AFTER_VALIDATION = if ($null -ne $launchResult) { $launchResult.KilledAfterValidation } else { $false }
    NOTES = $notes
    ERROR = $errorMessage
  }
}

$failed = @($results | Where-Object { $_.Status -ne "pass" })
$overallResult = if ($failed.Count -eq 0) { "pass" } else { "fail" }
$failedKinds = if ($failed.Count -eq 0) { "" } else { ($failed | ForEach-Object { $_.Kind }) -join "," }

$summaryLines = $results | ForEach-Object {
  "{0} status={1} package={2} install={3} main_menu_loaded={4} wait_for_input_idle={5} notes={6}" -f $_.Kind, $_.Status, $_.PackagePath, $_.InstallDir, $_.MainMenuLoaded, $_.WaitForInputIdle, $_.Notes
}
Set-Content -LiteralPath (Join-Path $artifactRoot "validation_summary.txt") -Value $summaryLines -Encoding Ascii

Write-SummaryEnv -Path (Join-Path $artifactRoot "summary.env") -Values @{
  RESULT = $overallResult
  BASE_INSTALL_DIR = $baseInstallDir
  BASE_INSTALL_KIND = $baseInstallKind
  PACKAGE_COUNT = $results.Count
  FAILED_KINDS = $failedKinds
  ARTIFACT_ROOT = $artifactRoot
}

if ($overallResult -ne "pass") {
  throw "Windows install validation failed: $failedKinds"
}

Write-Host "Windows install validation passed:"
foreach ($result in $results) {
  Write-Host ("  {0}: {1}" -f $result.Kind, $result.PackagePath)
}
Write-Host ("Artifacts: {0}" -f $artifactRoot)
