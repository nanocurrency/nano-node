$ErrorActionPreference = "Stop"

# Download and extract Qt library.
# Original files can be found at: https://download.qt.io/online/qtsdkrepository/windows_x86/desktop/qt5_5152/qt.qt5.5152.win64_msvc2019_64/

# Define URLs, file names and their corresponding SHA1 hashes
$toDownload = @(
    @{
        Url = 'https://download.qt.io/online/qtsdkrepository/windows_x86/desktop/qt5_5152/qt.qt5.5152.win64_msvc2019_64/5.15.2-0-202011130602qtbase-Windows-Windows_10-MSVC2019-Windows-Windows_10-X86_64.7z';
        FileName = 'qt5base.7z';
        SHA1 = 'e29464430a2225bce6ce96b4ed18eec3f8b944d6';
    },
    @{
        Url = 'https://download.qt.io/online/qtsdkrepository/windows_x86/desktop/qt5_5152/qt.qt5.5152.win64_msvc2019_64/5.15.2-0-202011130602qtwinextras-Windows-Windows_10-MSVC2019-Windows-Windows_10-X86_64.7z';
        FileName = 'qt5winextra.7z';
        SHA1 = '70da33b18ddeac4dd00ceed205f8110c426cea16';
    }
)

$targetFolder = "C:\Qt"

# Download and process files
foreach ($entry in $toDownload) {
    $tempFile = Join-Path $env:TEMP $entry.FileName

    # Download file
    Invoke-WebRequest -Uri $entry.Url -OutFile $tempFile

    # Calculate file hash
    $fileHash = (Get-FileHash -Path $tempFile -Algorithm SHA1).Hash

    # Compare hashes
    if ($fileHash -eq $entry.SHA1) {
        Write-Host "Hashes match for $($entry.FileName)."
    } else {
        Write-Error "Hashes do not match for $($entry.FileName). Stopping script execution."
        exit 1
    }

    # Decompress archive
    7z x $tempFile -o"$targetFolder" -aoa
}

# Save install location for subsequent steps
"NANO_QT_DIR=C:\Qt\5.15.2\msvc2019_64\lib\cmake\Qt5" >> $env:GITHUB_ENV