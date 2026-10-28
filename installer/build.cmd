@echo off
setlocal
cd /d "%~dp0.."
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (echo Visual Studio 2022 C++ tools are required. & exit /b 1)
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VSROOT=%%i"
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
rem Optional workspace-only Microsoft SDK, used on the development host without installing an SDK.
if exist "..\work\native-sdk\sdk\c\Include\10.0.26100.0\um\Windows.h" (
 set "SDKBASE=%~dp0..\..\work\native-sdk\sdk\c"
 set "SDKLIB=%~dp0..\..\work\native-sdk\x64\c"
)
if defined SDKBASE (
 set "INCLUDE=%SDKBASE%\Include\10.0.26100.0\ucrt;%SDKBASE%\Include\10.0.26100.0\shared;%SDKBASE%\Include\10.0.26100.0\um;%INCLUDE%"
 set "LIB=%SDKLIB%\um\x64;%SDKLIB%\ucrt\x64;%LIB%"
 set "PATH=%SDKBASE%\bin\10.0.26100.0\x64;%PATH%"
)
if not exist bin\Release mkdir bin\Release
if not exist obj\app mkdir obj\app
if not exist obj\tests mkdir obj\tests
if not exist artifacts mkdir artifacts
set "CLFLAGS=/nologo /TC /std:c17 /utf-8 /W4 /O2 /MT /GS /guard:cf /D_WIN32_WINNT=0x0A00 /I src"

if not exist obj\installer mkdir obj\installer
rc /nologo /I installer\common /fo obj\installer\guard.res installer\common\guard.rc
if errorlevel 1 exit /b 1
cl %CLFLAGS% /LD /Foobj\installer\ /Febin\Release\InstallGuard.dll installer\common\guard.c src\trust.c src\core.c /link obj\installer\guard.res /MANIFEST:NO user32.lib bcrypt.lib version.lib ole32.lib
if errorlevel 1 exit /b 1
rc /nologo /I installer\web /fo obj\installer\web.res installer\web\web.rc
if errorlevel 1 exit /b 1
cl %CLFLAGS% /Foobj\installer\ /Febin\Release\NetPilotLite-Web-Setup.exe installer\web\main.c src\online.c src\json.c installer\web\cache.c src\trust.c src\core.c /link obj\installer\web.res /MANIFEST:NO /SUBSYSTEM:WINDOWS user32.lib gdi32.lib comctl32.lib shell32.lib ole32.lib uuid.lib advapi32.lib bcrypt.lib version.lib winhttp.lib shlwapi.lib
if errorlevel 1 exit /b 1
exit /b 0
