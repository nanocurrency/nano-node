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
$qt5_root = "c:\qt"
$qt5base_url = Get-RedirectedUri "https://repo.nano.org/artifacts/5.15.2-0-202011130602qtbase-Windows-Windows_10-MSVC2019-Windows-Windows_10-X86_64.7z"
$qt5winextra_url = Get-RedirectedUri "https://repo.nano.org/artifacts/5.15.2-0-202011130602qtwinextras-Windows-Windows_10-MSVC2019-Windows-Windows_10-X86_64.7z"
$qt5base_artifact = "${env:TMP}\qt5base.7z"
$qt5winextra_artifact = "${env:TMP}\qt5winextra.7z"

(New-Object System.Net.WebClient).DownloadFile($qt5base_url, $qt5base_artifact)
(New-Object System.Net.WebClient).DownloadFile($qt5winextra_url, $qt5winextra_artifact)
mkdir $qt5_root
Push-Location $qt5_root
7z x "${env:TMP}\qt5*.7z" -aoa
Pop-Location
