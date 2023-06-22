$ErrorActionPreference = "Continue"

Set-MpPreference -DisableArchiveScanning $true
Set-MpPreference -DisableRealtimeMonitoring $true
Set-MpPreference -DisableBehaviorMonitoring $true