$ErrorActionPreference = "Stop"

& "$PSScriptRoot\disable-defender.ps1"
& "$PSScriptRoot\install-qt.ps1"
& "$PSScriptRoot\remove-git-link.ps1"