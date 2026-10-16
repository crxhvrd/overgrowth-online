# Overgrowth Online installer. Run install.bat (next to this file) with Overgrowth closed.
#
# - Overgrowth\OvergrowthOnline\        the online build (its own exe, scripts and shaders)
# - Overgrowth\Overgrowth.exe           a small launcher: Steam starts it for Play, invites and
#                                       "Join Game"; it starts Overgrowth Online (hold Shift for the
#                                       normal game). The original is kept as Overgrowth_stock.exe.
# - Documents\Wolfire\OvergrowthOnline\ its settings and mods, apart from the normal game's: the
#                                       mod goes there; your normal settings are copied once. An
#                                       older install (OverhaulOnline) is renamed: its folder goes,
#                                       its settings move over.
# Uninstall: uninstall.bat. Steam's "Verify integrity of game files" also undoes the launcher
# (run install.bat again afterwards).
param([string]$GamePath = "", [string]$WriteDir = "")  # WriteDir: for tests
$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path

. (Join-Path $here "find_game.ps1")

if (Get-Process -Name Overgrowth, Overgrowth_stock, OvergrowthOnline, OverhaulOnline -ErrorAction SilentlyContinue) {
    Write-Host "Overgrowth is running. Close it, then run install.bat again." -ForegroundColor Yellow
    exit 1
}

$game = if ($GamePath) { $GamePath } else { Find-Overgrowth }
if (-not $game -or -not (Test-Path (Join-Path $game "Data\Levels"))) {
    Write-Host "Couldn't find Overgrowth. Run:  install.bat ""<your Overgrowth folder>""" -ForegroundColor Yellow
    exit 1
}
Write-Host "Overgrowth: $game"

# 1. The online build
$online = Join-Path $game "OvergrowthOnline"
if (Test-Path $online) { Remove-Item $online -Recurse -Force }
Copy-Item (Join-Path $here "OvergrowthOnline") $online -Recurse
Get-ChildItem $online -Recurse -File | Unblock-File  # downloaded zips mark every file "from the internet"
Write-Host "  installed OvergrowthOnline\"
$old_online = Join-Path $game "OverhaulOnline"  # (its name before Overgrowth Online)
if (Test-Path $old_online) {
    Remove-Item $old_online -Recurse -Force
    Write-Host "  removed the old OverhaulOnline\"
}

# 2. The launcher in place of Overgrowth.exe (the original kept once)
$exe = Join-Path $game "Overgrowth.exe"
$stock = Join-Path $game "Overgrowth_stock.exe"
$launcher = Join-Path $here "launcher\Overgrowth.exe"
# Any version of our launcher is small (~130 KB); the game is several MB. Only
# the game itself is ever kept as Overgrowth_stock.exe (an older launcher once
# replaced it there)
$is_launcher = (Get-Item $exe).Length -lt 2MB
if (-not $is_launcher) {
    # Not ours: the original (first install, or Steam put it back)
    Copy-Item $exe $stock -Force
    Write-Host "  kept the original as Overgrowth_stock.exe"
} elseif (-not (Test-Path $stock) -or (Get-Item $stock).Length -lt 2MB) {
    Write-Host "  The original Overgrowth.exe is missing. In Steam: Overgrowth > Properties > Installed Files >" -ForegroundColor Yellow
    Write-Host "  Verify integrity of game files, then run install.bat again. Until then Shift can't start the normal game." -ForegroundColor Yellow
}
Copy-Item $launcher $exe -Force
Unblock-File $exe
Write-Host "  launcher installed (hold Shift when starting for the normal game)"

# 3. Settings and mods: Documents\Wolfire\OvergrowthOnline
$docs = [Environment]::GetFolderPath("MyDocuments")
$wd = if ($WriteDir) { $WriteDir } else { Join-Path $docs "Wolfire\OvergrowthOnline" }
$old_wd = Join-Path $docs "Wolfire\OverhaulOnline"
if (-not $WriteDir -and -not (Test-Path $wd) -and (Test-Path $old_wd)) {
    Move-Item $old_wd $wd
    Write-Host "  moved your settings and mods from Wolfire\OverhaulOnline"
}
$mods = Join-Path $wd "Data\Mods"
New-Item -ItemType Directory -Force $mods | Out-Null
$mod = Join-Path $mods "overhaul"
if (Test-Path $mod) { Remove-Item $mod -Recurse -Force }
Copy-Item (Join-Path $here "mod\overhaul") $mod -Recurse
$version = ([xml](Get-Content (Join-Path $mod "mod.xml"))).Mod.Version

# Terrain maps earlier versions generated broken (striped normal maps: dark bands
# over the ground); the game's own are used now, or they're made again correctly
# (the engine's own names: <heightmap>.png_normal.png, <heightmap>.png_<level>_weights.png;
# never in Data\Mods, where mods keep their own)
$mods_dir = Join-Path $wd "Data\Mods"
$broken = @(Get-ChildItem (Join-Path $wd "Data") -Recurse -File -ErrorAction SilentlyContinue |
    Where-Object { -not $_.FullName.StartsWith($mods_dir, [StringComparison]::OrdinalIgnoreCase) -and
        ($_.Name -match '\.png_normal\.png(_converted\.dds)?$' -or $_.Name -match '\.png_.+_weights\.png(_converted\.dds|_image_sample_cache)?$') })
$broken += @(Get-ChildItem $wd -File -Filter "__weights.png*" -ErrorAction SilentlyContinue)
if ($broken.Count -gt 0) {
    $broken | Remove-Item -Force
    Write-Host "  removed $($broken.Count) broken terrain maps an earlier version made"
}

$config = Join-Path $wd "Data\config.txt"
$normal = Join-Path $docs "Wolfire\Overgrowth\Data\config.txt"
if (-not (Test-Path $config) -and (Test-Path $normal)) {
    Copy-Item $normal $config
    Write-Host "  copied your normal settings"
}

$stock_mods = @(
    @("com-wolfire-arena", "0.0.1"), @("com-wolfire-drika", "1.0.0"), @("com-wolfire-lugaru-campaign", "1.0.0"),
    @("com-wolfire-machine-translations", "0.0.1"), @("com-wolfire-overgrowth-campaign", "1.0.0"),
    @("com-wolfire-overgrowth-core", "1.0.0"), @("com-wolfire-sandbox-levels", "0.0.1"),
    @("com-wolfire-timbles-therium", "rc"), @("com-wolfire-versus", "0.0.1"), @("crxhvrd-overhaul", $version))
$rows = ($stock_mods | ForEach-Object { "    <ModInstance id=""$($_[0])"" activated=""true"" modsource=""local"" version=""$($_[1])"" />" }) -join "`n"
$modconfig = Join-Path $wd "modconfig.xml"
[IO.File]::WriteAllText($modconfig, "<?xml version=""2.0"" ?>`n<ActiveMods>`n$rows`n</ActiveMods>`n", (New-Object Text.UTF8Encoding $false))
Write-Host "  Overgrowth Online $version installed and enabled ($wd)"

Write-Host ""
Write-Host "Done. Start Overgrowth from Steam as usual (Steam must be running)." -ForegroundColor Green
