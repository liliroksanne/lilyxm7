@echo off
setlocal EnableDelayedExpansion
title lilyxm7 - Lanzador de Simulador y Compilacion

cd /d "%~dp0"
if not exist "%~dp0logs" mkdir "%~dp0logs"

:: Asegurar rutas habituales de arduino-cli en PATH
set "PATH=%PATH%;%ProgramFiles%\Arduino CLI;%LOCALAPPDATA%\Programs\Arduino CLI"

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

set "ESP32_URL=https://espressif.github.io/arduino-esp32/package_esp32_index.json"
set "LOGGER=%~dp0tools\run_logged.py"

:: Permitir invocacion directa por parametro: simweb | local | web | wokwi | instalar | real | diag | serial
if /i "%~1"=="simweb" goto run_sim_web
if /i "%~1"=="local" goto run_local
if /i "%~1"=="web" goto run_wokwi_web
if /i "%~1"=="wokwi" goto run_wokwi
if /i "%~1"=="instalar" goto install_deps
if /i "%~1"=="real" goto build_real
if /i "%~1"=="diag" goto run_diag
if /i "%~1"=="serial" goto run_serial

:menu
cls
echo ============================================================
echo   lilyxm7 v1.0 - Simulador OBD-II (Suzuki Grand Vitara 2008)
echo   Logs centralizados en: %~dp0logs\
echo ============================================================
echo.
echo   [1] Simulador WEB (compila y recarga el firmware al editar)
echo   [2] Wokwi Web en navegador (paquete 1 clic para wokwi.com)
echo   [3] Compilar ESP32-S3 y abrir Wokwi en VS Code
echo   [4] Instalar herramientas (Arduino CLI, ESP32-S3 y g++ para C++ real)
echo   [5] Compilar firmware REAL para la Vitara (MODO OBD-II CAN)
echo   [6] Capturar logs USB de la ESP32-S3 real (logs\esp32_serial.log)
echo   [7] Generar diagnostico de entorno (logs\diagnostics.log)
echo   [8] Simulador clasico de escritorio (Tkinter)
echo   [9] Salir
echo.
set /p "OPT=Elige una opcion (1-9): "

if "%OPT%"=="1" goto run_sim_web
if "%OPT%"=="2" goto run_wokwi_web
if "%OPT%"=="3" goto run_wokwi
if "%OPT%"=="4" goto install_deps
if "%OPT%"=="5" goto build_real
if "%OPT%"=="6" goto run_serial
if "%OPT%"=="7" goto run_diag
if "%OPT%"=="8" goto run_local
if "%OPT%"=="9" exit /b 0
goto menu

:run_cmd
:: Uso interno: call :run_cmd <componente_log> <comando y args...>
set "LOG_COMP=%~1"
shift
set "CMD_REST="
:gather_args
if "%~1"=="" goto exec_gathered
set "CMD_REST=!CMD_REST! %1"
shift
goto gather_args
:exec_gathered
if defined PY_CMD (
    "%PY_CMD%" "%LOGGER%" "%LOG_COMP%" -- !CMD_REST!
    exit /b !errorlevel!
) else (
    echo [AVISO] Python no encontrado para logger; guardando log basico en logs\%LOG_COMP%.log
    !CMD_REST! >> "%~dp0logs\%LOG_COMP%.log" 2>&1
    exit /b !errorlevel!
)

:run_sim_web
echo.
if not defined PY_CMD (
    echo [ERROR] No se encontro Python instalado.
    pause
    goto menu
)
echo [lilyxm7] Iniciando Simulador WEB con recarga automatica ...
echo           (Logs en logs\sim_web.log y logs\all.log)
"%PY_CMD%" "%~dp0sim_web\server.py" --watch-firmware
if errorlevel 1 (
    echo [ERROR] El servidor web termino con error. Revisa logs\sim_web.log
    pause
)
if "%~1"=="" goto menu
exit /b 0

