from pathlib import Path
import json,re,collections,math,statistics
B=Path(__file__).resolve().parents[1];R=B.parent
def load(p):return json.loads(p.read_text(encoding='utf-8-sig'))
before=load(R/'Build079/research/cpu079b.json');after=load(B/'research/cpu080.json')
data=[]
for label,state,profile in [('before075',before,load(R/'Build079/research/cpu-top079f.json')),('after080',after,load(B/'research/cpu-top080.json'))]:
 assert not state['pending'] and state['status'].startswith('PASS')
 result=load(Path(state['run'])/'result.json');system=next(p for p in profile['processes'] if p['pid']==4);server=next(p for p in profile['processes'] if p['name']=='svchost')
 samples=sum(x['samples'] for x in system['cameraKernelBins'] if 'qccamavs8150+0x924f' in x['location'] or 'qccamavs8150+0x925' in x['location'])
 rows=[]
 for session in result['sessions']:
  metrics={k:float(v) for k,v in re.findall(r'\b([\w]+)=([\d.]+)',session['resultLine'])}
  client=next(p for p in profile['processes'] if p['pid']==session['clientPid']);start=math.floor((client['startMS']+metrics['first_frame_ms'])/1000)+1;end=start+6
  cpu={}
  for kind,process,field in [('systemAll',system,'bins'),('systemCameraStack',system,'cameraKernelBins'),('serverAll',server,'bins')]:
   selected=[x for x in process[field] if start<=int(x['location'].split(':',1)[0])<end and (kind!='systemCameraStack' or x['location'].split(':',1)[1]!='none')]
   n=sum(x['samples'] for x in selected);cpu[kind]=dict(samples=n,oneCoreFraction=n/((end-start)*1000))
  rows.append(dict(camera=session['camera'],metrics={k:metrics[k] for k in ['frames','delivered_fps','timestamp_fps','max_gap_ms','first_frame_ms','duplicates','read_failures']},steadyTraceSeconds=[start,end],cpu=cpu,nativeClose=session['nativeCloseEvidencePassed']))
 data.append(dict(label=label,run=state['run'],cpuTraceDurationMS=profile['durationMS'],eventsLost=profile['lost'],systemCPUms=system['samples'],relayLoopCPUms=samples,rows=rows))
assert data[0]['relayLoopCPUms']>10000 and data[1]['relayLoopCPUms']==0
assert all(x['metrics']['delivered_fps']>29 and x['metrics']['read_failures']==0 and x['metrics']['duplicates']==0 and x['nativeClose'] for x in data[1]['rows'])
comparison=[]
for a,b in zip(data[0]['rows'],data[1]['rows']):
 assert a['camera']==b['camera']
 comparison.append(dict(camera=a['camera'],beforeFPS=a['metrics']['delivered_fps'],afterFPS=b['metrics']['delivered_fps'],fpsIncreasePercent=(b['metrics']['delivered_fps']/a['metrics']['delivered_fps']-1)*100,beforeMaxCallbackGapMS=a['metrics']['max_gap_ms'],afterMaxCallbackGapMS=b['metrics']['max_gap_ms'],beforeSystemCameraCoreFraction=a['cpu']['systemCameraStack']['oneCoreFraction'],afterSystemCameraCoreFraction=b['cpu']['systemCameraStack']['oneCoreFraction']))
report=dict(status='PASS080_BOTH_VIDEO_30FPS_UNUSED_RELAY_CPU_REMOVED',data=data,comparison=comparison,limits=['Two bounded sequential tests in an uncalibrated dark scene; the kernel instruction and all other image/control/data code are byte-audited.','CPU samples are statistical and include kernel workers visible on camera stacks; no glass-to-glass latency or optical Android-parity claim.','Preview can remain limited by long exposure in darkness; video FPS does not change sensor exposure limits.','First_frame_ms includes control/tuning inspection and warmup; not a bare camera-launch benchmark.'])
(B/'research/speed080.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(dict(status=report['status'],relayBefore=data[0]['relayLoopCPUms'],relayAfter=data[1]['relayLoopCPUms'],comparison=comparison),indent=2))
