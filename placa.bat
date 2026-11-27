@echo off
setlocal
title lilyxm7 - Placa ESP32-S3 (Carga y Logs)

set "PS_SCRIPT=%~dp0tools\cargar_placa.ps1"

:: Atajos directos desde consola:
::   placa.bat prueba        (o placa.bat -PruebaPantalla)
::   placa.bat lilyxm7
::   placa.bat logs          (o placa.bat -SoloLogs)
::   placa.bat detectar      (o placa.bat -SoloDetectar)
if /i "%~1"=="prueba" goto load_prueba
if /i "%~1"=="-PruebaPantalla" goto load_prueba
if /i "%~1"=="lilyxm7" goto load_lilyxm7
if /i "%~1"=="logs" goto only_logs
if /i "%~1"=="-SoloLogs" goto only_logs
if /i "%~1"=="detectar" goto only_detect
if /i "%~1"=="-SoloDetectar" goto only_detect

:: Si se pasa otro parametro (ej. COM3), pasarlo directo al script
if not "%~1"=="" (
    powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%PS_SCRIPT%" %*
    exit /b %ERRORLEVEL%
)

:menu
cls
echo ============================================================
echo   lilyxm7 - Placa Fisica ESP32-S3 (LAFVIN Retro Game PCB)
echo   Logs guardados en: %~dp0logs\
echo ============================================================
echo.
echo   [1] Cargar prueba_pantalla en la placa (pantalla + botones)
echo   [2] Cargar lilyxm7 completo en la placa (OBD-II Vitara)
echo   [3] Sacar logs de la placa por USB (Monitor Serial 115200)
echo   [4] Detectar puerto COM de la placa
echo   [5] Salir
echo.
set /p "OPT=Elige una opcion (1-5): "

if "%OPT%"=="1" goto load_prueba
if "%OPT%"=="2" goto load_lilyxm7
if "%OPT%"=="3" goto only_logs
if "%OPT%"=="4" goto only_detect
if "%OPT%"=="5" exit /b 0
goto menu

:load_prueba
echo.
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%PS_SCRIPT%" -PruebaPantalla
if "%~1"=="" (
    echo.
    pause
    goto menu
)
exit /b %ERRORLEVEL%

:load_lilyxm7
echo.
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%PS_SCRIPT%"
if "%~1"=="" (
    echo.
    pause
    goto menu
)
exit /b %ERRORLEVEL%

:only_logs
echo.
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%PS_SCRIPT%" -SoloLogs
if "%~1"=="" (
    echo.
    pause
    goto menu
)
exit /b %ERRORLEVEL%

:only_detect
echo.
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%PS_SCRIPT%" -SoloDetectar
if "%~1"=="" (
    echo.
    pause
    goto menu
)
exit /b %ERRORLEVEL%
