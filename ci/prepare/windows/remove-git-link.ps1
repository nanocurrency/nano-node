$ErrorActionPreference = "Stop"

# Git's link.exe shadows the MSVC linker when bash puts /usr/bin first on PATH.
$gitLink = "C:\Program Files\Git\usr\bin\link.exe"
if (Test-Path $gitLink) {
    Write-Host "Removing Git link.exe to avoid shadowing MSVC linker: $gitLink"
    Remove-Item $gitLink -Force -ErrorAction Stop
}
