from pathlib import Path
import csv,json,re,statistics,sys
run=Path(sys.argv[1]);p=run/'rear';log=(p/'torch.log').read_text(encoding='utf-8-sig')
assert 'CLIENT_CLOSED clean=1 acquired=' in log and 'errors=0' in log
assert 'TORCH077_RESTORED enabled=0 expected=0' in log
freq=int(re.search(r'QPC_FREQUENCY (\d+)',log)[1])
on=int(re.search(r'TORCH077_ON_DONE enabled=1 qpc=(\d+)',log)[1]);off=int(re.search(r'TORCH077_OFF_DONE enabled=0 qpc=(\d+)',log)[1])
rows=[]
with (p/'frames.csv').open(encoding='utf-8-sig') as f:
 for r in csv.reader(f):
  if not r or r[0]!='FRAME':continue
  rows.append(dict(phase=int(r[1]),index=int(r[2]),source100ns=int(r[3]),qpc=int(r[4]),y=float(r[7])))
for r in rows:r['timeFromOnMs']=(r['qpc']-on)/freq*1000
def describe(items):
 vals=[r['y'] for r in items]
 return dict(n=len(vals),mean=statistics.mean(vals),median=statistics.median(vals),std=statistics.pstdev(vals),min=min(vals),max=max(vals)) if vals else None
baseline=[r for r in rows if on-freq*2<=r['qpc']<on]
window=[r for r in rows if on<=r['qpc']<off+freq]
late=[r for r in rows if r['qpc']>=off+freq*2]
b=describe(baseline);w=describe(window);a=describe(late)
out=dict(status='PASS077_PUBLIC_TORCH_RESPONSE_ANALYZED',run=str(run),apiOnOffRestore=True,nativeClose=json.loads((p/'native-close.json').read_text(encoding='utf-8-sig'))['status'],onIntervalMs=(off-on)/freq*1000,powerSupported='powerSupported=1' in log,baselineLate=b,arrivalWindowOnPlus1000ms=w,afterLate=a,peakDeltaY=w['max']-b['mean'],physicalLEDProven=False,limits=['Public API readback alone does not prove LED emission.','Arrival timing includes sensor/ISP/FrameReader latency.','Luma response needs native exposure/gain/flash metadata correlation; no photodiode measurement.'],frames=rows)
(run/'torch-analysis077.json').write_text(json.dumps(out,indent=2)+'\n')
print(json.dumps({k:v for k,v in out.items() if k!='frames'},indent=2))
