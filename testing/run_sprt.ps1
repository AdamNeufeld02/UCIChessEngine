<#
.SYNOPSIS
    Build the current working tree ("dev") and a baseline git ref, then run an
    SPRT match between them with fastchess to check whether a change gains Elo.

.DESCRIPTION
    - "dev"      = current working tree (including uncommitted changes), built fresh.
    - "baseline" = a git ref (default: HEAD, i.e. your last commit) checked out into
                   a throwaway git worktree and built fresh.

    Bounds presets (pass -Bounds):
      normal        elo0=0   elo1=5   -> "does this feature/tweak actually gain Elo?"
      nonregression elo0=-3  elo1=1   -> "did this refactor/speedup avoid losing Elo?"
    Or pass -Elo0/-Elo1 directly for custom bounds.

.EXAMPLE
    .\run_sprt.ps1
    Test uncommitted changes vs HEAD, default STC bounds.

.EXAMPLE
    .\run_sprt.ps1 -BaselineRef main~5 -Bounds nonregression -Concurrency 8
    Test current tree vs the commit 5 back on main, non-regression bounds, 8 games at once.

.EXAMPLE
    .\run_sprt.ps1 -Resume
    Continue a match that was interrupted (Ctrl+C, machine went to sleep, etc.)
    from its last autosave, using the exact dev/baseline binaries and weights
    already built rather than rebuilding and losing the games played so far.
#>

param(
    [string]$BaselineRef = "HEAD",
    [ValidateSet("normal", "nonregression")]
    [string]$Bounds = "normal",
    [double]$Elo0 = [double]::NaN,
    [double]$Elo1 = [double]::NaN,
    [string]$TC = "8+0.08",
    [int]$Concurrency = 0,
    [int]$Rounds = 20000,
    [int]$Hash = 16,
    [string]$Book = "$PSScriptRoot\books\8moves_v3.pgn",
    [switch]$Resume
)

$ErrorActionPreference = "Stop"

$RepoRoot   = Resolve-Path "$PSScriptRoot\.."
$FastChess  = "$PSScriptRoot\fastchess\fastchess.exe"
$EnginesDir = "$PSScriptRoot\engines"
$ResultsDir = "$PSScriptRoot\results"
$WorktreeDir = "$PSScriptRoot\.worktree\baseline"
$DevBuildDir = "$PSScriptRoot\build\dev"
$BaseBuildDir = "$PSScriptRoot\build\baseline"
# Fixed, CWD-independent location so a later -Resume can always find it,
# regardless of what directory the script happened to be invoked from -
# fastchess's own default ("config.json") is relative to CWD at launch, which
# isn't reliable (same reasoning as findNearExecutable in exe_path.h).
$ConfigPath = "$PSScriptRoot\config.json"

New-Item -ItemType Directory -Force -Path $EnginesDir, $ResultsDir | Out-Null

if (-not (Test-Path $FastChess)) {
    throw "fastchess.exe not found at $FastChess. See testing/README.md to (re)download it."
}

if ($Resume) {
    # Deliberately skips every build/worktree step: resuming should continue
    # the EXACT match that was interrupted (same dev/baseline binaries, same
    # weights files, same pairings/results) rather than risk silently
    # rebuilding from a working tree that may have changed since the match
    # started, which would make the "continued" data no longer comparable to
    # the games already played.
    if (-not (Test-Path $ConfigPath)) {
        throw "No saved match state at $ConfigPath - nothing to resume. Run without -Resume to start a new match."
    }
    Write-Host "==> Resuming SPRT from $ConfigPath" -ForegroundColor Green
    & $FastChess -config file="$ConfigPath"
    exit $LASTEXITCODE
}

if (-not (Test-Path $Book)) {
    throw "Opening book not found at $Book. See testing/README.md to (re)download it."
}

if ($Concurrency -le 0) {
    $Concurrency = [Math]::Max(1, [Environment]::ProcessorCount - 2)
}

