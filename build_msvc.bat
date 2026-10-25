@echo off
rem Builds FastTravelAnywhere.asi with Visual Studio (2019 or newer, "Desktop development with C++").
rem Run from an "x64 Native Tools Command Prompt for VS".
setlocal
cd /d "%~dp0"
if not exist build mkdir build
cl /nologo /O2 /MT /LD /EHs-c- /GR- /std:c++17 /DNDEBUG ^
   src\FastTravelAnywhere.cpp ^
   /Fobuild\ /Fe:build\FastTravelAnywhere.dll /link user32.lib
if errorlevel 1 exit /b 1
move /y build\FastTravelAnywhere.dll build\FastTravelAnywhere.asi >nul
del /q build\*.obj build\*.lib build\*.exp 2>nul
echo Built build\FastTravelAnywhere.asi
