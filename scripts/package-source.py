import pathlib,zipfile
root=pathlib.Path(__file__).resolve().parent.parent
destination=root.parent/'WarehouseBarrier-source.zip'
items=['WarehouseBarrier.uproject','Start-Warehouse.cmd','README.md','EXPERIMENT_REPORT.md','OPERATIONS_GUIDE.md','.gitignore','Source','Config','Content','scripts','Trainer/main.cpp','Trainer/CMakeLists.txt','artifacts/screenshots','artifacts/warehouse_dynamic','artifacts/warehouse_final','artifacts/verification-results.txt','artifacts/operations-results.txt','artifacts/camera-results.txt','artifacts/cart-speed-results.txt','artifacts/inventory-speed-0.00.json','artifacts/inventory-speed-0.15.json','artifacts/inventory-speed-0.30.json','artifacts/inventory-speed-0.45.json']
with zipfile.ZipFile(destination,'w',zipfile.ZIP_DEFLATED) as z:
    for item in items:
        p=root/item
        for f in (p.rglob('*') if p.is_dir() else [p]):
            if f.is_file():
                z.write(f,'WarehouseBarrier/'+f.relative_to(root).as_posix())
print(destination)



