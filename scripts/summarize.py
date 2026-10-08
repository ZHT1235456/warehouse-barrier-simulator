"""Only stdlib; no Python torch import or training."""
import argparse,csv,json,pathlib,statistics
p=argparse.ArgumentParser();p.add_argument('directory');a=p.parse_args()
root=pathlib.Path(a.directory)
rows=[]
for f in sorted(root.rglob('metrics_*.csv')):
    model=f.parent/'model.json'
    if not model.exists() or json.loads(model.read_text())['smoke']:
        continue
    with f.open() as h:
        for r in csv.DictReader(h):
            r['run']=f.parent.name
            rows.append(r)
fields=['success','hard_recovery','brake_safe_lost','effort','intervention','gate_usage']
groups={}
for r in rows:
    # Baselines are repeated across stage files; deduplicate exact seed/domain/run/method.
    key=(r['run'],r['seed'],r['domain'],r['method'])
    groups[key]=r
summary=[]
for name in sorted({r['method'] for r in groups.values()}):
    rs=[r for r in groups.values() if r['method']==name]
    summary.append({'method':name,'pairs':len(rs),**{k:statistics.mean(float(r[k]) for r in rs) for k in fields}})
with (root/'summary.csv').open('w',newline='') as f:
    w=csv.DictWriter(f,fieldnames=['method','pairs']+fields);w.writeheader();w.writerows(summary)
print(json.dumps(summary,indent=2))
