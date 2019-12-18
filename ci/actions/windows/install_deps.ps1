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
$rocksdb_url = Get-RedirectedUri "https://repo.nano.org/artifacts/rocksdb-msvc14.1-latest.7z"
$qt5base_url = Get-RedirectedUri "https://download.qt.io/online/qtsdkrepository/windows_x86/desktop/qt5_5131/qt.qt5.5131.win64_msvc2017_64/5.13.1-0-201909031231qtbase-Windows-Windows_10-MSVC2017-Windows-Windows_10-X86_64.7z"
$qt5winextra_url = Get-RedirectedUri "https://download.qt.io/online/qtsdkrepository/windows_x86/desktop/qt5_5131/qt.qt5.5131.win64_msvc2017_64/5.13.1-0-201909031231qtwinextras-Windows-Windows_10-MSVC2017-Windows-Windows_10-X86_64.7z"
$rocksdb_artifact = "${env:TMP}\rocksdb.7z"
$qt5base_artifact = "${env:TMP}\qt5base.7z"
$qt5winextra_artifact = "${env:TMP}\qt5winextra.7z"

(New-Object System.Net.WebClient).DownloadFile($qt5base_url, $qt5base_artifact)
(New-Object System.Net.WebClient).DownloadFile($qt5winextra_url, $qt5winextra_artifact)
mkdir $qt5_root
Push-Location $qt5_root
7z x "${env:TMP}\qt5*.7z"
Pop-Location

Push-Location ${env:VCPKG_INSTALLATION_ROOT} 
(New-Object System.Net.WebClient).DownloadFile($rocksdb_url, $rocksdb_artifact)
7z x $rocksdb_artifact
Pop-Location 