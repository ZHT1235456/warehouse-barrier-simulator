param([string]$Engine='C:\Program Files\Epic Games\UE_5.8', [switch]$ExistingLogs)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$results=@()
foreach($fps in @(30,60,144)){
    $log="$root\artifacts\camera-check-$fps.log"
    if(!$ExistingLogs){
        & "$Engine\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$root\WarehouseBarrier.uproject" -game -nullrhi "-CameraCheckFPS=$fps" -unattended -nosplash -stdout *> $log
        if($LASTEXITCODE){throw "Camera run failed: $fps"}
    }
    $match=Select-String -LiteralPath $log -Pattern 'CAMERA_CHECK fps='
    if(!$match -or $match.Line -notmatch 'moving_frames=(\d+) repeated=(\d+) max_display_cm=([\d.]+) max_camera_cm=([\d.]+) max_angle_deg=([\d.]+)'){throw "Camera result missing: $fps"}
    if([int]$Matches[1] -le 0 -or [int]$Matches[2] -ne 0 -or [double]$Matches[3] -gt 110/$fps -or [double]$Matches[4] -gt 120/$fps -or [double]$Matches[5] -gt .001){throw "Discontinuous follow camera: $fps"}
    $results+=$match.Line
}
$results | Set-Content -LiteralPath "$root\artifacts\camera-results.txt" -Encoding utf8
$results
