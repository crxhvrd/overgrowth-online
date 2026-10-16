@echo off
rem Build the Overgrowth Online launcher (Overgrowth.exe stand-in) into Build\launcher\
call "R:\vs\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cd /d "%~dp0"
if not exist "..\..\Build\launcher" mkdir "..\..\Build\launcher"
cl /nologo /O2 /MT /EHsc /DUNICODE /D_UNICODE launcher.cpp /Fo"..\..\Build\launcher\\" /Fe"..\..\Build\launcher\Overgrowth.exe" /link /SUBSYSTEM:WINDOWS shell32.lib ole32.lib user32.lib
