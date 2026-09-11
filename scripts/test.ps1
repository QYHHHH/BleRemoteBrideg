# MiRemoteBridge - every check that does not need hardware
#
#   .\scripts\test.ps1
#
# 1. regenerate firmware\MiRemoteBridge\selftest_vectors.h from the shared JSON
# 2. run the host-side model check: parser, tracker, key map, HID report
#    descriptor, and randomised stuck-key invariants
# 3. compile the firmware so a broken edit cannot go unnoticed
#
# The on-device half of the same suite is the `selftest` console command; see
# docs\TESTING.md.

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '_common.ps1')

try {
    $paths = Get-BridgePaths
    Assert-BridgeToolchain -Paths $paths

    $python = Get-BridgePython
    if (-not $python) {
        Write-Host 'No python interpreter on PATH.' -ForegroundColor Red
        Write-Host 'The firmware still builds without Python; only the host checks need it.' -ForegroundColor Yellow
        exit 1
    }
    Write-Host ("Using python: {0}" -f $python) -ForegroundColor DarkGray

    $failures = 0

    Write-Host ''
    Write-Host '=== 1/4 regenerate firmware assets ===' -ForegroundColor Cyan
    & $python (Join-Path $paths.Root 'tests\tools\gen_vectors.py')
    if ($LASTEXITCODE -ne 0) { $failures++ }
    & $python (Join-Path $paths.Root 'tests\tools\gen_web_page.py')
    if ($LASTEXITCODE -ne 0) { $failures++ }
    & $python (Join-Path $paths.Root 'tests\tools\gen_web_page.py') --check
    if ($LASTEXITCODE -ne 0) { $failures++ }

    Write-Host ''
    Write-Host '=== 2/4 host-side model check ===' -ForegroundColor Cyan
    & $python (Join-Path $paths.Root 'tests\model\check_vectors.py')
    if ($LASTEXITCODE -ne 0) { $failures++ }

    Write-Host ''
    Write-Host '=== 3/4 Web UI source budget ===' -ForegroundColor Cyan
    & $python (Join-Path $paths.Root 'tests\tools\check_web_ui.py') --check-size
    if ($LASTEXITCODE -ne 0) { $failures++ }

    Write-Host ''
    Write-Host '=== 4/4 compile the firmware ===' -ForegroundColor Cyan
    $code = Invoke-BridgeBuild -Paths $paths
    if ($code -ne 0) { $failures++ }

    Write-Host ''
    if ($failures -ne 0) {
        Write-Host ("HOST CHECKS FAILED ({0} step(s) reported an error)" -f $failures) -ForegroundColor Red
        exit 1
    }
    Write-Host 'HOST CHECKS PASSED' -ForegroundColor Green
    Write-Host 'This says nothing about radio behaviour. Run `selftest` on the device' -ForegroundColor Yellow
    Write-Host 'and then work through docs\TESTING.md.' -ForegroundColor Yellow
    exit 0
} catch {
    Write-Host ("ERROR: " + $_.Exception.Message) -ForegroundColor Red
    exit 1
}
