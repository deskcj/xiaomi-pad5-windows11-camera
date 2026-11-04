from pathlib import Path
import re,json,statistics as st,collections,datetime
R=Path(__file__).resolve().parents[2];B=R/'Build074'
t=json.loads((B/'research/trial074b.json').read_text(encoding='utf-8-sig'));rows=[]
for run in t['runs']:
 if run['mode']!='Video':continue
 for side in ('front','rear'):
  p=Path(run['run']);log=(p/f'capture-{side}/capture.log').read_text()
  stamps=[int(x) for x in re.findall(r'LATENCY063_FRAME.*?timestamp100ns=(\d+)',log)]
  gaps=[(b-a)/10000 for a,b in zip(stamps,stamps[1:])][-100:]
  period=st.median(sorted(gaps)[:max(1,len(gaps)//10)])
  counts=collections.Counter(round(g/period) for g in gaps)
  residual=max(abs(g-round(g/period)*period) for g in gaps)
  row=dict(phase=run['phase'],camera=side,samples=len(stamps),minimumObservedPeriodMs=period,
           lateMedianGapMs=st.median(gaps),lateMaxGapMs=max(gaps),gapCountsByPeriods=dict(counts),maxIntegerPeriodResidualMs=residual,
           inferredMissingPeriods=sum(max(0,n-1)*v for n,v in counts.items()),
           limitation='Inferred from source sample timestamps, not an independent count of hardware SOF interrupts.')
  meta=p/f'mft-{side}-analysis.json'
  if meta.exists():
   m=json.loads(meta.read_text());pub=[x for x in m['rows'] if x['kind']=='metadata'][-100:]
   times=[datetime.datetime.fromisoformat(x['time']) for x in pub]
   delta=[(b-a).total_seconds()*1000 for a,b in zip(times,times[1:])]
   row['nativeMetadata']=dict(samples=len(pub),eventMedianGapMs=st.median(delta),eventMaxGapMs=max(delta),
                              declaredDurationNs=sorted(set(x['duration'] for x in pub)),requests=[pub[0]['request'],pub[-1]['request']])
  rows.append(row)
out=R/'Build075/research/delivery-intervals075.json';out.write_text(json.dumps(dict(status='PASS075_SOURCE_INTERVALS_MEASURED',rows=rows),indent=2)+'\n')
print(json.dumps(rows,indent=2))