:run_local
echo.
if not defined PY_CMD (
    echo [ERROR] No se encontro Python instalado.
    echo Instala Python 3 desde https://www.python.org/ o con: winget install Python.Python.3.12
    pause
    goto menu
)
echo [lilyxm7] Iniciando simulador local... (log: logs\simulador_local.log)
"%PY_CMD%" "%~dp0simulador_local.py"
if errorlevel 1 (
    echo [ERROR] El simulador local termino con error. Revisa logs\simulador_local.log
    pause
)
if "%~1"=="" goto menu
exit /b 0

:run_wokwi_web
echo.
if not defined PY_CMD (
    echo [ERROR] No se encontro Python para generar el paquete Wokwi Web.
    pause
    goto menu
)
echo [lilyxm7] Generando paquete unificado para Wokwi Web...
"%PY_CMD%" "%~dp0tools\export_wokwi_web.py"
if errorlevel 1 (
    echo [ERROR] Fallo la generacion del paquete Wokwi Web. Revisa logs\wokwi_web.log
) else (
    echo.
    echo [OK] Panel abierto en tu navegador: wokwi_web\abrir_wokwi.html
    echo      Archivos tambien guardados en: wokwi_web\
)
pause
if "%~1"=="" goto menu
exit /b 0

:install_deps
echo.
echo ============================================================
echo  Instalando requisitos para Wokwi / ESP32-S3
echo  Log completo en: logs\install.log y logs\all.log
echo ============================================================
echo.

where arduino-cli >nul 2>&1
if errorlevel 1 (
    echo [1/5] Instalando arduino-cli con winget...
    call :run_cmd install winget install -e --id ArduinoSA.CLI --accept-source-agreements --accept-package-agreements
    set "PATH=%PATH%;%ProgramFiles%\Arduino CLI;%LOCALAPPDATA%\Programs\Arduino CLI"
) else (
    echo [1/5] arduino-cli ya esta instalado.
    call :run_cmd install arduino-cli version
)

where arduino-cli >nul 2>&1
if errorlevel 1 (
    echo.
    echo [ERROR] No se detecto arduino-cli en PATH. Cierra y vuelve a abrir esta ventana y reintenta [3].
    pause
    goto menu
)

echo [2/5] Configurando URL oficial de placas ESP32...
call :run_cmd install arduino-cli config init --overwrite
call :run_cmd install arduino-cli config add board_manager.additional_urls "%ESP32_URL%"
call :run_cmd install arduino-cli core update-index --additional-urls "%ESP32_URL%"
if errorlevel 1 (
    echo [ERROR] Fallo core update-index. Revisa logs\install.log
    pause
    goto menu
)

echo [3/5] Instalando core esp32:esp32 (puede tardar varios minutos, ~400 MB)...
call :run_cmd install arduino-cli core install esp32:esp32 --additional-urls "%ESP32_URL%"
if errorlevel 1 (
    echo [ERROR] Fallo la instalacion de esp32:esp32. Revisa logs\install.log
    pause
    goto menu
)

echo [4/5] Instalando librerias Adafruit GFX Library y Adafruit ILI9341...
call :run_cmd install arduino-cli lib install "Adafruit GFX Library"
if errorlevel 1 (
    echo [ERROR] Fallo la instalacion de Adafruit GFX Library. Revisa logs\install.log
    pause
    goto menu
)
call :run_cmd install arduino-cli lib install "Adafruit ILI9341"
if errorlevel 1 (
    echo [ERROR] Fallo la instalacion de Adafruit ILI9341. Revisa logs\install.log
    pause
    goto menu
)

where code >nul 2>&1
if errorlevel 1 (
    echo [5/6] VS Code no esta en PATH. Si usas Wokwi web, revisa wokwi\README.md
) else (
    echo [5/6] Instalando extension Wokwi para VS Code...
    call :run_cmd install code.cmd --install-extension wokwi.wokwi-vscode
)

