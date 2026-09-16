# MiRemoteBridge - build the firmware
#
#   .\scripts\build.ps1
#   .\scripts\build.ps1 -CdcOnBoot        # boards with only a native USB port
#   .\scripts\build.ps1 -Clean
#   .\scripts\build.ps1 -Version v0.0.4-improv   # debug build, see config.h
#
# A full log is written to build\last-build.log.

param(
    [switch] $CdcOnBoot,
    [switch] $Clean,
    [string] $OutputDir = '',
    [string] $Version = ''
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '_common.ps1')

try {
    $paths = Get-BridgePaths
    Assert-BridgeToolchain -Paths $paths
    $code = Invoke-BridgeBuild -Paths $paths -CdcOnBoot:$CdcOnBoot -Clean:$Clean -OutputDir $OutputDir -Version $Version
    exit $code
} catch {
    Write-Host ("ERROR: " + $_.Exception.Message) -ForegroundColor Red
    exit 1
}
