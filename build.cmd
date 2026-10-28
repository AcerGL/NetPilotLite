@echo off
setlocal
cd /d "%~dp0"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (echo Visual Studio 2022 C++ tools are required. & exit /b 1)
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VSROOT=%%i"
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
rem Optional workspace-only Microsoft SDK, used on the development host without installing an SDK.
if exist "..\work\native-sdk\sdk\c\Include\10.0.26100.0\um\Windows.h" (
 set "SDKBASE=%~dp0..\work\native-sdk\sdk\c"
 set "SDKLIB=%~dp0..\work\native-sdk\x64\c"
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
cl %CLFLAGS% /Foobj\app\ /Febin\Release\ReleaseTool.exe tools\release.c src\trust.c src\core.c /link /SUBSYSTEM:CONSOLE bcrypt.lib crypt32.lib version.lib user32.lib
if errorlevel 1 exit /b 1
cl %CLFLAGS% /Foobj\app\ /Febin\Release\IconTool.exe tools\icon.c src\visual.c /link /SUBSYSTEM:CONSOLE windowscodecs.lib ole32.lib uuid.lib gdi32.lib msimg32.lib advapi32.lib user32.lib
if errorlevel 1 exit /b 1
if not exist assets\NetPilotLite.ico bin\Release\IconTool.exe assets\icon-master.png assets\NetPilotLite.ico
if not exist assets\update-key.bin (
 echo Missing pinned public key. Use ReleaseTool keygen private.dpapi assets\update-key.bin once.
 exit /b 1
)
"%VSROOT%\MSBuild\Current\Bin\amd64\MSBuild.exe" NetPilotLite.sln /p:Configuration=Release /p:Platform=x64 /m:1 /v:minimal /nologo
if errorlevel 1 exit /b 1
bin\Release\NetPilotLite.Tests.exe
exit /b %errorlevel%