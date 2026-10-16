# Where Overgrowth is installed: every Steam library folder (libraryfolders.vdf)
function Find-Overgrowth {
    $libs = @()
    foreach ($key in "HKCU:\Software\Valve\Steam", "HKLM:\SOFTWARE\WOW6432Node\Valve\Steam") {
        try {
            $p = (Get-ItemProperty $key -ErrorAction Stop)
            foreach ($name in "SteamPath", "InstallPath") {
                if ($p.$name) { $libs += $p.$name }
            }
        } catch {}
    }
    foreach ($steam in @($libs)) {
        $vdf = Join-Path $steam "steamapps\libraryfolders.vdf"
        if (Test-Path $vdf) {
            foreach ($line in Get-Content $vdf) {
                if ($line -match '"path"\s+"([^"]+)"') { $libs += ($Matches[1] -replace '\\\\', '\') }
            }
        }
    }
    foreach ($lib in $libs | Select-Object -Unique) {
        $game = Join-Path $lib "steamapps\common\Overgrowth"
        if (Test-Path (Join-Path $game "Overgrowth.exe")) { return $game }
    }
    return $null
}
