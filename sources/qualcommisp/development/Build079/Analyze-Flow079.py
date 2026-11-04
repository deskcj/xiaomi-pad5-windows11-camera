from pathlib import Path
import json,re,collections,statistics,datetime
B=Path(__file__).resolve().parents[1]
def epoch(s): return datetime.datetime.fromisoformat(s).timestamp()
def cadence(rows):
 times=[epoch(r['time']) for r in rows];gaps=[(b-a)*1000 for a,b in zip(times,times[1:])]
 return dict(count=len(rows),fps=(len(rows)-1)/(times[-1]-times[0]) if len(times)>1 else None,gapMedianMS=statistics.median(gaps) if gaps else None,gapsOver50MS=sum(g>50 for g in gaps),maxGapMS=max(gaps) if gaps else None)
rows=[]
for camera in ('front','rear'):
 d=json.loads((B/f'research/kernel-events-{camera}079.json').read_text());ports=collections.defaultdict(list)
 for r in d['events']:
  m=re.search(r'BuildFrameMetadata requestId = (\d+), portId = (0x[0-9a-f]+)',r['message'])
  if m:ports[m[2]].append(dict(time=r['time'],id=int(m[1])))
 sof=[r for r in d['events'] if 'camif_sof_irq' in r['message']]
 out=dict(camera=camera,sof=cadence(sof),ports={p:dict(**cadence(rs),firstId=rs[0]['id'],lastId=rs[-1]['id'],uniqueIds=len(set(r['id'] for r in rs)),first=[r for r in rs[:3]]) for p,rs in ports.items()})
 rows.append(out)
cpu=json.loads((B/'research/cpu-top079d.json').read_text());server=next(p for p in cpu['processes'] if p['pid']==7664)
steady=[]
for camera,start,end in [('front',14,22),('rear',32,40)]:
 for field in ['bins','stackBins']:
  selected=[r for r in server[field] if start<=int(r['location'].split(':')[0])<end];counts=collections.Counter()
  for r in selected:counts[r['location'].split(':',1)[1]]+=r['samples']
  steady.append(dict(camera=camera,group=field,traceSeconds=[start,end],samples=sum(counts.values()),oneCoreFraction=sum(counts.values())/((end-start)*1000),top=counts.most_common(12)))
report=dict(status='PASS079_SOF_AND_DOWNSTREAM_CADENCE_COMPARED',capture='Build079/results/Video-default-20261024-025610',rows=rows,cpuSteady=steady,limits=['SOF measures IFE interrupt cadence, not CSI payload image quality or downstream completion.','Kernel metadata port0x300d and port0 completion are distinct from output pin0x3; identify meanings from native code before patching.','tracerpt XML has a suspect +04:59 timezone offset; only within-file deltas are used. CPU steady bins are relative to ETL session, not aligned by the XML timezone.','No clock or sensor mode change justified by these data.'])
(B/'research/flow079.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
