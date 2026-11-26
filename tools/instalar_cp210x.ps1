[CmdletBinding()]
param([switch]$SoloVerificar)

$ErrorActionPreference = 'Stop'
try {
    $root = Split-Path -Parent $PSScriptRoot
    $zip = Join-Path $root 'LAFVIN-AIoT-Starter-Kit-main\CP210X\CP210x_Universal_Windows_Driver.zip'
    if (!(Test-Path -LiteralPath $zip -PathType Leaf)) { throw "No se encontro $zip" }

    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    if (!$SoloVerificar -and !$principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        Write-Host 'Acepta la solicitud de administrador para instalar el controlador.'
        $child = Start-Process -FilePath (Join-Path $PSHOME 'powershell.exe') -Verb RunAs -WindowStyle Hidden -Wait -PassThru -ArgumentList ('-NoLogo -NoProfile -ExecutionPolicy Bypass -File "{0}"' -f $PSCommandPath)
        $log = Join-Path $root 'logs\instalar_cp210x.log'
        if (Test-Path -LiteralPath $log) { Get-Content -LiteralPath $log }
        exit $child.ExitCode
    }

    $folder = Join-Path ([IO.Path]::GetTempPath()) ("lilyxm7-cp210x-" + [guid]::NewGuid().ToString('N'))
    Expand-Archive -LiteralPath $zip -DestinationPath $folder
    $inf = Get-ChildItem -LiteralPath $folder -Recurse -Filter silabser.inf -File | Select-Object -First 1
    if (!$inf -or !(Select-String -LiteralPath $inf.FullName -SimpleMatch 'USB\VID_10C4&PID_EA60' -Quiet)) {
        throw 'El paquete no contiene un INF compatible con el CP2102 detectado.'
    }
    Write-Host 'INF compatible encontrado. Windows valida la firma al instalarlo.'
    if ($SoloVerificar) { exit 0 }

    $log = Join-Path $root 'logs\instalar_cp210x.log'
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $log) | Out-Null
    & "$env:WINDIR\System32\pnputil.exe" /add-driver $inf.FullName /install | Tee-Object -FilePath $log
    $code = $LASTEXITCODE
    if ($code -eq 3010) {
        'Windows solicita reiniciar para completar la instalacion.' | Tee-Object -FilePath $log -Append
        exit 0
    }
    if ($code -ne 0) { throw "PnPUtil devolvio $code. Consulta $log." }
    Start-Sleep -Seconds 2
    & "$env:WINDIR\System32\pnputil.exe" /enum-devices /deviceid 'USB\VID_10C4&PID_EA60' /connected |
        Tee-Object -FilePath $log -Append
    $ports = [IO.Ports.SerialPort]::GetPortNames()
    if ($ports.Count) { "Puertos COM visibles: $($ports -join ', ')" | Tee-Object -FilePath $log -Append }
    else { 'Todavia no hay COM. Desconecta y reconecta la placa y ejecuta cargar_placa.bat -SoloDetectar.' | Tee-Object -FilePath $log -Append }
} catch {
    Write-Host "ERROR: $($_.Exception.Message)"
    if ($log) { "ERROR: $($_.Exception.Message)" | Out-File -LiteralPath $log -Append }
    exit 1
}