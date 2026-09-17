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
        throw "Arduino CLI not found at $($Paths.Cli). This repository already pins the toolchain (commit 15654b6) - do not reinstall it."
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

function Set-BridgeVersionOverride {
    # Writes (or clears) firmware\MiRemoteBridge\version_local.h, which config.h
    # includes in preference to its tracked BRIDGE_FW_VERSION default.
    #
    # Why a generated header instead of --build-property: passing a quoted C
    # string literal through build.extra_flags means surviving PowerShell 5.1's
    # native-argument re-quoting and arduino-cli's own flag splitting, and it
    # collides with the -CdcOnBoot flags that also use build.extra_flags. A
    # header sidesteps both, and works when the sketch is opened in the IDE.
    #
    # Returns the override that is now in effect ('' when there is none). The
    # caller compares that against what the last build actually used - not
    # against "did I just change the file" - because the file can also be
    # deleted or edited by hand between builds.
    param(
        [hashtable] $Paths,
        [string] $Version = ''
    )

    $file = Join-Path $Paths.Sketch 'version_local.h'

    $wanted = $null
    if (-not [string]::IsNullOrWhiteSpace($Version)) {
        # The suffix may be a Chinese feature name, so the file has to be UTF-8.
        # Set-Content -Encoding utf8 emits a BOM under PowerShell 5.1; GCC
        # accepts one at the head of a source file.
        $wanted = @"
// Generated by scripts\build.ps1 -Version. Not tracked by git - do not edit.
#pragma once
#define BRIDGE_FW_VERSION "$Version"
"@
    }

    # Compare against what is on disk so an unchanged override stays cheap:
    # without this, every -Version build would rewrite the header and pay a
    # full rebuild. Get-Content strips a UTF-8 BOM on read, which is what makes
    # the comparison work.
    $current = $null
    if (Test-Path -LiteralPath $file) {
        $current = Get-Content -LiteralPath $file -Raw -ErrorAction SilentlyContinue
    }

    if ($null -eq $wanted) {
        # Always clear it when no override was asked for. Leaving a stale
        # override behind would make the next plain build report a version that
        # matches nothing in git, which is the exact failure this is meant to
        # prevent.
        if ($null -eq $current) { return '' }
        Remove-Item -LiteralPath $file -Force
        Write-Host 'Version: config.h default (removed the stale version_local.h)' -ForegroundColor DarkYellow
        return ''
    }

    if ($null -eq $current -or $current.Trim() -ne $wanted.Trim()) {
        Set-Content -LiteralPath $file -Value $wanted -Encoding utf8
    }
    Write-Host ("Version override: {0}" -f $Version) -ForegroundColor Yellow
    return $Version
}

function Get-BridgeVersionOverrideMarker {
    # Where the override used by the last build is remembered. Lives under
    # build\ so it is git-ignored and dies with the build cache it describes.
    param([hashtable] $Paths)
    return (Join-Path $Paths.BuildDir 'last-version-override.txt')
}

function Invoke-BridgeBuild {
    param(
        [hashtable] $Paths,
        [switch] $CdcOnBoot,
        [switch] $Clean,
        [string] $OutputDir = '',
        [string] $Version = ''
    )

    if ([string]::IsNullOrWhiteSpace($OutputDir)) { $OutputDir = $Paths.OutputDir }

    # arduino-cli decides whether to recompile an object by checking the
    # dependencies it recorded last time. A version_local.h that has just
    # appeared was in nobody's dependency list, so nothing looks stale and the
    # build silently keeps the previous version string - the exact bug this
    # whole mechanism exists to fix. Whenever the override differs from the one
    # the cached objects were built with, drop the cache and rebuild for real.
    $effective = Set-BridgeVersionOverride -Paths $Paths -Version $Version
    $marker = Get-BridgeVersionOverrideMarker -Paths $Paths
    $previous = ''
    if (Test-Path -LiteralPath $marker) {
        $previous = ((Get-Content -LiteralPath $marker -Raw -ErrorAction SilentlyContinue) -as [string])
        if ($null -eq $previous) { $previous = '' }
        $previous = $previous.Trim()
    }
    $versionChanged = ($previous -ne $effective)

    $arguments = @('--config-file', $Paths.Config, 'compile', '--fqbn', $Paths.Fqbn,
                   '--output-dir', $OutputDir)

    if ($Clean -or $versionChanged) {
        if ($versionChanged -and -not $Clean) {
            $from = if ($previous) { $previous } else { 'config.h default' }
            $to = if ($effective) { $effective } else { 'config.h default' }
            Write-Host ("Version override changed ({0} -> {1}) - forcing a full rebuild so the new string is really in the image." -f $from, $to) -ForegroundColor DarkYellow
        }
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

    # The sketch path has to be passed explicitly. Without it arduino-cli treats
    # the *current directory* as the sketch and looks for MiRemoteBridge.ino
    # there, so `.\scripts\build.ps1` from the repository root dies with
    # "Can't open sketch: main file missing from sketch" - it only ever worked
    # by accident, when the caller happened to be inside the sketch folder.
    # Keep it last: it is the positional argument.
    $arguments += $Paths.Sketch

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

    # Record what the cached objects were built with, so the next run can tell
    # whether it may trust them. Written only after a real binary appeared: a
    # failed build must not claim the override landed, or the retry would skip
    # the rebuild it still needs.
    $effective | Out-File -FilePath $marker -Encoding utf8 -NoNewline

    return 0
}
