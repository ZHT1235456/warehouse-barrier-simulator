param([int]$Steps=600,[int]$Seeds=5,[switch]$Paper,[switch]$Smoke,[switch]$Static)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$exe="$root\Trainer\build\Release\barrier_train.exe"
if(!(Test-Path $exe)){throw 'Run scripts/build-trainer.ps1 first'}
for($seed=0;$seed -lt $Seeds;$seed++){
 $kind=if($Paper){'paper'}elseif($Static){'warehouse'}else{'warehouse_dynamic'}
 $out="$root\artifacts\$kind\seed$seed"
 $arguments=@('--steps',"$Steps",'--seed',"$seed",'--out',$out)
 if($Paper){$arguments+='--paper'}
 if(!$Paper -and !$Static){$arguments+='--dynamic'}
 if($Smoke){$arguments+='--smoke'}
 & $exe @arguments
 if($LASTEXITCODE){throw "Training failed for seed $seed"}
}
if(!$Paper){
 python "$root\scripts\summarize.py" "$root\artifacts\$kind"
 New-Item "$root\Content\Models" -ItemType Directory -Force | Out-Null
 foreach($file in @('model.json','parity.json','loss.csv','metrics_staged.csv','metrics_joint.csv')){Copy-Item "$root\artifacts\$kind\seed0\$file" "$root\Content\Models\$file" -Force}
 Copy-Item "$root\artifacts\$kind\summary.csv" "$root\Content\Models\summary.csv" -Force
}
