# Stop immediately if any error happens
$ErrorActionPreference = "Stop"

if ( ${env:BETA} -eq 1 ) {
    $network_cfg="beta"
} else {
    $network_cfg="live"
}

$exe=Resolve-Path -Path $env:GITHUB_WORKSPACE\build\nano-node-*-win64.exe
$zip=Resolve-Path -Path $env:GITHUB_WORKSPACE\build\nano-node-*-win64.zip
aws s3 cp $exe s3://repo.nano.org/$network_cfg/binaries/nano-node-$env:TAG-win64.exe --grants read=uri=http://acs.amazonaws.com/groups/global/AllUsers
aws s3 cp "$zip" s3://repo.nano.org/$network_cfg/binaries/nano-node-$env:TAG-win64.zip --grants read=uri=http://acs.amazonaws.com/groups/global/AllUsers