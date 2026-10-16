@echo off
rem Build the Steam build (Release) in Build_steam\. Usage: build_steam.bat [target]
call "R:\vs\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cd /d "%~dp0.."
set TARGET=%1
if "%TARGET%"=="" set TARGET=Overgrowth
cmake --build Build_steam --config Release --target %TARGET% -- /m /v:minimal /nologo
