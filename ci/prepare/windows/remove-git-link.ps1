$ErrorActionPreference = "Continue"

# Git's link.exe shadows the MSVC linker when bash puts /usr/bin first on PATH.
$gitLink = "C:\Program Files\Git\usr\bin\link.exe"
if (Test-Path $gitLink) {
    Remove-Item $gitLink -Force
}
