@echo off
setlocal
pushd "%~dp0"

where gcc >nul 2>nul
if errorlevel 1 goto :no_gcc
where g++ >nul 2>nul
if errorlevel 1 goto :no_gcc
if not defined WindowsSdkDir goto :no_sdk_root
if not defined WindowsSDKVersion goto :no_sdk_version

set "CPPWINRT_INCLUDE=%WindowsSdkDir%Include\%WindowsSDKVersion%cppwinrt"
set "WINSDK_UM_LIB=%WindowsSdkDir%Lib\%WindowsSDKVersion%um\x64"
if not exist "%CPPWINRT_INCLUDE%\winrt\base.h" goto :no_cppwinrt
if not exist "%WINSDK_UM_LIB%\windowsapp.lib" goto :no_windowsapp

gcc -std=c11 -Wall -Wextra -Wpedantic -c battery_check.c -o battery_check.o
if errorlevel 1 goto :failed
g++ -std=c++20 -Wall -Wextra -Wpedantic -I"%CPPWINRT_INCLUDE%" -c ..\ble_helper.cpp -o ble_helper.o
if errorlevel 1 goto :failed
g++ -o battery_check.exe battery_check.o ble_helper.o -L"%WINSDK_UM_LIB%" -lwindowsapp -lruntimeobject -lole32 -lbthprops
if errorlevel 1 goto :failed

echo Build succeeded: %CD%\battery_check.exe
exit /b 0
:no_gcc
echo ERROR: gcc or g++ was not found. Run this script inside the w64devkit shell.
exit /b 1
:no_sdk_root
echo ERROR: WindowsSdkDir is not set.
exit /b 1
:no_sdk_version
echo ERROR: WindowsSDKVersion is not set.
exit /b 1
:no_cppwinrt
echo ERROR: C++/WinRT headers were not found at "%CPPWINRT_INCLUDE%".
exit /b 1
:no_windowsapp
echo ERROR: windowsapp.lib was not found at "%WINSDK_UM_LIB%".
exit /b 1
:failed
echo ERROR: build failed.
exit /b 1
