@echo off
setlocal
cd /d "%~dp0.."
set "CACHE_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%CACHE_VSWHERE%" (
  echo Install the free Visual Studio Build Tools with Desktop development with C++, or use WSL and make.
  exit /b 1
)
for /f "usebackq tokens=*" %%i in (`"%CACHE_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "CACHE_VSROOT=%%i"
if not defined CACHE_VSROOT exit /b 1
call "%CACHE_VSROOT%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b 1
if not exist build mkdir build
cl /nologo /std:c++17 /EHsc /W4 /O2 /Iinclude src\main.cpp /Febuild\cache_sim.exe /Fobuild\main.obj
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /O2 /Iinclude tests\test_cache.cpp /Febuild\cache_tests.exe /Fobuild\test_cache.obj
if errorlevel 1 exit /b 1
build\cache_tests.exe
if errorlevel 1 exit /b 1
python tests\test_integration.py --binary build\cache_sim.exe
exit /b %errorlevel%
