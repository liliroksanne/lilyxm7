@echo off
chcp 65001 >nul
setlocal EnableDelayedExpansion
title lilyxm7 - Lanzador de Simulador y Compilacion

cd /d "%~dp0"

:: Localizar un ejecutable real de Python (evitando el alias vacio de Microsoft Store)
set "PY_CMD="
if exist "%LOCALAPPDATA%\Python\bin\python.exe" set "PY_CMD=%LOCALAPPDATA%\Python\bin\python.exe"
if not defined PY_CMD (
    for %%P in (python3.exe python.exe) do (
        for /f "delims=" %%F in ('where %%P 2^>nul') do (
            echo %%F | findstr /i /v "WindowsApps" >nul && if not defined PY_CMD set "PY_CMD=%%F"
        )
    )
)

:: Permitir invocacion directa por parametro: local | wokwi | instalar | real
if /i "%~1"=="local" goto run_local
if /i "%~1"=="wokwi" goto run_wokwi
if /i "%~1"=="instalar" goto install_deps
if /i "%~1"=="real" goto build_real

:menu
cls
echo ============================================================
echo   lilyxm7 v1.0 - Simulador OBD-II (Suzuki Grand Vitara 2008)
echo ============================================================
echo.
echo   [1] Abrir simulador local en Python (inmediato, sin descargas)
echo   [2] Compilar ESP32-S3 y abrir simulador Wokwi en VS Code
echo   [3] Instalar herramientas para Wokwi / ESP32-S3 (una sola vez)
echo   [4] Compilar firmware REAL para la Vitara (MODO OBD-II CAN)
echo   [5] Salir
echo.
set /p "OPT=Elige una opcion (1-5): "

if "%OPT%"=="1" goto run_local
if "%OPT%"=="2" goto run_wokwi
if "%OPT%"=="3" goto install_deps
if "%OPT%"=="4" goto build_real
if "%OPT%"=="5" exit /b 0
goto menu

:run_local
echo.
if not defined PY_CMD (
    echo [ERROR] No se encontro Python instalado.
    echo Instala Python 3 desde https://www.python.org/ o con: winget install Python.Python.3.12
    pause
    goto menu
)
echo [lilyxm7] Iniciando simulador local con "%PY_CMD%"...
"%PY_CMD%" "%~dp0simulador_local.py"
if errorlevel 1 pause
if "%~1"=="" goto menu
exit /b 0

:install_deps
echo.
echo ============================================================
echo  Instalando requisitos para compilar y simular en Wokwi
echo ============================================================
echo.

where arduino-cli >nul 2>&1
if errorlevel 1 (
    echo [1/4] Instalando arduino-cli con winget...
    winget install -e --id ArduinoSA.CLI --accept-source-agreements --accept-package-agreements
    set "PATH=%PATH%;%LOCALAPPDATA%\Programs\Arduino CLI;%ProgramFiles%\Arduino CLI"
) else (
    echo [1/4] arduino-cli ya esta instalado.
)

where arduino-cli >nul 2>&1
if errorlevel 1 (
    echo.
    echo [AVISO] Cierra y vuelve a abrir esta ventana para que Windows recargue el PATH de arduino-cli,
    echo y luego vuelve a ejecutar la opcion [3].
    pause
    goto menu
)

echo [2/4] Agregando soporte de placas ESP32 (esp32:esp32)...
arduino-cli config init --overwrite >nul 2>&1
arduino-cli config add board_manager.additional_urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core update-index
arduino-cli core install esp32:esp32

echo [3/4] Instalando librerias Adafruit GFX y Adafruit ILI9341...
arduino-cli lib install "Adafruit GFX Library"
arduino-cli lib install "Adafruit ILI9341"

where code >nul 2>&1
if errorlevel 1 (
    echo [4/4] VS Code no esta en PATH. Si usas Wokwi en la web, revisa wokwi\README.md
) else (
    echo [4/4] Instalando extension Wokwi para VS Code...
    call code --install-extension wokwi.wokwi-vscode
)

echo.
echo [LISTO] Requisitos instalados. Ya puedes usar la opcion [2] para simular en Wokwi.
pause
if "%~1"=="" goto menu
exit /b 0

:run_wokwi
echo.
where arduino-cli >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Falta arduino-cli. Ejecuta primero la opcion [3] para instalar los requisitos.
    pause
    goto menu
)

echo [1/2] Compilando lilyxm7 en MODO SIMULACION para ESP32-S3...
if not exist "%~dp0build" mkdir "%~dp0build"
arduino-cli compile ^
  --fqbn esp32:esp32:esp32s3 ^
  --build-property "compiler.cpp.extra_flags=-DLILYXM7_SIMULATION_MODE=1" ^
  --output-dir "%~dp0build" ^
  "%~dp0lilyxm7"

if errorlevel 1 (
    echo.
    echo [ERROR] Fallo la compilacion. Si aun no instalas el core esp32 o librerias, ejecuta la opcion [3].
    pause
    goto menu
)

echo.
echo [2/2] Compilacion OK: build\lilyxm7.ino.merged.bin
where code >nul 2>&1
if errorlevel 1 (
    echo [AVISO] No se encontro VS Code en PATH. Abre esta carpeta en VS Code o sube los archivos a wokwi.com.
    pause
    goto menu
)

echo Abriendo VS Code en C:\Git\lilyxm7...
echo Dentro de VS Code presiona F1 y elige:  Wokwi: Start Simulator
start "" code "%~dp0"
if "%~1"=="" (
    pause
    goto menu
)
exit /b 0

:build_real
echo.
where arduino-cli >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Falta arduino-cli. Ejecuta primero la opcion [3] o compila desde Arduino IDE.
    pause
    goto menu
)

echo Compilando lilyxm7 en MODO OBD-II REAL (Vitara 2008)...
if not exist "%~dp0build_real" mkdir "%~dp0build_real"
arduino-cli compile ^
  --fqbn esp32:esp32:esp32s3 ^
  --build-property "compiler.cpp.extra_flags=-DLILYXM7_SIMULATION_MODE=0" ^
  --output-dir "%~dp0build_real" ^
  "%~dp0lilyxm7"

if errorlevel 1 (
    echo [ERROR] Fallo la compilacion.
) else (
    echo [OK] Binario real generado en build_real\lilyxm7.ino.merged.bin
)
pause
if "%~1"=="" goto menu
exit /b 0
