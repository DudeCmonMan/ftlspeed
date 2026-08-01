@echo off
setlocal
cd /d "%~dp0"

set "STEP=locating Visual Studio"
echo [1/6] %STEP%...
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSPATH="
if exist "%VSWHERE%" for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
if not defined VSPATH set "VSPATH=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
set "VCVARS=%VSPATH%\VC\Auxiliary\Build\vcvarsall.bat"
if not exist "%VCVARS%" goto :novcvars
echo       %VSPATH%

set "STEP=running vcvarsall x86"
echo [2/6] %STEP%...
call "%VCVARS%" x86 >nul
if errorlevel 1 goto :fail

set "STEP=generating proxy exports"
echo [3/6] %STEP%...
python tools\gen_proxy.py
if errorlevel 1 goto :fail

set "STEP=creating output directories"
echo [4/6] %STEP%...
if not exist bin mkdir bin
if not exist build mkdir build

set "CFLAGS=/nologo /O2 /W3 /MT /D_CRT_SECURE_NO_WARNINGS"

set "STEP=compiling bin\dbghelp.dll"
echo [5/6] %STEP%...
cl %CFLAGS% /Fobuild\ /Febin\dbghelp.dll /LD src\proxy.c src\core.c src\clock.c src\iat.c src\config.c gen\stubs.c user32.lib /link /DEF:gen\proxy.def
if errorlevel 1 goto :fail

rem Debug tool only - deliberately NOT shipped with the mod, so it stays out of bin\.
set "STEP=compiling ftlspeed-dbg.exe"
echo [6/6] %STEP%...
cl %CFLAGS% /Fobuild\ /Fe"%~dp0ftlspeed-dbg.exe" src\control.c
if errorlevel 1 goto :fail

echo.
echo Build OK.
echo   bin\dbghelp.dll    the mod, run deploy.bat to install it
echo   ftlspeed-dbg.exe   debug console, not shipped
exit /b 0

:novcvars
echo.
echo BUILD FAILED: vcvarsall.bat not found under "%VSPATH%"
exit /b 1

:fail
echo.
echo BUILD FAILED: %STEP%
exit /b 1
