from pathlib import Path
import json,re,hashlib,statistics,collections
import numpy as np
from PIL import Image,ImageDraw
B=Path(__file__).resolve().parents[1];load=lambda p:json.loads(p.read_text(encoding='utf-8-sig'))
t=load(B/'research/trial249.json');assert t['status'].startswith('PASS249') and not t['pending'] and t['ownerCloseVerified'] and t['fullMftDecoded'] and not t['errors']
assert load(B/'research/final-state249.json')['status'].startswith('PASS249')
G=Path(r'H:\Vlad\cam-results\QISP\Build249-Both-AE-ROI');G.mkdir(exist_ok=False)
reports=[]
for runinfo in t['runs']:
 side=runinfo['camera'];run=Path(runinfo['run']);text=(run/'capture.log').read_text();compact=re.sub(r'\s+','',text)
 metrics=[]
 for m in re.finditer(r'ROI247B_METRICphase=(\d+)elapsedMs=(\d+)source100ns=(-?\d+)qpc=(\d+)patchY=([\d.]+)whitePercent=([\d.]+)meanG=([\d.-]+)meanB=([\d.-]+)n=(\d+)',compact):
  metrics.append(dict(phase=int(m[1]),elapsedMs=int(m[2]),source100ns=int(m[3]),qpc=int(m[4]),patchY=float(m[5]),whitePercent=float(m[6])))
 bounds=[]
 for m in re.finditer(r'ROI247B_PHASEphase=(\d+)durationMs=(\d+)',compact):
  q=list(re.finditer(r'qpc=(\d+)',compact[:m.start()]))[-1];bounds.append(dict(phase=int(m[1]),qpc=int(q[1]),durationMs=int(m[2])))
 assert [v['phase'] for v in bounds]==list(range(5))
 phases=[];images={};planes={}
 for phase in [0,1,3,4]:
  p=run/f'phase-{phase}.nv12';raw=p.read_bytes();assert len(raw)==3110400
  a=np.frombuffer(raw,np.uint8);y=a[:1920*1080].reshape(1080,1920).astype(np.float32);uv=a[1920*1080:].reshape(540,960,2).repeat(2,0).repeat(2,1).astype(np.float32)-128
  rgb=np.stack([y+1.402*uv[:,:,1],y-.344136*uv[:,:,0]-.714136*uv[:,:,1],y+1.772*uv[:,:,0]],2)
  im=Image.fromarray(np.clip(np.rint(rgb),0,255).astype(np.uint8));im.save(G/f'{side}-phase-{phase}.jpg',quality=95);images[phase]=im
  box=(1140,520,1410,780) if side=='rear' else (1056,108,1296,243);x0,y0,x1,y1=box;z=y[y0:y1,x0:x1];r=rgb[y0:y1,x0:x1]
  planes[phase]=(z,r)
  stable=[v for v in metrics if v['phase']==phase and v['elapsedMs']>=3000]
  phases.append(dict(phase=phase,raw=str(p),rawSHA=hashlib.sha256(raw).hexdigest(),monitorBox=box,rawMeanY=float(z.mean()),rawYp05=float(np.percentile(z,5)),rawYp95=float(np.percentile(z,95)),rawY255Percent=float((z>=254).mean()*100),rawRG245Percent=float(((r[:,:,0]>=245)&(r[:,:,1]>=245)).mean()*100),rawRG255Percent=float(((r[:,:,0]>=255)&(r[:,:,1]>=255)).mean()*100),stableSamples=len(stable),stableMedianY=statistics.median(v['patchY'] for v in stable) if stable else None,stableYspan=[min(v['patchY'] for v in stable),max(v['patchY'] for v in stable)] if stable else None))
 # Fixed baseline low-Y pixel mask tests whether the ink/background contrast changes.
 z0,r0=planes[0];mask=z0<=np.percentile(z0,8);paper=z0>=np.percentile(z0,60)
 for row in phases:
  z,_=planes[row['phase']];row['baselineFixedMaskInkY']=float(z[mask].mean());row['baselineFixedMaskPaperY']=float(z[paper].mean());row['baselineFixedMaskContrast']=row['baselineFixedMaskPaperY']-row['baselineFixedMaskInkY']
 contact=Image.new('RGB',(1280,768),'#202020');draw=ImageDraw.Draw(contact)
 for i,phase in enumerate([0,1,3,4]):
  x=(i%2)*640;yy=(i//2)*384;draw.text((x+8,yy+6),f'{side} phase{phase} '+{0:'common AE',1:'bright AE ROI',3:'dark AE ROI',4:'clear/common AE'}[phase],fill='white');contact.paste(images[phase].resize((640,360),Image.Resampling.LANCZOS),(x,yy+24))
 contact.save(G/f'{side}-ae-roi249.png')
 if side=='rear':
  crops=Image.new('RGB',(900,828),'#202020');d=ImageDraw.Draw(crops)
  for i,phase in enumerate([0,1,3,4]):
   x=i%2*450;yy=i//2*414;d.text((x+6,yy+6),f'phase{phase} native1:1 no colour edits',fill='white');crops.paste(images[phase].crop((1080,450,1530,840)),(x,yy+24))
  crops.save(G/'rear-smile-ae-crops249.png')
 native=collections.defaultdict(list);counts=collections.Counter();sensors=[];digest=hashlib.sha256()
 for line in (run/'mft249.jsonl').open('rb'):
  digest.update(line);e=json.loads(line);s=e.get('text','');q=e['qpc']
  for label,pattern in [('TouchAE',r'TouchAE|TouchROI|TouchRoi'),('metering',r'MeteringMode|Metering mode'),('crop',r'AppliedCrop|sensorCrop|activeArray')]:
   if re.search(pattern,s,re.I):
    counts[label]+=1
    if len(native[label])<45:native[label].append(dict(qpc=q,text=s))
  m=re.search(r'Publishing metadata: ReqId: (\d+) resID (\d+).*?frameDuration:\s*(\d+), exposureTime:\s*(\d+), sensitivity: (\d+), sensorGain: ([\d.]+), ispDigitalGain:\s*([\d.]+)',s)
  if m:
   phase=next((a['phase'] for a in reversed(bounds) if q>=a['qpc']),-1)
   sensors.append(dict(phase=phase,qpc=q,request=int(m[1]),exposureNs=int(m[4]),analogGain=float(m[6]),ispGain=float(m[7])))
 nativeSummary=[]
 for phase in range(5):
  lo=bounds[phase]['qpc']+3*19200000;hi=(bounds[phase+1]['qpc'] if phase<4 else bounds[phase]['qpc']+8*19200000)
  a=[s for s in sensors if lo<=s['qpc']<hi]
  nativeSummary.append(dict(phase=phase,samples=len(a),exposureNs=sorted({v['exposureNs'] for v in a}),analogGain=sorted({v['analogGain'] for v in a}),ispGainRange=[min(v['ispGain'] for v in a),max(v['ispGain'] for v in a)] if a else None))
 reports.append(dict(camera=side,phases=phases,phaseBoundsQpc=bounds,nativeSensor=nativeSummary,nativeRoiExamples=dict(native),nativeCounts=dict(counts),mftSHA=digest.hexdigest(),fullMftEvents=runinfo['fullMftEvents'],metricFrames=len(metrics)))
r=dict(status='MEASURED249_BOTH_PUBLIC_WINRT_AE_ONLY_ROI_CLEAR_TEST_DATA249',reports=reports,gallery=str(G),driverChanges=False,tuningChanges=False,androidParity=False,limits=['Same scene sequence, not exact same sensor request across phases.','Night/window lighting is not the Android reference illumination.','White-all-RGB is insufficient for yellow clipping; red/green and fixed-mask contrast are separately reported.','Fixed low-Y mask also includes scene texture and is not calibrated ink or optical MTF.','Polling and sparse pixel logging affect delivered count; this is not a speed benchmark.','Native sensor metadata is joined by phase/QPC, not exact retained image request.'])
(B/'research/roi249.json').write_text(json.dumps(r,indent=2)+'\n');(G/'roi249.json').write_text(json.dumps(r,indent=2)+'\n')
print(json.dumps(dict(status=r['status'],reports=[{'camera':v['camera'],'phases':v['phases'],'nativeSensor':v['nativeSensor'],'nativeRoiExamples':v['nativeRoiExamples']} for v in reports],gallery=str(G)),indent=2))
