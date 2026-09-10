param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]] $CliArguments
)

$projectRoot = Split-Path -Parent $PSScriptRoot
$cli = Join-Path $projectRoot '.tools\arduino-cli-1.5.1\arduino-cli.exe'
$config = Join-Path $projectRoot 'arduino-cli.yaml'

if (-not (Test-Path -LiteralPath $cli)) {
    throw "Arduino CLI is not installed at $cli"
}

Push-Location $projectRoot
try {
    & $cli --config-file $config @CliArguments
    exit $LASTEXITCODE
}
finally {
    Pop-Location
}

