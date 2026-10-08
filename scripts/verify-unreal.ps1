param([string]$Engine='C:\Program Files\Epic Games\UE_5.8')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$editor="$Engine\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
foreach($speed in @(0.0,0.15,0.30,0.45)){
 $value=$speed.ToString('F2',[Globalization.CultureInfo]::InvariantCulture)
 $log="$root\artifacts\verify-speed-$value.log"
 & $editor "$root\WarehouseBarrier.uproject" -game -nullrhi -BarrierVerify "-BarrierSpeed=$value" -unattended -nosplash -stdout *> $log
 if($LASTEXITCODE){throw "UE verification failed at speed $value"}
 $parity=Select-String -Path $log -Pattern 'inference parity passed'
 $episode=Select-String -Path $log -Pattern 'Episode='
 if(!$parity -or !$episode){throw "Missing verification records at speed $value"}
 if($episode.Line -match 'min_margin=(-?\d+\.\d+)'){
  $margin=[double]::Parse($Matches[1],[Globalization.CultureInfo]::InvariantCulture)
  if($margin -lt 0){throw "Collision in navigation at speed $value : margin=$margin"}
 }
 if($episode.Line -match 'py=(-?\d+\.\d+)'){
  $endpoint=[double]::Parse($Matches[1],[Globalization.CultureInfo]::InvariantCulture)
  if($endpoint -lt 4){throw "Navigation goal not reached at speed $value"}
 }
 Write-Output "Speed $value m/s"
 Write-Output $parity.Line
 Write-Output $episode.Line
}
