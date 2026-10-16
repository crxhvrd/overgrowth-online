@echo off
rem Configure the Overgrowth engine build (Release, VS 2022 x64).
rem Game data comes from the installed public 1.4 release (AUX_DATA).
call "R:\vs\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cd /d "%~dp0.."
cmake -S Projects -B Build -G "Visual Studio 17 2022" -A x64 -DCMAKE_GENERATOR_INSTANCE=R:/vs ^
    -DBUILD_ID=2001 -DAUX_DATA="R:/SteamLibrary/steamapps/common/Overgrowth" -DCMAKE_POLICY_VERSION_MINIMUM=3.5 %*
