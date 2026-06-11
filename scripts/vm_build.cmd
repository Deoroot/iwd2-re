@echo off
REM Build iwd2-re on the VM via the VS2019 Developer environment.
REM Invoked from the host over SSH: ssh win11vm cmd /c C:\iwd2-re\scripts\vm_build.cmd
setlocal
set "VSDEV=C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\Common7\Tools\VsDevCmd.bat"
if not exist "%VSDEV%" (
  echo ERROR: VsDevCmd.bat not found at "%VSDEV%"
  exit /b 1
)
call "%VSDEV%" -arch=x86 -no_logo
echo ==^> taskkill stale exe
taskkill /f /im iwd2-re.exe 2>nul
echo ==^> cmake --build
cmake --build C:\iwd2-re\build --config Debug
exit /b %ERRORLEVEL%
