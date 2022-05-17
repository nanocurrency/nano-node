$ErrorActionPreference = "Continue"

if ( ${env:BETA} -eq 1 ) {
    $network_cfg = "beta"
}
elseif ( ${env:TEST} -eq 1 ) {
    $network_cfg = "test"
}
else {
    $network_cfg = "live"
}

if ( ${env:GITHUB_REPOSITORY} == "nanocurrency/nano-node" ) {
    $s3_bucket="repo.nano.org"
}
else {
    $s3_bucket="private-build-repo"
}

$exe = Resolve-Path -Path $env:GITHUB_WORKSPACE\build\nano-node-*-win64.exe
$zip = Resolve-Path -Path $env:GITHUB_WORKSPACE\build\nano-node-*-win64.zip

((Get-FileHash $exe).hash)+" "+(split-path -Path $exe -Resolve -leaf) | Out-file -FilePath "$exe.sha256"
((Get-FileHash $zip).hash)+" "+(split-path -Path $zip -Resolve -leaf) | Out-file -FilePath "$zip.sha256"

aws s3 cp $exe s3://$s3_bucket/$network_cfg/binaries/nano-node-$env:TAG-win64.exe --grants read=uri=http://acs.amazonaws.com/groups/global/AllUsers
aws s3 cp "$exe.sha256" s3://$s3_bucket/$network_cfg/binaries/nano-node-$env:TAG-win64.exe.sha256 --grants read=uri=http://acs.amazonaws.com/groups/global/AllUsers
aws s3 cp "$zip" s3://$s3_bucket/$network_cfg/binaries/nano-node-$env:TAG-win64.zip --grants read=uri=http://acs.amazonaws.com/groups/global/AllUsers
aws s3 cp "$zip.sha256" s3://$s3_bucket/$network_cfg/binaries/nano-node-$env:TAG-win64.zip.sha256 --grants read=uri=http://acs.amazonaws.com/groups/global/AllUsers