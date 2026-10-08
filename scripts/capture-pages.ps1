param([string]$Engine='C:\Program Files\Epic Games\UE_5.8')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$destination="$root\artifacts\screenshots"
New-Item $destination -ItemType Directory -Force | Out-Null
$names=@('warehouse','inventory','training','evaluation')
for($page=0;$page -lt 4;$page++){
 $captureArgs=@('-game','-RenderOffscreen','-BarrierCapture',"-BarrierPage=$page",'-unattended','-nosplash','-stdout')
 if($page -eq 0){$captureArgs+='-WorkCapture'}
 if($page -eq 1){$captureArgs+='-SortedCapture'}
 & "$Engine\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$root\WarehouseBarrier.uproject" @captureArgs *> "$root\artifacts\capture-page-$page.log"
 if($LASTEXITCODE){throw "Capture failed: page $page"}
 $latest=Get-ChildItem "$root\Saved\Screenshots\WindowsEditor" -Filter 'ScreenShot*.png' | Sort-Object LastWriteTime -Descending | Select-Object -First 1
 if(!$latest){throw 'Screenshot missing'}
 Copy-Item $latest.FullName "$destination\$($names[$page]).png" -Force
}
