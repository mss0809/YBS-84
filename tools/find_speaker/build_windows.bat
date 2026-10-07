@echo off
setlocal
pushd "%~dp0"

where cl >nul 2>nul
if errorlevel 1 call :load_visual_studio
where cl >nul 2>nul
if errorlevel 1 goto :no_compiler

cl /nologo /std:c11 /utf-8 /W4 /D_CRT_SECURE_NO_WARNINGS /TC /c find_speaker.c
if errorlevel 1 goto :failed
cl /nologo /std:c++20 /utf-8 /EHsc /W4 /c ..\ble_helper.cpp /Fo"ble_helper.obj"
if errorlevel 1 goto :failed
link /nologo /out:find_speaker.exe find_speaker.obj ble_helper.obj windowsapp.lib
if errorlevel 1 goto :failed

echo Build succeeded: %CD%\find_speaker.exe
exit /b 0
:no_compiler
echo ERROR: cl.exe was not found.
exit /b 1
:failed
echo ERROR: build failed.
exit /b 1
:load_visual_studio
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" exit /b 0
for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_INSTALL=%%I"
if not defined VS_INSTALL exit /b 0
call "%VS_INSTALL%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
exit /b 0
