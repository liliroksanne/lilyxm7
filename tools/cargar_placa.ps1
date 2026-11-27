[CmdletBinding()]
param(
    [Parameter(Position=0)]
    [ValidatePattern('^COM[1-9][0-9]*$')]
    [string]$Puerto,
    [switch]$SoloDetectar,
    [string]$ArduinoData,
    [string]$ArduinoLibraries,
    [switch]$SoloCompilar,
    [switch]$PruebaPantalla,
    [switch]$SoloLogs
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$sketchName = if ($PruebaPantalla) { 'prueba_pantalla' } else { 'lilyxm7' }
$sketch = Join-Path $root $sketchName
$logs = Join-Path $root 'logs'
$null = New-Item -ItemType Directory -Force -Path $logs
$stamp = '{0:yyyyMMdd-HHmmss}-{1}' -f (Get-Date), $PID
$runLog = Join-Path $logs "carga_esp32_$stamp.log"
$serialLog = Join-Path $logs "esp32_serial_$stamp.log"
$latestSerialLog = Join-Path $logs 'esp32_serial_latest.log'
$utf8 = New-Object System.Text.UTF8Encoding($false)
$fqbn = 'esp32:esp32:esp32s3'

function Write-Carga([string]$Message) {
    Write-Host $Message
    [IO.File]::AppendAllText($runLog, "[$(Get-Date -Format HH:mm:ss)] $Message`r`n", $utf8)
}

function Get-Ports {
    $ErrorActionPreference = 'Continue'
    $output = & $cli @cliConfig board list --discovery-timeout 2s --json 2>&1
    $code = $LASTEXITCODE
    $raw = foreach ($line in $output) {
        if ($line -is [System.Management.Automation.ErrorRecord]) { Write-Carga ([string]$line) }
        else { [string]$line }
    }
    if ($code -ne 0) {
        throw "Arduino CLI no pudo enumerar los puertos. Revisa $runLog."
    }
    $ErrorActionPreference = 'Stop'
    $result = ($raw -join "`n") | ConvertFrom-Json
    if (!$result -or !$result.PSObject.Properties['detected_ports']) {
        throw "Arduino CLI no devolvio el listado esperado. Revisa $runLog."
    }
    foreach ($entry in $result.detected_ports) {
        $port = $entry.port
        if ($port.protocol -ne 'serial' -or $port.address -notmatch '^COM[1-9][0-9]*$') { continue }
        $vid = ([string]$port.properties.vid) -replace '^0x', ''
        $pidUsb = ([string]$port.properties.pid) -replace '^0x', ''
        $usb = "{0}:{1}" -f $vid, $pidUsb
        $serial = $port.properties.serialNumber
        if (!$serial) { $serial = $port.properties.serial_number }
        $native = $usb -eq '303a:1001'
        $isS3 = @($entry.matching_boards | Where-Object {
            $_.fqbn -match '^esp32:esp32:esp32s3(?::|$)' -or $_.name -match 'ESP32.S3'
        }).Count -gt 0
        $bridge = $usb -in @('10c4:ea60','1a86:7523','1a86:55d4','0403:6001','0403:6015')
        [pscustomobject]@{
            Address = $port.address
            Usb = $usb
            Serial = [string]$serial
            Native = $native
            Candidate = $isS3 -or $native -or $bridge
            Label = ($entry.matching_boards.name -join ', ')
        }
    }
}

function Select-UploadPort($Ports) {
    if ($Puerto) {
        $match = @($Ports | Where-Object Address -eq $Puerto)
        if ($match.Count -eq 1) { return $match[0] }
        throw "$Puerto no aparece entre los puertos serie actuales."
    }

    $candidates = @($Ports | Where-Object Candidate)
    if ($candidates.Count -eq 1) { return $candidates[0] }
    if (!$Ports.Count) { throw 'Windows no ve ningun COM. Conecta la placa por USB con un cable de datos y vuelve a ejecutar el BAT.' }
    throw 'El COM de la ESP32-S3 es ambiguo. Deja conectada solo esa placa o indica su puerto, por ejemplo: cargar_placa.bat COM7'
}

function Wait-MonitorPort($Original) {
    $deadline = (Get-Date).AddSeconds(20)
    do {
        $ports = @(Get-Ports)
        $match = @()
        if ($Original.Serial) {
            $match = @($ports | Where-Object { $_.Serial -eq $Original.Serial -and $_.Usb -eq $Original.Usb })
        }
        if ($match.Count -eq 1) { return $match[0].Address }
        if ($match.Count -eq 0 -and !$Original.Serial) {
            $match = @($ports | Where-Object { $_.Address -eq $Original.Address -and $_.Usb -eq $Original.Usb })
            if ($match.Count -eq 1) { return $match[0].Address }
            if ($Original.Native) {
                $other = @($ports | Where-Object Native)
                if ($other.Count -eq 1 -and $other[0].Address -notin $portsBefore.Address) { return $other[0].Address }
            }
        }
        Start-Sleep -Milliseconds 400
    } while ((Get-Date) -lt $deadline)
    throw 'La carga termino, pero no se pudo identificar el COM del monitor tras el reinicio.'
}

function Invoke-Arduino([string[]]$Arguments) {
    # Windows PowerShell puede marcar stderr de programas nativos como ErrorRecord.
    $ErrorActionPreference = 'Continue'
    & $cli @cliConfig @Arguments 2>&1 | ForEach-Object { Write-Carga ([string]$_) }
    if ($LASTEXITCODE -ne 0) { throw "Arduino CLI fallo (codigo $LASTEXITCODE). Revisa $runLog." }
}

try {
    $cli = (Get-Command arduino-cli.exe -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1).Source
    if (!$cli) {
        $cli = Join-Path $env:ProgramFiles 'Arduino CLI\arduino-cli.exe'
        if (!(Test-Path -LiteralPath $cli -PathType Leaf)) {
            throw 'No se encontro Arduino CLI. Instala los requisitos con la opcion [4] de ejecutar_simulador.bat.'
        }
    }
    # Arduino IDE y el CLI independiente pueden usar perfiles distintos.
    $cliConfig = @()
    $ideConfig = Join-Path $env:USERPROFILE '.arduinoIDE\arduino-cli.yaml'
    if (Test-Path -LiteralPath $ideConfig -PathType Leaf) {
        $cliConfig = @('--config-file', $ideConfig)
    }
    $corePath = 'packages\esp32\hardware\esp32'
    $defaultArduinoData = "C:\Users\LilianaRoksanneD$([char]0x00ED)az\AppData\Local\Arduino15"
    if ($ArduinoData) {
        $env:ARDUINO_DIRECTORIES_DATA = (Resolve-Path -LiteralPath $ArduinoData).Path
        if (!(Test-Path -LiteralPath (Join-Path $env:ARDUINO_DIRECTORIES_DATA $corePath))) {
            throw "No hay una plataforma ESP32 en $env:ARDUINO_DIRECTORIES_DATA."
        }
    } else {
        $candidates = @(
            $defaultArduinoData
            $env:ARDUINO_DIRECTORIES_DATA
            (Join-Path $env:USERPROFILE 'AppData\Local\Arduino15')
            (Join-Path $env:LOCALAPPDATA 'Arduino15')
        )
        $installed = $candidates | Where-Object {
            $_ -and (Test-Path -LiteralPath (Join-Path $_ $corePath))
        } | Select-Object -First 1
        if ($installed) { $env:ARDUINO_DIRECTORIES_DATA = $installed }
        else { $env:ARDUINO_DIRECTORIES_DATA = $defaultArduinoData }
    }
    Write-Carga "Lanzador: $PSCommandPath"
    Write-Carga "Arduino CLI: $cli"
    Write-Carga "Perfil Windows: $env:USERPROFILE"
    Write-Carga "Plataformas Arduino: $env:ARDUINO_DIRECTORIES_DATA"
    Write-Carga 'Buscando la ESP32-S3 por USB...'
    $portsBefore = @(Get-Ports)
    foreach ($p in $portsBefore) { Write-Carga ("  {0}  USB {1}  {2}" -f $p.Address, $p.Usb, $p.Label) }
    $target = Select-UploadPort $portsBefore
    Write-Carga "Puerto seleccionado: $($target.Address)"
    if ($SoloDetectar) { exit 0 }

    # El USB nativo necesita CDC en el arranque para que Serial siga en ese conector.
    if ($target.Native) { $fqbn += ':USBMode=hwcdc,CDCOnBoot=cdc' }

    if ($SoloLogs) {
        Write-Carga "Solo monitor de logs en $($target.Address) a 115200. Ctrl+C para cerrar."
        Write-Carga "Registros: $serialLog y $latestSerialLog"
        $writer = New-Object IO.StreamWriter($serialLog, $false, $utf8)
        $latestWriter = New-Object IO.StreamWriter($latestSerialLog, $false, $utf8)
        $writer.AutoFlush = $true
        $latestWriter.AutoFlush = $true
        try {
            $ErrorActionPreference = 'Continue'
            & $cli @cliConfig monitor --fqbn $fqbn --port $target.Address --config baudrate=115200 --timestamp 2>&1 |
                ForEach-Object {
                    $line = [string]$_
                    $writer.WriteLine($line)
                    $latestWriter.WriteLine($line)
                    Write-Host $line
                }
            if ($LASTEXITCODE -ne 0) { throw "El monitor termino con codigo $LASTEXITCODE. Revisa $runLog." }
        } finally {
            $writer.Dispose()
            $latestWriter.Dispose()
        }
        exit 0
    }

    $cores = & $cli @cliConfig core list
    $coreExit = $LASTEXITCODE
    $cores | ForEach-Object { Write-Carga ([string]$_) }
    if ($coreExit -ne 0 -or !($cores -match '^esp32:esp32\s')) {
        throw 'Arduino CLI no ve esp32:esp32. Usa -ArduinoData con la carpeta Arduino15 de la instalacion del IDE.'
    }

    $build = Join-Path $root "build_real\usb-$stamp"
    Write-Carga "Compilando $sketchName para $fqbn..."
    if (!$ArduinoLibraries) {
        $localSketchbook = Join-Path (Split-Path -Parent $root) 'Arduino'
        $ArduinoLibraries = Join-Path $localSketchbook 'libraries'
        if (!(Test-Path -LiteralPath $ArduinoLibraries -PathType Container)) {
            throw "No existe $ArduinoLibraries. Crea esa carpeta o indica -ArduinoLibraries."
        }
        $env:ARDUINO_DIRECTORIES_USER = $localSketchbook
    }
    $ArduinoLibraries = (Resolve-Path -LiteralPath $ArduinoLibraries).Path
    Write-Carga "Bibliotecas Arduino: $ArduinoLibraries"
    $compileArgs = @('compile','--fqbn',$fqbn,'--output-dir',$build,'--libraries',$ArduinoLibraries)
    if (!$PruebaPantalla) {
        $compileArgs += @('--build-property','compiler.cpp.extra_flags=-DLILYXM7_SIMULATION_MODE=0')
    }
    Invoke-Arduino ($compileArgs + @($sketch))

    if ($SoloCompilar) { exit 0 }

    $current = @(Get-Ports | Where-Object {
        $_.Address -eq $target.Address -and $_.Usb -eq $target.Usb -and $_.Serial -eq $target.Serial
    })
    if ($current.Count -ne 1) { throw 'La placa cambio de puerto durante la compilacion. Ejecuta el BAT de nuevo para detectarla.' }
    Write-Carga "Cargando y verificando firmware por $($target.Address)..."
    Invoke-Arduino @('upload','--fqbn',$fqbn,'--port',$target.Address,'--input-dir',$build,'--verify',$sketch)

    $monitorPort = Wait-MonitorPort $target
    Write-Carga "Monitor $monitorPort a 115200. Ctrl+C para cerrar. Registros: $serialLog y $latestSerialLog"
    $writer = New-Object IO.StreamWriter($serialLog, $false, $utf8)
    $latestWriter = New-Object IO.StreamWriter($latestSerialLog, $false, $utf8)
    $writer.AutoFlush = $true
    $latestWriter.AutoFlush = $true
    try {
        $ErrorActionPreference = 'Continue'
        & $cli @cliConfig monitor --fqbn $fqbn --port $monitorPort --config baudrate=115200 --timestamp 2>&1 |
            ForEach-Object {
                $line = [string]$_
                $writer.WriteLine($line)
                $latestWriter.WriteLine($line)
                Write-Host $line
            }
        if ($LASTEXITCODE -ne 0) { throw "El monitor termino con codigo $LASTEXITCODE. Revisa $serialLog." }
    } finally {
        $writer.Dispose()
        $latestWriter.Dispose()
    }
} catch {
    Write-Carga "ERROR: $($_.Exception.Message)"
    exit 1
}