from pathlib import Path
import json,re,datetime,statistics
B=Path(__file__).resolve().parents[1];R=B.parent;rows=[]
for label,root,number in [('before075',R/'Build079',79),('after080',B,80)]:
 for camera in ('front','rear'):
  d=json.loads((root/f'research/kernel-events-{camera}{number:03}.json').read_text());rs=[]
  for r in d['events']:
   m=re.search(r'BuildFrameMetadata requestId = (\d+), portId = (0x[0-9a-f]+)',r['message'])
   if m and m[2]=='0x300d':rs.append(dict(id=int(m[1]),time=datetime.datetime.fromisoformat(r['time']).timestamp()))
  gaps=[(b['time']-a['time'])*1000 for a,b in zip(rs,rs[1:])]
  rows.append(dict(label=label,camera=camera,sofFPS=d['sof']['wallFPS'],metadata300dFPS=(len(rs)-1)/(rs[-1]['time']-rs[0]['time']),metadataUniqueRequests=len(set(r['id'] for r in rs)),metadataOver50MS=sum(x>50 for x in gaps),metadataMaxGapMS=max(gaps)))
assert all(r['metadata300dFPS']>29.5 for r in rows if r['label']=='after080')
out=dict(status='PASS080_KERNEL_REQUEST_METADATA_NEAR_SENSOR_30FPS',rows=rows,limits=['Linux NABU defines0x300d as BF statistics,0x300e as AWB_BG; do not label0x300d final video output.','Only within-file XML intervals; XML +04:59 offset is not used to align CPU timestamps.','Request metadata cadence supports removal of request backpressure but does not measure optical latency.'])
(B/'research/flow080.json').write_text(json.dumps(out,indent=2)+'\n');print(json.dumps(out,indent=2))
