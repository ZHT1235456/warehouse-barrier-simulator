param([string]$TorchRoot=$env:LIBTORCH_ROOT)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
if(!$TorchRoot){throw 'Set LIBTORCH_ROOT or pass -TorchRoot <libtorch-root>.'}
if(!(Test-Path (Join-Path $TorchRoot 'share/cmake/Torch/TorchConfig.cmake'))){throw "libtorch CMake package not found under: $TorchRoot"}
$vswhere="${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$vs){throw 'MSVC C++ build tools not found'}
$cmake=Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
if(!(Test-Path $cmake)){$cmake=(Get-Command cmake -ErrorAction Stop).Source}
& $cmake -S "$root\Trainer" -B "$root\Trainer\build" -G 'Visual Studio 17 2022' -A x64 "-DCMAKE_PREFIX_PATH=$TorchRoot"
if($LASTEXITCODE){throw 'CMake configure failed'}
& $cmake --build "$root\Trainer\build" --config Release --parallel 2
if($LASTEXITCODE){throw 'Trainer build failed'}
