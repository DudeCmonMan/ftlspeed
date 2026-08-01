@echo off
setlocal
cd /d "%~dp0"

for %%i in ("%~dp0..") do set "GAMEROOT=%%~fi"

if not exist "%GAMEROOT%\FTLGame.exe" goto :nogame
if not exist "bin\dbghelp.dll" goto :nobuild

tasklist /fi "imagename eq FTLGame.exe" 2>nul | find /i "FTLGame.exe" >nul
if not errorlevel 1 goto :running

echo Deploying to "%GAMEROOT%"
copy /y "bin\dbghelp.dll" "%GAMEROOT%\" >nul
if errorlevel 1 goto :copyfail
echo   dbghelp.dll    installed

rem Never clobber an existing config - it holds the user's chosen speed.
if exist "%GAMEROOT%\speed.toml" (
    echo   speed.toml     kept existing
) else (
    copy /y "speed.toml" "%GAMEROOT%\" >nul
    if errorlevel 1 goto :copyfail
    echo   speed.toml     installed
)

rem ftlspeed-dbg.exe is a dev tool and is deliberately never deployed.

echo.
echo Done. Start FTL however you normally do.
echo Uninstall: delete dbghelp.dll from "%GAMEROOT%"
exit /b 0

:nogame
echo DEPLOY FAILED: FTLGame.exe not found in "%GAMEROOT%"
echo This folder must sit inside the FTL install directory.
exit /b 1

:nobuild
echo DEPLOY FAILED: bin\dbghelp.dll missing. Run build.bat first.
exit /b 1

:running
echo DEPLOY FAILED: FTL is running. Close it first - Windows locks a loaded DLL.
exit /b 1

:copyfail
echo DEPLOY FAILED: could not copy into "%GAMEROOT%"
exit /b 1
