# MiRemoteBridge - flash the firmware to the board
#
#   .\scripts\flash.ps1                       # detect and report ports, then stop
#   .\scripts\flash.ps1 -Port COM3            # explicit confirmation, probe, build, upload
#   .\scripts\flash.ps1 -Port COM3 -ProbeOnly # read-only chip identification
#   .\scripts\flash.ps1 -Port COM3 -SkipBuild
#
# This script never guesses a COM port. Without -Port it lists what is really
# attached and stops; only an explicitly given port is ever uploaded to.
# Every outcome is written to build\last-flash.txt, the upload output to
# build\last-upload.log.

param(
    [string] $Port = '',
    [switch] $SkipBuild,
    [switch] $CdcOnBoot,
    [switch] $ProbeOnly,
    [int]    $Baud = 921600
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '_common.ps1')

$statusFile = $null

function Set-FlashStatus {
    param([string] $Text)
    if ($statusFile) { $Text | Out-File -FilePath $statusFile -Encoding utf8 }
    Write-Host ''
    Write-Host $Text -ForegroundColor Cyan
}

try {
    $paths = Get-BridgePaths
    Assert-BridgeToolchain -Paths $paths
    $statusFile = Join-Path $paths.BuildDir 'last-flash.txt'

    # -----------------------------------------------------------------------
    # 1. Report exactly what is attached.
    # -----------------------------------------------------------------------
    Write-Host 'Detected serial ports:' -ForegroundColor Cyan
    $ports = Get-BridgeSerialPorts -Paths $paths
    if ($ports.Count -eq 0) {
        Write-Host '  (none)' -ForegroundColor Yellow
    } else {
        $ports | Format-Table Address, Protocol, Vid, Pid, Serial -AutoSize | Out-String | Write-Host
    }

    if ([string]::IsNullOrWhiteSpace($Port)) {
        if ($ports.Count -eq 1) {
            $hint = "Exactly one serial port is present. Confirm it and re-run:`n    .\scripts\flash.ps1 -Port $($ports[0].Address)"
        } elseif ($ports.Count -gt 1) {
            $hint = 'More than one serial port is present. Identify the ESP32-C3 one and pass it with -Port.'
        } else {
            $hint = 'No serial port was found. Check the cable, the board power and the USB-to-UART driver.'
        }
        Set-FlashStatus ("NOT FLASHED: no -Port was given.`n" + $hint)
        exit 2
    }

    # -----------------------------------------------------------------------
    # 2. Read-only probe: does an ESP32-C3 actually answer on that port?
    # -----------------------------------------------------------------------
    $esptool = Get-BridgeEsptool
    if ($esptool) {
        Write-Host ("Probing {0} for an Espressif chip (read-only, nothing is written)..." -f $Port) -ForegroundColor Cyan
        $probe = & $esptool --port $Port --baud 115200 chip-id 2>&1 | Out-String
        Write-Host $probe
        # esptool 4.x prints "Chip is ESP32-C3"; 5.x prints "Chip type: ESP32-C3
        # (QFN32)" (and warns that the C3 has no chip ID, reading the MAC
        # instead). Matching only the 4.x form made every probe fail on 5.x with
        # "no Espressif chip answered", on a board that was fine.
        $chip = $null
        foreach ($pattern in @('Chip is (ESP32-[A-Za-z0-9]+)',
                               'Chip type:\s*(ESP32-[A-Za-z0-9]+)',
                               'Detecting chip type\.+\s*(ESP32-[A-Za-z0-9]+)')) {
            if ($probe -match $pattern) { $chip = $Matches[1]; break }
        }
        if ($chip) {
            Write-Host ("Chip detected: {0}" -f $chip) -ForegroundColor Green
            if ($chip -ne 'ESP32-C3') {
                Set-FlashStatus ("NOT FLASHED: {0} answers on {1}, but this firmware targets ESP32-C3." -f $chip, $Port)
                exit 3
            }
        } else {
            $msg = "NOT FLASHED: no Espressif chip answered on $Port.`n`n" +
                   "The port exists but could not be opened or did not respond. Common causes:`n" +
                   "  * the board is not powered (charge-only cable, unpowered hub);`n" +
                   "  * another program holds the port (serial monitor, Arduino IDE, PuTTY);`n" +
                   "  * a driver problem - check with: Get-PnpDevice -Class Ports | Format-List"
            Set-FlashStatus $msg
            exit 4
        }
    } else {
        Write-Host 'esptool not found; skipping the chip probe.' -ForegroundColor Yellow
    }

    if ($ProbeOnly) {
        Set-FlashStatus ("PROBE OK on {0}" -f $Port)
        exit 0
    }

    # -----------------------------------------------------------------------
    # 3. Build (unless skipped) and upload.
    # -----------------------------------------------------------------------
    if (-not $SkipBuild) {
        $code = Invoke-BridgeBuild -Paths $paths -CdcOnBoot:$CdcOnBoot
        if ($code -ne 0) {
            Set-FlashStatus ("NOT FLASHED: the build failed (exit {0})." -f $code)
            exit $code
        }
    }

    if (-not (Test-Path -LiteralPath (Join-Path $paths.OutputDir 'MiRemoteBridge.ino.bin'))) {
        Set-FlashStatus ("NOT FLASHED: no compiled binary in {0}. Run .\scripts\build.ps1 first." -f $paths.OutputDir)
        exit 5
    }

    Write-Host ''
    Write-Host ("Uploading to {0} ..." -f $Port) -ForegroundColor Cyan
    $uploadLog = Join-Path $paths.BuildDir 'last-upload.log'
    & $paths.Cli --config-file $paths.Config upload -p $Port --fqbn $paths.Fqbn --input-dir $paths.OutputDir $paths.Sketch 2>&1 |
        Tee-Object -FilePath $uploadLog
    $code = $LASTEXITCODE

    if ($code -ne 0) {
        Set-FlashStatus ("UPLOAD FAILED on {0} (exit {1}). Log: {2}" -f $Port, $code, $uploadLog)
        Write-Host 'If the board refuses to enter download mode:' -ForegroundColor Yellow
        Write-Host '  1. close anything holding the port;' -ForegroundColor Yellow
        Write-Host '  2. hold BOOT, tap RESET, release BOOT, then re-run;' -ForegroundColor Yellow
        Write-Host '  3. retry with -Baud 115200.' -ForegroundColor Yellow
        exit $code
    }

    Set-FlashStatus ("UPLOAD OK on {0}. Next: .\scripts\monitor.ps1 -Port {0}" -f $Port)
    exit 0
} catch {
    Write-Host ("ERROR: " + $_.Exception.Message) -ForegroundColor Red
    exit 1
}
