# Overgrowth Online uninstaller: the original Overgrowth.exe back, the OvergrowthOnline folder removed
# (and an older install's OverhaulOnline). Its settings (Documents\Wolfire\OvergrowthOnline) stay;
# delete that folder yourself if you like.
param([string]$GamePath = "")
$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $here "find_game.ps1")

if (Get-Process -Name Overgrowth, Overgrowth_stock, OvergrowthOnline, OverhaulOnline -ErrorAction SilentlyContinue) {
    Write-Host "Overgrowth is running. Close it, then run uninstall.bat again." -ForegroundColor Yellow
    exit 1
}

$game = if ($GamePath) { $GamePath } else { Find-Overgrowth }
if (-not $game) {
    Write-Host "Couldn't find Overgrowth. Run:  uninstall.bat ""<your Overgrowth folder>""" -ForegroundColor Yellow
    exit 1
}

$exe = Join-Path $game "Overgrowth.exe"
$stock = Join-Path $game "Overgrowth_stock.exe"
if ((Test-Path $stock) -and (Get-Item $stock).Length -ge 2MB) {  # (the game, never a launcher)
    Copy-Item $stock $exe -Force
    Remove-Item $stock -Force
    Write-Host "  the original Overgrowth.exe is back"
} else {
    Write-Host "  no original Overgrowth.exe kept: use Steam's Verify integrity of game files to get it back" -ForegroundColor Yellow
}

foreach ($name in "OvergrowthOnline", "OverhaulOnline") {
    $online = Join-Path $game $name
    if (Test-Path $online) {
        Remove-Item $online -Recurse -Force
        Write-Host "  removed $name\"
    }
}
Write-Host "Done." -ForegroundColor Green
