@echo off
rem Double-click (or run from cmd) to build Eden on Windows.
rem Extra arguments are passed on, e.g.: build-windows.cmd -BuildType RelWithDebInfo -Clean
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\windows\build-local.ps1" %*
set EXITCODE=%ERRORLEVEL%
if "%~1"=="" pause
exit /b %EXITCODE%
