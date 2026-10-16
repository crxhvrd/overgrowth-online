@echo off
rem Build the engine (Release). Usage: build.bat [target]   (default Overgrowth)
call "R:\vs\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cd /d "%~dp0.."
set TARGET=%1
if "%TARGET%"=="" set TARGET=Overgrowth
cmake --build Build --config Release --target %TARGET% -- /m /v:minimal /nologo
