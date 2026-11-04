from pathlib import Path
import sys,json,hashlib,numpy as np
run=Path(sys.argv[1]);result=json.loads((run/'result.json').read_text(encoding='utf-8-sig'))
assert result['status'].startswith('PASS') and not result['pending']
rows=[]
for s in result['sessions']:
 cap=Path(s['captureDirectory']);log=(cap/'capture.log').read_text(encoding='utf-8-sig')
 assert 'matrix=2 matrixDefault=false range=1 rangeDefault=false' in log
 for p in sorted(cap.glob('frame-*.bin')):
  meta=json.loads(p.with_suffix('.json').read_text());data=p.read_bytes()
  assert len(data)==meta['currentBytes']==3110400
  assert meta['sameSample'] and meta['captureOnly'] and meta['layoutValid'] and meta['matrix']==2 and meta['range']==1
  fnv=1469598103934665603
  for byte in data:fnv=((fnv^byte)*1099511628211)&0xffffffffffffffff
  assert fnv==int(meta['rawFnv64'])
  y=np.frombuffer(data,dtype=np.uint8,count=2073600);uv=np.frombuffer(data,dtype=np.uint8,offset=2073600).reshape(-1,2)
  rows.append(dict(camera=s['camera'],frame=meta['frame'],path=str(p),bytes=len(data),sha256=hashlib.sha256(data).hexdigest(),fullFNVVerified=True,allBytesZero=not any(data),yMean=float(y.mean()),yStd=float(y.std()),yMin=int(y.min()),yMax=int(y.max()),uvMean=[float(uv[:,i].mean()) for i in (0,1)]))
  assert not rows[-1]['allBytesZero']
out=dict(status='PASS080_RETAINED_RAW_FULL_FNV_NATIVE_NV12',run=str(run),frames=rows,declaredMatrix=2,declaredRange=1,MFTypeAttributesWerePresent=True,qualityRanked=False,reason='Room dark; checksum/format/nonzero proof only. No daylight color or focus parity claim.')
(run/'verified-profile').mkdir(exist_ok=True)
(run/'verified-profile/raw-proof080.json').write_text(json.dumps(out,indent=2)+'\n')
print(json.dumps(dict(status=out['status'],frames=len(rows),allFullFNVVerified=True,yMeans=[round(r['yMean'],3) for r in rows]),indent=2))
