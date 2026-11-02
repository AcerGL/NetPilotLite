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
if not exist artifacts\update-fixtures mkdir artifacts\update-fixtures
for %%d in (old prior new fail) do if not exist artifacts\update-fixtures\%%d mkdir artifacts\update-fixtures\%%d
if not exist obj\update-tests mkdir obj\update-tests
set "LIBS=user32.lib gdi32.lib shell32.lib advapi32.lib setupapi.lib cfgmgr32.lib iphlpapi.lib wtsapi32.lib ole32.lib comctl32.lib dwmapi.lib uxtheme.lib windowscodecs.lib uuid.lib msimg32.lib wlanapi.lib ws2_32.lib comdlg32.lib version.lib bcrypt.lib shlwapi.lib propsys.lib winhttp.lib crypt32.lib"
rc /nologo /c65001 /I tests\resources /fo obj\old-fixture.res tests\resources\old.rc
if errorlevel 1 exit /b 1
rc /nologo /c65001 /I tests\resources /fo obj\prior-fixture.res tests\resources\prior.rc
if errorlevel 1 exit /b 1
rc /nologo /c65001 /I tests\resources /fo obj\new-fixture.res tests\resources\new.rc
if errorlevel 1 exit /b 1
rc /nologo /c65001 /I tests\resources /fo obj\fail-fixture.res tests\resources\fail.rc
if errorlevel 1 exit /b 1
rc /nologo /c65001 /I src /fo obj\update-tests.res src\updater.rc
if errorlevel 1 exit /b 1
cl %CLFLAGS% /DNP_READONLY_BUILD /I tests\legacy /Foobj\update-tests\ /Feartifacts\update-fixtures\old\NetPilotLite.exe tests\legacy\main.c tests\legacy\app.c tests\legacy\core.c tests\legacy\backend_win.c tests\legacy\mock.c /link obj\old-fixture.res /MANIFEST:NO /SUBSYSTEM:WINDOWS %LIBS%
if errorlevel 1 exit /b 1
cl %CLFLAGS% /DNP_READONLY_BUILD /DNP_MOCK_PRODUCT /I tests\prior /Foobj\update-tests\ /Feartifacts\update-fixtures\prior\NetPilotLite.exe tests\prior\main.c tests\prior\app.c tests\prior\core.c tests\prior\backend_win.c tests\prior\mock.c tests\prior\settings.c tests\prior\visual.c tests\prior\trust.c tests\prior\update_launch.c /link obj\prior-fixture.res /MANIFEST:NO /SUBSYSTEM:WINDOWS %LIBS%
if errorlevel 1 exit /b 1
cl %CLFLAGS% /DNP_READONLY_BUILD /DNP_MOCK_PRODUCT /Foobj\update-tests\ /Feartifacts\update-fixtures\new\NetPilotLite.exe src\main.c src\app.c src\core.c src\backend_win.c src\mock.c src\settings.c src\routing.c src\apps.c src\online.c src\private_auth.c src\private_update.c src\json.c src\visual.c src\trust.c src\update_launch.c /link obj\new-fixture.res /MANIFEST:NO /SUBSYSTEM:WINDOWS %LIBS%
if errorlevel 1 exit /b 1
cl %CLFLAGS% /Foobj\update-tests\ /Feartifacts\update-fixtures\fail\NetPilotLite.exe tests\fixture_fail.c /link obj\fail-fixture.res /MANIFEST:NO /SUBSYSTEM:WINDOWS %LIBS%
if errorlevel 1 exit /b 1
cl %CLFLAGS% /DNP_UPDATE_TEST /DNP_UPDATE_LIBRARY /Foobj\update-tests\ /Febin\Release\UpdateTests.exe tests\update_tests.c src\updater.c src\trust.c src\core.c src\settings.c src\backend_win.c src\mock.c /DNP_READONLY_BUILD /link obj\update-tests.res /MANIFEST:NO /SUBSYSTEM:CONSOLE %LIBS%
if errorlevel 1 exit /b 1
if "%~1"=="" (echo Pass the offline private.dpapi key path to sign test fixtures. & exit /b 2)
for %%d in (old prior new fail) do bin\Release\ReleaseTool.exe sign "%~1" "artifacts\update-fixtures\%%d\NetPilotLite.exe"
exit /b %errorlevel%