function Resolve-CMake {
    $cmd = Get-Command cmake -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }

    $candidates = Get-ChildItem -Path "C:\Program Files\Microsoft Visual Studio\*\*\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
                                       "C:\Program Files\CMake\bin\cmake.exe" `
                                 -ErrorAction SilentlyContinue
    if ($candidates) { return $candidates[0].FullName }

    throw "Could not find cmake.exe. Install CMake or the 'C++ CMake tools for Windows' VS component, or add cmake to PATH."
}

$CMakeExe = Resolve-CMake
Write-Host "Using cmake: $CMakeExe"

if ([double]::IsNaN($Elo0) -or [double]::IsNaN($Elo1)) {
    if ($Bounds -eq "nonregression") { $Elo0 = -3.0; $Elo1 = 1.0 }
    else { $Elo0 = 0.0; $Elo1 = 5.0 }
}

function Wait-ExeReady {
    # A freshly-written .exe can be transiently refused by Windows Application
    # Control / SmartScreen-style scanning ("process creation failed") right
    # after it's copied into place. Poke it with a throwaway run -- closing
    # its stdin immediately signals EOF, so the UCI loop exits on its own --
    # so any such scan happens now, before fastchess needs it to launch.
    param([string]$ExePath, [int]$MaxAttempts = 6)

    for ($i = 1; $i -le $MaxAttempts; $i++) {
        try {
            $psi = New-Object System.Diagnostics.ProcessStartInfo
            $psi.FileName = $ExePath
            $psi.RedirectStandardInput = $true
            $psi.RedirectStandardOutput = $true
            $psi.UseShellExecute = $false

            $proc = [System.Diagnostics.Process]::Start($psi)
            $proc.StandardInput.Close()
            if (-not $proc.WaitForExit(3000)) { $proc.Kill() }
            return
        } catch {
            if ($i -eq $MaxAttempts) {
                throw "'$ExePath' still won't launch after $MaxAttempts attempts (Windows Application Control likely blocking it): $_"
            }
            Start-Sleep -Milliseconds 1000
        }
    }
}

function Get-DefaultWeightsFile {
    # Resolves the eval weights file a given source tree's engine would load
    # by default (per include/engine/default_weights_path.h's
    # DEFAULT_WEIGHTS_FILE constant), reading it directly from that tree
    # rather than assuming a fixed filename - so this stays correct even if a
    # baseline ref used a different constant value. Returns $null if there's
    # no tuned weights file for this tree (hand-set defaults only).
    param([string]$SourceDir)
    $headerPath = Join-Path $SourceDir "include\engine\default_weights_path.h"
    if (-not (Test-Path $headerPath)) { return $null }
    $content = Get-Content $headerPath -Raw
    if ($content -notmatch 'DEFAULT_WEIGHTS_FILE\s*=\s*"([^"]*)"') { return $null }
    $fileName = $Matches[1]
    if ([string]::IsNullOrEmpty($fileName)) { return $null }
    $weightsPath = Join-Path $SourceDir "data\$fileName"
    if (-not (Test-Path $weightsPath)) { return $null }
    return $weightsPath
}

function Build-Engine {
    param([string]$SourceDir, [string]$BuildDir, [string]$Label)

    Write-Host "==> Configuring $Label ($SourceDir)" -ForegroundColor Cyan
    & $CMakeExe -S $SourceDir -B $BuildDir -DCMAKE_BUILD_TYPE=Release | Write-Host
    if ($LASTEXITCODE -ne 0) { throw "$Label CMake configure failed" }

    Write-Host "==> Building $Label" -ForegroundColor Cyan
    & $CMakeExe --build $BuildDir --config Release --target uci_engine | Write-Host
    if ($LASTEXITCODE -ne 0) { throw "$Label build failed" }

    $exe = Get-ChildItem -Path $BuildDir -Recurse -Filter "uci_engine.exe" |
           Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $exe) { throw "$Label build did not produce uci_engine.exe" }
    return $exe.FullName
}

# --- Build "dev" from the current working tree ---
$devExeSrc = Build-Engine -SourceDir $RepoRoot -BuildDir $DevBuildDir -Label "dev"
$devExe = "$EnginesDir\dev.exe"
Copy-Item $devExeSrc $devExe -Force
Wait-ExeReady -ExePath $devExe

# Resolve + copy out dev's own eval weights now, from the working tree, and
# pass it to fastchess explicitly via EvalFile below - NOT via the engine's
# built-in auto-discovery. Auto-discovery (findNearExecutable) walks up from
# wherever the exe is COPIED to (testing\engines\), which has no data/ folder
# of its own, so it walks all the way up to $RepoRoot\data - the SAME folder
# both dev's and baseline's copied exes would resolve to. Relying on it here
# would silently make both engines load whatever's currently sitting in
# $RepoRoot\data, regardless of which git ref each was actually built from.
$devWeightsSrc = Get-DefaultWeightsFile $RepoRoot
$devWeights = $null
if ($devWeightsSrc) {
    $devWeights = "$EnginesDir\dev.weights.txt"
    Copy-Item $devWeightsSrc $devWeights -Force
}

# --- Build "baseline" from the requested git ref, via a throwaway worktree ---
if (Test-Path $WorktreeDir) {
    git -C $RepoRoot worktree remove --force $WorktreeDir 2>$null
    Remove-Item -Recurse -Force $WorktreeDir -ErrorAction SilentlyContinue
}
git -C $RepoRoot worktree add --detach $WorktreeDir $BaselineRef | Write-Host
if ($LASTEXITCODE -ne 0) { throw "git worktree add failed for ref '$BaselineRef'" }

$baseWeights = $null
try {
    $baseExeSrc = Build-Engine -SourceDir $WorktreeDir -BuildDir $BaseBuildDir -Label "baseline ($BaselineRef)"
    $baseExe = "$EnginesDir\baseline.exe"
    Copy-Item $baseExeSrc $baseExe -Force
    Wait-ExeReady -ExePath $baseExe

    # Must copy the weights file out of the worktree now - it gets deleted in
    # the `finally` below, before fastchess ever runs.
    $baseWeightsSrc = Get-DefaultWeightsFile $WorktreeDir
    if ($baseWeightsSrc) {
        $baseWeights = "$EnginesDir\baseline.weights.txt"
        Copy-Item $baseWeightsSrc $baseWeights -Force
    }
} finally {
    git -C $RepoRoot worktree remove --force $WorktreeDir 2>$null
}

Write-Host "==> dev weights:      $(if ($devWeights) { $devWeights } else { '<hand-set defaults only - no tuned weights file found>' })"
Write-Host "==> baseline weights: $(if ($baseWeights) { $baseWeights } else { '<hand-set defaults only - no tuned weights file found>' })"

# --- Run the SPRT match ---
$stamp = Get-Date -Format "yyyyMMdd_HHmmss"
$pgnOut = "$ResultsDir\games_$stamp.pgn"
$logOut = "$ResultsDir\fastchess_$stamp.log"

Write-Host "==> Running SPRT: dev vs baseline ($BaselineRef)" -ForegroundColor Green
Write-Host "    TC=$TC  concurrency=$Concurrency  bounds=[$Elo0, $Elo1]  book=$Book"

$devEngineArgs = @("cmd=$devExe", "name=dev")
if ($devWeights) { $devEngineArgs += "option.EvalFile=$devWeights" }

$baseEngineArgs = @("cmd=$baseExe", "name=baseline")
if ($baseWeights) { $baseEngineArgs += "option.EvalFile=$baseWeights" }

& $FastChess `
    -engine @devEngineArgs `
    -engine @baseEngineArgs `
    -each tc=$TC option.Threads=1 option.Hash=$Hash `
    -rounds $Rounds -games 2 -repeat `
    -concurrency $Concurrency `
    -openings file="$Book" format=pgn order=random plies=8 `
    -sprt elo0=$Elo0 elo1=$Elo1 alpha=0.05 beta=0.05 `
    -draw movenumber=40 movecount=8 score=8 `
    -resign movecount=3 score=400 `
    -pgnout file="$pgnOut" `
    -log file="$logOut" `
    -config outname="$ConfigPath"

Write-Host "==> PGN:  $pgnOut"
Write-Host "==> Log:  $logOut"
