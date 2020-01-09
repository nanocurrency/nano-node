# Stop immediately if any error happens
$ErrorActionPreference = "Stop"

if (${env:artifact} -eq 1) {
    if ( ${env:BETA} -eq 1 ) {
        $env:NETWORK_CFG = "beta"
        $env:BUILD_TYPE = "RelWithDebInfo"
    } else {
        $env:NETWORK_CFG = "live"
        $env:BUILD_TYPE = "Release"
    }
    $env:ROCKS_LIB = '-DROCKSDB_LIBRARIES="c:\vcpkg\installed\x64-windows-static\lib\rocksdb.lib"'
    $env:NANO_TEST = "-DNANO_TEST=OFF"
    $env:TRAVIS_TAG = ${env:TAG}
    
    $env:CI = "-DCI_BUILD=ON"
    $env:RUN = "artifact"
} else {
    if ( ${env:RELEASE} -eq 1 ) {
        $env:BUILD_TYPE = "RelWithDebInfo"
        $env:ROCKS_LIB = '-DROCKSDB_LIBRARIES="c:\vcpkg\installed\x64-windows-static\lib\rocksdb.lib"'
    } else { 
        $env:BUILD_TYPE = "Debug"
        $env:ROCKS_LIB = '-DROCKSDB_LIBRARIES="c:\vcpkg\installed\x64-windows-static\debug\lib\rocksdbd.lib"'
    }
    $env:NETWORK_CFG = "test"
    $env:NANO_TEST = "-DNANO_TEST=ON"
    $env:CI = "-DCI_BUILD=OFF"
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
    Invoke-WebRequest -Uri https://aka.ms/vs/15/release/vc_redist.x64.exe -OutFile "$p\vc_redist.x64.exe"
}

& ..\ci\actions\windows\build.bat
if (${LastExitCode} -ne 0) {
    throw "Failed to build ${env:RUN}"
}
$env:cmake_path = Split-Path -Path(get-command cmake.exe).Path
. "$PSScriptRoot\signing.ps1"

& ..\ci\actions\windows\run.bat
if (${LastExitCode} -ne 0) {
    throw "Failed to Pass Test ${env:RUN}"
}

Pop-Location