import csv,pathlib,statistics
root=pathlib.Path(__file__).resolve().parent.parent
def summary(kind):
    with (root/'artifacts'/kind/'summary.csv').open() as f:
        return list(csv.DictReader(f))
def table(rows):
    names={'brake':'基础刹车','staged':'分阶段策略','joint':'受保护联合','full_boundary':'全时边界'}
    result=['|策略|恢复成功|困难恢复|安全回退|控制能量|干预量|门控调用|','|---|---:|---:|---:|---:|---:|---:|']
    for r in rows:
        result.append('|'+names[r['method']]+'|'+ '|'.join(f'{float(r[k])*100:.2f}%' if k not in ('effort','intervention') else f'{float(r[k]):.3f}' for k in ['success','hard_recovery','brake_safe_lost','effort','intervention','gate_usage'])+'|')
    return '\n'.join(result)
pairs=[];seed_means=[]
for seed in range(5):
    base=root/'artifacts/warehouse_dynamic'/f'seed{seed}'
    def values(stage):
        with (base/f'metrics_{stage}.csv').open() as f:
            return {r['domain']:float(r['success']) for r in csv.DictReader(f) if r['method']==stage}
    staged,joint=values('staged'),values('joint')
    ds=[100*(joint[d]-staged[d]) for d in staged]
    pairs.extend(ds);seed_means.append(statistics.mean(ds))
text=f'''# 实验记录：三维仓库中的屏障切换控制

日期：2026-10-07。环境：UE 5.8.3、MSVC 19.44、libtorch 2.13 CPU。

## 完成内容

四组三层货架、两辆缓行推车和一辆自主运输 AGV；斜俯视与跟随相机；仓库仿真、训练曲线、评估指标三个界面。训练采用独立 C++ libtorch 程序，UE 原生 C++ 读取导出权重。

动态实验：五个随机种子，每个种子 A/B/C/D 各600次更新，切换方向预训练150次；13个测试环境，共65个配对。移动推车训练速度0.15—0.40 m/s，位置和速度进入30维环境参数，h/s输入34维。原始曲线、模型和评估记录位于 artifacts/warehouse_dynamic/seed0 至 seed4。

## 动态仓库

{table(summary('warehouse_dynamic'))}

联合策略相对分阶段策略平均提高 {statistics.mean(pairs):.3f} 个百分点，65个配对中 {sum(d>0 for d in pairs)} 个提高。五个种子的平均提升依次为 {', '.join(f'{v:.3f}' for v in seed_means)} 个百分点。

## 静态仓库

{table(summary('warehouse_final'))}

## 运行验证

模型导出与 UE 推理的最大绝对误差为 3.44×10⁻⁷。停止、0.15、0.30、0.45 m/s四种推车速度均完成同一运输路线，最小离散安全裕度均为0.0637 m，任务用时13.80—14.10 s。完整记录：artifacts/verification-results.txt。

运输任务采用下侧通道至右侧通道两段路线；名义动作以三秒备份预测检查，预测同时推进机器人和移动推车。recovery表格使用论文恢复策略协议，导航任务记录单独保存。

## 文件

- Start-Warehouse.cmd：启动三维应用。
- Trainer/main.cpp：四阶段学习、动态 rollout、评估和模型导出。
- Source/WarehouseBarrier/：三维场景、运动学、模型推理和 Slate 多界面。
- Content/Models/：部署权重、推理核对样本、训练曲线与65配对汇总。
- artifacts/screenshots/：三个界面的 UE 实际截图。
- README.md：参数、指标定义与重跑命令。
'''
(root/'EXPERIMENT_REPORT.md').write_text(text,encoding='utf-8')
print(root/'EXPERIMENT_REPORT.md')
