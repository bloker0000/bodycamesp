@echo off
setlocal
cd /d "%~dp0"

set "PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"
for /f "usebackq delims=" %%i in (`vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "vs=%%i"
call "%vs%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
if not exist build\obj mkdir build\obj

cl /nologo /std:c++20 /O2 /MT /EHa /W4 /LD src\*.cpp /Fobuild\obj\ /Fe:build\bcesp.dll /link user32.lib || exit /b 1
cl /nologo /std:c++20 /O2 /MT /EHsc /W4 injector\inject.cpp /Fobuild\obj\ /Fe:build\inject.exe || exit /b 1
echo built build\bcesp.dll and build\inject.exe
