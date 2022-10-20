$ErrorActionPreference = "Continue"

function Get-RedirectedUri {
    <#
    .SYNOPSIS
        Gets the real download URL from the redirection.
    .DESCRIPTION
        Used to get the real URL for downloading a file, this will not work if downloading the file directly.
    .EXAMPLE
        Get-RedirectedURL -URL "https://download.mozilla.org/?product=firefox-latest&os=win&lang=en-US"
    .PARAMETER URL
        URL for the redirected URL to be un-obfuscated
    .NOTES
        Code from: Redone per issue #2896 in core https://github.com/PowerShell/PowerShell/issues/2896
    #>
 
    [CmdletBinding()]
    param (
        [Parameter(Mandatory = $true)]
        [string]$Uri
    )
    process {
        do {
            try {
                $request = Invoke-WebRequest -Method Head -Uri $Uri
                if ($null -ne $request.BaseResponse.ResponseUri) {
                    # This is for Powershell 5
                    $redirectUri = $request.BaseResponse.ResponseUri.AbsoluteUri
                }
                elseif ($null -ne $request.BaseResponse.RequestMessage.RequestUri) {
                    # This is for Powershell core
                    $redirectUri = $request.BaseResponse.RequestMessage.RequestUri.AbsoluteUri
                }
 
                $retry = $false
            }
            catch {
                if (($_.Exception.GetType() -match "HttpResponseException") -and ($_.Exception -match "302")) {
                    $Uri = $_.Exception.Response.Headers.Location.AbsoluteUri
                    $retry = $true
                }
                else {
                    throw $_
                }
            }
        } while ($retry)
 
        $redirectUri
    }
}
$boost_url = Get-RedirectedUri "https://repo.nano.org/artifacts/boost-msvc14.2-1.70-full.zip"
$BOOST_ROOT = "c:\local\boost_1_70_0"
$qt5_root = "c:\qt"
$qt5base_url = Get-RedirectedUri "https://repo.nano.org/artifacts/5.13.1-0-201909031231qtbase-Windows-Windows_10-MSVC2017-Windows-Windows_10-X86_64.7z"
$qt5winextra_url = Get-RedirectedUri "https://repo.nano.org/artifacts/5.13.1-0-201909031231qtwinextras-Windows-Windows_10-MSVC2017-Windows-Windows_10-X86_64.7z"
$qt5base_artifact = "${env:TMP}\qt5base.7z"
$qt5winextra_artifact = "${env:TMP}\qt5winextra.7z"
$openssl_url = Get-RedirectedUri "https://repo.nano.org/artifacts/OpenSSL-1.1.1q-Win_x64.7z"
$OPENSSL_ROOT_DIR = "c:\local\OpenSSL-1.1.1q-Win_x64"

(New-Object System.Net.WebClient).DownloadFile($qt5base_url, $qt5base_artifact)
(New-Object System.Net.WebClient).DownloadFile($qt5winextra_url, $qt5winextra_artifact)
mkdir $qt5_root
Push-Location $qt5_root
7z x "${env:TMP}\qt5*.7z" -aoa
Pop-Location


mkdir $BOOST_ROOT
Write-Output "BOOST_ROOT=$BOOST_ROOT" | Out-File -FilePath $env:GITHUB_ENV -Encoding utf8 -Append
(New-Object System.Net.WebClient).DownloadFile($boost_url, "${env:TMP}\boost-msvc.zip")
Push-Location $BOOST_ROOT
7z x "${env:TMP}\boost-msvc.zip" -aoa


mkdir $OPENSSL_ROOT_DIR
Write-Output "OPENSSL_ROOT_DIR=$OPENSSL_ROOT_DIR" | Out-File -FilePath $env:GITHUB_ENV -Encoding utf8 -Append
(New-Object System.Net.WebClient).DownloadFile($openssl_url, "${env:TMP}\openssl-win64-msvc.7z")
Push-Location $OPENSSL_ROOT_DIR
7z x "${env:TMP}\openssl-win64-msvc.7z" -aoa
