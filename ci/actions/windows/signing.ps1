$ErrorActionPreference = "Continue"

if (Test-Path env:CSC_LINK) {
  $path = Join-Path -Path "$env:TMP" -ChildPath csc.p12
  [IO.File]::WriteAllBytes($path, [Convert]::FromBase64String($env:CSC_LINK))
  $arguments = -split 'sign /a /ph /tr http://timestamp.digicert.com /td sha256 /fd sha256'
  $arguments += @('/f', $path, '/p', $env:CSC_KEY_PASSWORD, "$env:BUILD_TYPE\*.exe")
  . "C:\Program Files (x86)\Windows Kits\10\App Certification Kit\signtool.exe" $arguments
}