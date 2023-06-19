$ErrorActionPreference = "Continue"

if (${env:artifact} -eq 1) {
    $env:BUILD_TYPE = "Release"
    if ( ${env:NETWORK} -eq "BETA" ) {
        $env:NETWORK_CFG = "beta"
        $env:BUILD_TYPE = "RelWithDebInfo"
    }
    elseif (${env:NETWORK} -eq "TEST") {
        $env:NETWORK_CFG = "test"
    }
    else {
        $env:NETWORK_CFG = "live"
    }
    $env:NANO_TEST = "-DNANO_TEST=OFF"
    if ([string]::IsNullOrEmpty(${env:VERSION_PRE_RELEASE})) {
        $env:CI_VERSION_PRE_RELEASE = "OFF"
    } else {
        $env:CI_VERSION_PRE_RELEASE = ${env:VERSION_PRE_RELEASE}
    }
    $env:CI = "-DCI_TAG=${env:TAG} -DCI_VERSION_PRE_RELEASE=${env:CI_VERSION_PRE_RELEASE}"
    $env:RUN = "artifact"
}
else {
    if (${env:RELEASE} -eq "true") {
        $env:BUILD_TYPE = "RelWithDebInfo"
    }
    else {
        $env:BUILD_TYPE = "Debug"
    }
    $env:NETWORK_CFG = "dev"
    $env:NANO_TEST = "-DNANO_TEST=ON"
    $env:RUN = "test"
}

mkdir build
Push-Location build

& ..\ci\actions\windows\configure.bat
if (${LastExitCode} -ne 0) {
    throw "Failed to configure"
}

if (${env:RUN} -eq "artifact") {
    $p = Get-Location
    Invoke-WebRequest -Uri https://aka.ms/vs/16/release/vc_redist.x64.exe -OutFile "$p\vc_redist.x64.exe"
}

$env:cmake_path = Split-Path -Path(get-command cmake.exe).Path

& ..\ci\actions\windows\build.bat
if (${LastExitCode} -ne 0) {
    throw "Failed to build ${env:RUN}"
}

# TODO: fix the signing script.
#. "$PSScriptRoot\signing.ps1"

Pop-Location
