@echo off
rem Builds BatteryWatch.dll (Rainmeter plugin) and BatteryCheck.exe (test tool).
rem Run this from the "x64 Native Tools Command Prompt for VS".
setlocal
cd /d "%~dp0"
if not exist out mkdir out

set FLAGS=/nologo /O2 /MT /EHsc /std:c++17 /W3 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS
set LIBS=hid.lib setupapi.lib

cl %FLAGS% /LD src\plugin.cpp src\hid_win.cpp /Foout\ /Fe:out\BatteryWatch.dll /link %LIBS% || goto :fail
cl %FLAGS% src\battery_check.cpp src\hid_win.cpp /Foout\ /Fe:out\BatteryCheck.exe /link %LIBS% || goto :fail

echo.
echo Built out\BatteryWatch.dll and out\BatteryCheck.exe
exit /b 0

:fail
echo.
echo BUILD FAILED - see the errors above.
exit /b 1
