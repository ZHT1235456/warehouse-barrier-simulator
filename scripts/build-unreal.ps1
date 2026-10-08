param([string]$Engine='C:\Program Files\Epic Games\UE_5.8')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
& "$Engine\Engine\Build\BatchFiles\Build.bat" WarehouseBarrierEditor Win64 Development "-Project=$root\WarehouseBarrier.uproject" -WaitMutex -NoHotReloadFromIDE
if($LASTEXITCODE){throw 'Unreal build failed'}
