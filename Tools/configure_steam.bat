@echo off
rem Configure the Steam build (Release, VS 2022 x64) in Build_steam\.
rem Needs the Steamworks SDK in Proprietary\steamworks\sdk. No AUX_DATA: the
rem release layout finds the game's Data one folder up (Overgrowth\OverhaulOnline\).
rem The GNS build (Build\, tools\configure.bat) stays the test build.
call "R:\vs\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cd /d "%~dp0.."
if not exist "Proprietary\steamworks\sdk\public\steam\steam_api.h" (
    echo Steamworks SDK missing: unzip it so Proprietary\steamworks\sdk\public\steam\steam_api.h exists
    exit /b 1
)
cmake -S Projects -B Build_steam -G "Visual Studio 17 2022" -A x64 -DCMAKE_GENERATOR_INSTANCE=R:/vs ^
    -DBUILD_ID=2001 -DENABLE_STEAMWORKS=ON -DAUX_DATA= -DOPEN_CONSOLE=OFF -DCMAKE_POLICY_VERSION_MINIMUM=3.5 %*
