param([string]$Engine='C:\Program Files\Epic Games\UE_5.8')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
if(!(Test-Path "$Engine\Engine\Binaries\Win64\UnrealEditor.exe")){throw "Unreal Engine executable not found under: $Engine"}
& "$Engine\Engine\Binaries\Win64\UnrealEditor.exe" "$root\WarehouseBarrier.uproject" -game -windowed -ResX=1440 -ResY=1000 -log
