@echo off
setlocal
cd /d "%~dp0"
if "%~1"=="" (echo Usage: publish.cmd full-path-to-private.dpapi & exit /b 2)
call build.cmd
if errorlevel 1 exit /b 1
bin\Release\ReleaseTool.exe sign "%~1" "%~dp0bin\Release\NetPilotLite.exe"
if errorlevel 1 exit /b 1
if not exist dist mkdir dist
for %%f in (NetPilotLite.exe NetPilotLite.exe.update NetPilotLite.Updater.exe version.json SHA256SUMS.txt) do (
 copy /y "bin\Release\%%f" "dist\%%f" >nul
 if errorlevel 1 (echo Failed to publish %%f. Close the build output before publishing. & exit /b 1)
)
echo Published fixed-name files in dist. Do not overwrite a running installed copy by hand.
exit /b 0
