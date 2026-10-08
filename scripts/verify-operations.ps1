param([string]$Engine='C:\Program Files\Epic Games\UE_5.8')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$results=@()
foreach($speed in @(0.0,0.15,0.30,0.45)){
 $value=$speed.ToString('F2',[Globalization.CultureInfo]::InvariantCulture)
 $log="$root\artifacts\operations-speed-$value.log"
 & "$Engine\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$root\WarehouseBarrier.uproject" -game -nullrhi -OpsVerify "-BarrierSpeed=$value" -unattended -nosplash -stdout *> $log
 if($LASTEXITCODE){throw "Execution failed: $value"}
 $complete=Select-String -Path $log -Pattern 'OPS_COMPLETE'
 if(!$complete){throw "Incomplete operations: $value"}
 if($complete.Line -notmatch 'min_margin=(\d+\.\d+)'){throw "Invalid safety margin: $value"}
 $inventory=Get-Content "$root\Saved\verification_inventory.json" -Raw | ConvertFrom-Json
 if($inventory.completed_jobs -ne 9 -or $inventory.inventory.Count -ne 12){throw 'Job or cargo count mismatch'}
 $slots=@($inventory.inventory.slot | Sort-Object -Unique)
 if($slots.Count -ne 12 -or $slots[0] -ne 0 -or $slots[11] -ne 11){throw 'Inventory has duplicate or missing slots'}
 foreach($item in $inventory.inventory){$expected=[string][char](65+[int][Math]::Floor($item.slot/3));if($item.sku -ne $expected){throw 'Cargo sorting mismatch'}}
 Copy-Item "$root\Saved\verification_inventory.json" "$root\artifacts\inventory-speed-$value.json" -Force
 $results+="speed=$value m/s : $($complete.Line)"
 $results | Set-Content "$root\artifacts\operations-results.txt" -Encoding utf8
 Write-Output $results[-1]
}
