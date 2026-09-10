# MiRemoteBridge - serial console
#
#   .\scripts\monitor.ps1                          # list the ports and stop
#   .\scripts\monitor.ps1 -Port COM3
#   .\scripts\monitor.ps1 -Port COM3 -Seconds 20   # capture to build\monitor.log
#
# Never guesses a port: without -Port it only reports what is attached.
# Ctrl+C leaves the interactive monitor.

param(
    [string] $Port = '',
    [int]    $Baud = 115200,
    [int]    $Seconds = 0
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '_common.ps1')

try {
    $paths = Get-BridgePaths
    Assert-BridgeToolchain -Paths $paths

    if ([string]::IsNullOrWhiteSpace($Port)) {
        Write-Host 'Detected serial ports:' -ForegroundColor Cyan
        $ports = Get-BridgeSerialPorts -Paths $paths
        if ($ports.Count -eq 0) {
            Write-Host '  (none)' -ForegroundColor Yellow
        } else {
            $ports | Format-Table Address, Protocol, Vid, Pid, Serial -AutoSize | Out-String | Write-Host
            Write-Host 'Pass the port explicitly, for example:' -ForegroundColor Yellow
            Write-Host ("    .\scripts\monitor.ps1 -Port {0}" -f $ports[0].Address) -ForegroundColor White
        }
        exit 2
    }

    if ($Seconds -gt 0) {
        $logPath = Join-Path $paths.BuildDir 'monitor.log'
        Write-Host ("Capturing {0}s from {1} into {2}" -f $Seconds, $Port, $logPath) -ForegroundColor Cyan
        $sp = New-Object System.IO.Ports.SerialPort($Port, $Baud, 'None', 8, 'One')
        $sp.ReadTimeout = 500
        $sp.DtrEnable = $false
        $sp.RtsEnable = $false
        # SerialPort defaults to ASCII, which turns every non-ASCII byte of an
        # advertised BLE name (Xiaomi advertises its localised name) into '?', so
        # a captured log could not be told apart from a device that really has no
        # name. Decode as UTF-8 instead.
        $sp.Encoding = [System.Text.Encoding]::UTF8
        $sp.Open()
        # Write as bytes arrive rather than buffering until the end. A capture
        # that is still running can then be read (or tailed) while it runs, and
        # closing the window early no longer throws away everything recorded so
        # far - which is exactly what happens with a buffer-and-write-at-the-end
        # capture when the thing you are debugging takes longer than the window.
        $writer = New-Object System.IO.StreamWriter($logPath, $false, [System.Text.Encoding]::UTF8)
        $deadline = (Get-Date).AddSeconds($Seconds)
        try {
            while ((Get-Date) -lt $deadline) {
                $chunk = ""
                try { $chunk = $sp.ReadExisting() } catch { }
                if ($chunk.Length -gt 0) {
                    $writer.Write($chunk)
                    $writer.Flush()
                    Write-Host $chunk -NoNewline
                }
                Start-Sleep -Milliseconds 50
            }
        } finally {
            $writer.Flush()
            $writer.Close()
            if ($sp.IsOpen) { $sp.Close() }
        }
        Write-Host ''
        Write-Host ("Saved to {0}" -f $logPath) -ForegroundColor Green
        exit 0
    }

    Write-Host ("Opening {0} at {1} baud. Ctrl+C to exit." -f $Port, $Baud) -ForegroundColor Cyan
    & $paths.Cli --config-file $paths.Config monitor -p $Port --config baudrate=$Baud
    exit $LASTEXITCODE
} catch {
    Write-Host ("ERROR: " + $_.Exception.Message) -ForegroundColor Red
    exit 4
}
