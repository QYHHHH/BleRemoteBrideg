# MiRemoteBridge - shared helpers for the scripts in this folder.
#
# Dot-source this file; it only defines functions and never calls exit, so an
# entry script can invoke several of them and still control its own exit code.

$script:BridgeProjectRoot = Split-Path -Parent $PSScriptRoot

# PowerShell 5.1 builds a case-insensitive dictionary out of the environment
# every time it starts a child process. If a proxy variable exists in both
# cases (http_proxy AND HTTP_PROXY) that construction throws, and the symptom
# is a build or upload that silently does nothing at all. Drop the duplicate
# before anything else runs.
foreach ($proxyName in @('http_proxy', 'https_proxy', 'no_proxy')) {
    $upper = $proxyName.ToUpperInvariant()
    $hasLower = [System.Environment]::GetEnvironmentVariable($proxyName, 'Process')
    $hasUpper = [System.Environment]::GetEnvironmentVariable($upper, 'Process')
    if ($hasLower -and $hasUpper) {
        [System.Environment]::SetEnvironmentVariable($upper, $null, 'Process')
        Write-Host "Note: removed the duplicate environment variable $upper; it breaks child process creation under PowerShell 5.1." -ForegroundColor DarkYellow
    }
}

function Get-BridgePaths {
    return @{
        Root      = $script:BridgeProjectRoot
        Scripts   = $PSScriptRoot
        Cli       = Join-Path $script:BridgeProjectRoot '.tools\arduino-cli-1.5.1\arduino-cli.exe'
        Config    = Join-Path $script:BridgeProjectRoot 'arduino-cli.yaml'
        Sketch    = Join-Path $script:BridgeProjectRoot 'firmware\MiRemoteBridge'
        OutputDir = Join-Path $script:BridgeProjectRoot 'build\MiRemoteBridge'
        BuildDir  = Join-Path $script:BridgeProjectRoot 'build'
        # FlashMode=dio is REQUIRED on this board, not a preference:
        # the default menu choice builds with build.boot=qio, i.e. the image
        # headers say DIO (so the ROM loads) but the bootloader configures the
        # flash driver in QIO. This board's Macronix flash does not answer in
        # QIO, so every bootloader read returns 0xFF and the chip reset-loops
        # with "partition 0 invalid magic number". Verified on hardware:
        # 80MHz+QIO fails, 40MHz+QIO fails, 80MHz+DIO boots.
        # See docs/TESTING.md section 4.
        # PartitionScheme=huge_app: the Wi-Fi config stack does not fit the
        # default 1.2 MB app partition (docs/TESTING.md section 4.11).
        Fqbn      = 'esp32:esp32:esp32c3:FlashMode=dio,PartitionScheme=huge_app'
    }
}

function Assert-BridgeToolchain {
    param([hashtable] $Paths)
    if (-not (Test-Path -LiteralPath $Paths.Cli)) {
        throw "Arduino CLI not found at $($Paths.Cli). This repository already pins the toolchain (commit 16b3766) - do not reinstall it."
    }
    if (-not (Test-Path -LiteralPath $Paths.Sketch)) {
        throw "Sketch folder not found: $($Paths.Sketch)"
    }
    if (-not (Test-Path -LiteralPath $Paths.BuildDir)) {
        New-Item -ItemType Directory -Path $Paths.BuildDir | Out-Null
    }
}

function Get-BridgeEsptool {
    # The toolchain lives outside the repository on purpose (see arduino-cli.yaml),
    # and the Arduino-ESP32 package can hold several esptool versions.
    $base = Join-Path $script:BridgeProjectRoot '..\arduino-c3-data\data\packages\esp32\tools\esptool_py'
    if (-not (Test-Path -LiteralPath $base)) { return $null }
    return Get-ChildItem -Path $base -Filter 'esptool.exe' -Recurse -ErrorAction SilentlyContinue |
           Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
}

function Get-BridgeSerialPorts {
    param([hashtable] $Paths)
    $ports = @()
    $json = & $Paths.Cli --config-file $Paths.Config board list --format json 2>$null | Out-String
    if ([string]::IsNullOrWhiteSpace($json)) { return $ports }
    try {
        $parsed = $json | ConvertFrom-Json
    } catch {
        return $ports
    }
    foreach ($entry in $parsed.detected_ports) {
        if ($entry.port.protocol -eq 'serial') {
            $ports += [pscustomobject]@{
                Address = $entry.port.address
                Protocol = $entry.port.protocol_label
                Vid     = $entry.port.properties.vid
                Pid     = $entry.port.properties.pid
                Serial  = $entry.port.properties.serialNumber
            }
        }
    }
    return $ports
}

function Get-BridgePython {
    foreach ($candidate in @('python', 'python3', 'py')) {
        $cmd = Get-Command $candidate -ErrorAction SilentlyContinue
        if ($cmd) { return $cmd.Source }
    }
    return $null
}

function Invoke-BridgeBuild {
    param(
        [hashtable] $Paths,
        [switch] $CdcOnBoot,
        [switch] $Clean,
        [string] $OutputDir = ''
    )

    if ([string]::IsNullOrWhiteSpace($OutputDir)) { $OutputDir = $Paths.OutputDir }

    $arguments = @('--config-file', $Paths.Config, 'compile', '--fqbn', $Paths.Fqbn,
                   '--output-dir', $OutputDir)

    if ($Clean) {
        if (Test-Path -LiteralPath $OutputDir) { Remove-Item -LiteralPath $OutputDir -Recurse -Force }
        $arguments += '--clean'
    }

    if ($CdcOnBoot) {
        # On boards with no USB-to-UART bridge chip, `Serial` has to be the
        # native USB CDC port instead of UART0.
        $arguments += '--build-property'
        $arguments += 'build.extra_flags=-DARDUINO_USB_MODE=1 -DARDUINO_USB_CDC_ON_BOOT=1'
        Write-Host 'Build option: USB CDC on boot (native USB serial)' -ForegroundColor Yellow
    }

    Write-Host "Building $($Paths.Sketch)" -ForegroundColor Cyan
    Write-Host "  fqbn   : $($Paths.Fqbn)"
    Write-Host "  output : $OutputDir"
    Write-Host ''

    $logPath = Join-Path $Paths.BuildDir 'last-build.log'
    & $Paths.Cli @arguments 2>&1 | Tee-Object -FilePath $logPath
    $code = $LASTEXITCODE

    if ($code -ne 0) {
        Write-Host ''
        Write-Host "BUILD FAILED (exit $code). Full log: $logPath" -ForegroundColor Red
        return $code
    }

    $bin = Join-Path $OutputDir 'MiRemoteBridge.ino.bin'
    if (Test-Path -LiteralPath $bin) {
        $size = (Get-Item -LiteralPath $bin).Length
        Write-Host ''
        Write-Host 'BUILD OK' -ForegroundColor Green
        Write-Host ("  {0}  ({1} bytes)" -f $bin, $size)
        Write-Host ("  log: {0}" -f $logPath)
    } else {
        Write-Host ''
        Write-Host 'BUILD reported success but no binary was produced.' -ForegroundColor Red
        return 1
    }
    return 0
}