where g++ >nul 2>&1
if errorlevel 1 (
    echo [6/6] Instalando g++ ^(WinLibs^) para ejecutar C++ real en el Simulador WEB local...
    call :run_cmd install winget install -e --id BrechtSanders.WinLibs.POSIX.UCRT --accept-source-agreements --accept-package-agreements
) else (
    echo [6/6] g++ ya esta instalado.
    call :run_cmd install g++ --version
)

echo.
echo Verificando estado final instalado:
call :run_cmd install arduino-cli core list
call :run_cmd install arduino-cli lib list

echo.
echo [LISTO] Requisitos instalados correctamente. Log guardado en logs\install.log
pause
if "%~1"=="" goto menu
exit /b 0

:run_wokwi
echo.
where arduino-cli >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Falta arduino-cli. Ejecuta primero la opcion [4] para instalar los requisitos.
    pause
    goto menu
)

echo [1/2] Compilando lilyxm7 en MODO SIMULACION para ESP32-S3...
echo       (Log: logs\build_sim.log)
if not exist "%~dp0build" mkdir "%~dp0build"
call :run_cmd build_sim arduino-cli compile --fqbn esp32:esp32:esp32s3 --build-property "compiler.cpp.extra_flags=-DLILYXM7_SIMULATION_MODE=1" --output-dir "%~dp0build" "%~dp0lilyxm7"

if errorlevel 1 (
    echo.
    echo [ERROR] Fallo la compilacion. Log completo en: logs\build_sim.log
    echo Si el error dice que falta 'esp32:esp32' o una libreria, ejecuta la opcion [4].
    set /p "RUN_INST=Quieres ejecutar la opcion [4] ahora mismo? (S/N): "
    if /i "!RUN_INST!"=="S" goto install_deps
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

echo Abriendo VS Code con el circuito Wokwi (diagram.json)...
echo Si es la primera vez: F1 -^> "Wokwi: Request a New License" (es GRATIS).
echo Luego solo dale al boton verde Play en la pestana de diagram.json.
start "" code "%~dp0" "%~dp0diagram.json"
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
echo (Log: logs\build_real.log)
if not exist "%~dp0build_real" mkdir "%~dp0build_real"
call :run_cmd build_real arduino-cli compile --fqbn esp32:esp32:esp32s3 --build-property "compiler.cpp.extra_flags=-DLILYXM7_SIMULATION_MODE=0" --output-dir "%~dp0build_real" "%~dp0lilyxm7"

if errorlevel 1 (
    echo [ERROR] Fallo la compilacion. Revisa logs\build_real.log
) else (
    echo [OK] Binario real generado en build_real\lilyxm7.ino.merged.bin
)
pause
if "%~1"=="" goto menu
exit /b 0

:run_diag
echo.
echo Recopilando diagnostico completo en logs\diagnostics.log ...
call :run_cmd diagnostics arduino-cli version
call :run_cmd diagnostics arduino-cli config dump
call :run_cmd diagnostics arduino-cli core list
call :run_cmd diagnostics arduino-cli lib list
if defined PY_CMD call :run_cmd diagnostics "%PY_CMD%" --version
where code >nul 2>&1 && call :run_cmd diagnostics code.cmd --list-extensions
echo.
echo [OK] Diagnostico guardado en logs\diagnostics.log y logs\all.log
pause
if "%~1"=="" goto menu
exit /b 0

:run_serial
echo.
where arduino-cli >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Falta arduino-cli para abrir el monitor serial.
    pause
    goto menu
)
echo Puertos USB detectados:
echo.
arduino-cli board list
echo.
set "COM_PORT=%~2"
if not defined COM_PORT set /p "COM_PORT=Escribe el puerto COM de tu ESP32-S3 (ej. COM3) o Enter para cancelar: "
if not defined COM_PORT goto menu

echo.
echo Capturando logs de %COM_PORT% a 115200 baud...
echo Todo se guarda en logs\esp32_serial.log y logs\all.log (Ctrl+C para detener).
echo.
call :run_cmd esp32_serial arduino-cli monitor -p %COM_PORT% -c baudrate=115200
pause
if "%~1"=="" goto menu
exit /b 0
