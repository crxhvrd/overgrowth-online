@echo off
rem Build the Overgrowth Online launcher (Overgrowth.exe stand-in) into Build\launcher\
rem (any Visual Studio or Build Tools with the C++ tools; set VCVARS to pick one)
setlocal enabledelayedexpansion
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not defined VCVARS if exist "!VSWHERE!" for /f "usebackq delims=" %%i in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VCVARS=%%i\VC\Auxiliary\Build\vcvars64.bat"
if not defined VCVARS set "VCVARS=R:\vs\VC\Auxiliary\Build\vcvars64.bat"
call "!VCVARS!" >nul || exit /b 1
cd /d "%~dp0"
if not exist "..\..\Build\launcher" mkdir "..\..\Build\launcher"
cl /nologo /O2 /MT /EHsc /DUNICODE /D_UNICODE launcher.cpp /Fo"..\..\Build\launcher\\" /Fe"..\..\Build\launcher\Overgrowth.exe" /link /SUBSYSTEM:WINDOWS shell32.lib ole32.lib user32.lib
