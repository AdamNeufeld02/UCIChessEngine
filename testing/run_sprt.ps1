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
    [string]$Book = "$PSScriptRoot\books\8moves_v3.pgn"
)

$ErrorActionPreference = "Stop"

$RepoRoot   = Resolve-Path "$PSScriptRoot\.."
$FastChess  = "$PSScriptRoot\fastchess\fastchess.exe"
$EnginesDir = "$PSScriptRoot\engines"
$ResultsDir = "$PSScriptRoot\results"
$WorktreeDir = "$PSScriptRoot\.worktree\baseline"
$DevBuildDir = "$PSScriptRoot\build\dev"
$BaseBuildDir = "$PSScriptRoot\build\baseline"

New-Item -ItemType Directory -Force -Path $EnginesDir, $ResultsDir | Out-Null

if (-not (Test-Path $FastChess)) {
    throw "fastchess.exe not found at $FastChess. See testing/README.md to (re)download it."
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

# --- Build "baseline" from the requested git ref, via a throwaway worktree ---
if (Test-Path $WorktreeDir) {
    git -C $RepoRoot worktree remove --force $WorktreeDir 2>$null
    Remove-Item -Recurse -Force $WorktreeDir -ErrorAction SilentlyContinue
}
git -C $RepoRoot worktree add --detach $WorktreeDir $BaselineRef | Write-Host
if ($LASTEXITCODE -ne 0) { throw "git worktree add failed for ref '$BaselineRef'" }

try {
    $baseExeSrc = Build-Engine -SourceDir $WorktreeDir -BuildDir $BaseBuildDir -Label "baseline ($BaselineRef)"
    $baseExe = "$EnginesDir\baseline.exe"
    Copy-Item $baseExeSrc $baseExe -Force
} finally {
    git -C $RepoRoot worktree remove --force $WorktreeDir 2>$null
}

# --- Run the SPRT match ---
$stamp = Get-Date -Format "yyyyMMdd_HHmmss"
$pgnOut = "$ResultsDir\games_$stamp.pgn"
$logOut = "$ResultsDir\fastchess_$stamp.log"

Write-Host "==> Running SPRT: dev vs baseline ($BaselineRef)" -ForegroundColor Green
Write-Host "    TC=$TC  concurrency=$Concurrency  bounds=[$Elo0, $Elo1]  book=$Book"

& $FastChess `
    -engine cmd="$devExe" name=dev `
    -engine cmd="$baseExe" name=baseline `
    -each tc=$TC option.Threads=1 option.Hash=$Hash `
    -rounds $Rounds -games 2 -repeat `
    -concurrency $Concurrency `
    -openings file="$Book" format=pgn order=random plies=8 `
    -sprt elo0=$Elo0 elo1=$Elo1 alpha=0.05 beta=0.05 `
    -draw movenumber=40 movecount=8 score=8 `
    -resign movecount=3 score=400 `
    -pgnout file="$pgnOut" `
    -log file="$logOut"

Write-Host "==> PGN:  $pgnOut"
Write-Host "==> Log:  $logOut"
