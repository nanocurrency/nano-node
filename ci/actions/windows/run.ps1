$ErrorActionPreference = "Continue"

$env:BUILD_TYPE = "Debug"
$env:NETWORK_CFG = "dev"

Push-Location build

& ..\ci\actions\windows\run.bat
if (${LastExitCode} -ne 0) {
    throw "Failed to Pass Tests"
}

Pop-Location