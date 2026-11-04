from pathlib import Path
import json,re
R=Path(r'H:\Vlad\Codex\QualcommISP\Build075')
runs=[]
for time,depth in [('004552',4),('004928',2)]:
 p=R/f'results/Video-default-20261024-{time}'
 j=json.loads((p/'result.json').read_text(encoding='utf-8-sig'))
 assert j['status'].startswith('PASS') and not j['pending']
 rows=[]
 for s in j['sessions']:
  assert s['nativeCloseEvidencePassed'] and s['exit']==0
  line=s['resultLine'];row={'camera':s['camera']}
  for name in ['unique_fps','timestamp_fps','max_gap_ms','read_failures','duplicates']:
   row[name]=float(re.search(r'\b'+name+r'=([\d.]+)',line)[1])
  rows.append(row)
 runs.append(dict(queueDepth=depth,path=str(p),rows=rows))
report=dict(status='PASS075_DEPTH_REVERSE_CONTROL_COMPLETE_NO_CONSISTENT_GAIN',runs=runs,decision='Keep stock depth2; depth4 does not consistently outperform reverse depth2. Earlier apparent improvement is not causal evidence.',deviceChanges=False,parentDepth4Journal='FAILED after child PASS due stale latest-profile pointer; preserved without relabelling',installedDriverImprovementClaimed=False)
(R/'research/depth4075-review.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
