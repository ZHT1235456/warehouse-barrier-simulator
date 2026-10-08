from pathlib import Path
import argparse
import shutil
import zipfile

parser = argparse.ArgumentParser(description='Package development-log materials.')
parser.add_argument('--paper', type=Path, required=True, help='Path to the source paper main.tex')
args = parser.parse_args()

root = Path(__file__).resolve().parent.parent
bundle = root / '开发日志素材'
figures = bundle / 'figures'
figures.mkdir(parents=True, exist_ok=True)
names = {'warehouse.png': '01-作业现场.png', 'inventory.png': '02-货架库存.png',
         'training.png': '03-训练分析.png', 'evaluation.png': '04-策略评估.png',
         'handling-closeup.png': '05-装载近景.png'}
for source, target in names.items():
    shutil.copy2(root / 'artifacts/screenshots' / source, figures / target)
for name in ['README.md', 'OPERATIONS_GUIDE.md', 'EXPERIMENT_REPORT.md',
             'Source/WarehouseBarrier/WarehouseMode.h', 'Source/WarehouseBarrier/WarehouseMode.cpp',
             'Source/WarehouseBarrier/WarehouseOperations.cpp', 'Source/WarehouseBarrier/BarrierDashboard.h',
             'Source/WarehouseBarrier/BarrierDashboard.cpp', 'Trainer/main.cpp', 'Trainer/CMakeLists.txt',
             'scripts/verify-operations.ps1', 'scripts/verify-camera.ps1']:
    target = bundle / '工程参考' / name
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(root / name, target)
target = bundle / '工程参考/论文/main.tex'
target.parent.mkdir(parents=True, exist_ok=True)
shutil.copy2(args.paper, target)
data = ['Content/Models/loss.csv', 'Content/Models/summary.csv', 'Content/Models/parity.json',
        'artifacts/operations-results.txt', 'artifacts/camera-results.txt', 'artifacts/cart-speed-results.txt']
data += [f'artifacts/inventory-speed-{speed}.json' for speed in ['0.00', '0.15', '0.30', '0.45']]
for name in data:
    target = bundle / '验证数据' / name
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(root / name, target)
for directory in ['artifacts/warehouse_dynamic', 'artifacts/warehouse_final']:
    for file in (root / directory).rglob('*.csv'):
        target = bundle / '验证数据' / file.relative_to(root)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(file, target)
archive = root.parent / '仓储中心开发日志_Gemini素材包.zip'
with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as z:
    for file in sorted(bundle.rglob('*')):
        if file.is_file():
            z.write(file, '开发日志素材/' + file.relative_to(bundle).as_posix())
print(archive)
