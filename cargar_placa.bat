@echo off
setlocal
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\cargar_placa.ps1" %*
set "RC=%ERRORLEVEL%"
if not "%RC%"=="0" if "%~1"=="" pause
exit /b %RC%
