from pathlib import Path
import os,json,re,statistics,collections,xml.etree.ElementTree as ET,datetime,csv
B=Path(__file__).resolve().parents[1];run=B/'results/Video-default-20261024-025610'
NS='{http://schemas.microsoft.com/win/2004/08/events/event}'
out=[]
for camera in ('front','rear'):
 src=run/f'dbgprint-{camera}.xml';rows=[];groups=collections.Counter();examples={}
 fd=os.open(src,os.O_RDONLY|getattr(os,'O_SEQUENTIAL',0))
 with os.fdopen(fd,'rb') as stream:
  for _,e in ET.iterparse(stream,events=('end',)):
   if e.tag!=NS+'Event':continue
   msg=e.find('.//'+NS+'Data[@Name="Message"]');time=e.find('.//'+NS+'TimeCreated')
   if msg is not None and msg.text:
    text=msg.text.strip();m=re.search(r'\{([^}]+)\}',text)
    if m:groups[m[1]]+=1;examples.setdefault(m[1],text)
    if re.search(r'camif_sof_irq|ifeFrameId|clock|frequency|pixel|dimension|resolution|width|height|BuildFrameMetadata|IpeConfigIfNeeded|SetSendPacket|drop|skip|RequestId',text,re.I):
     rows.append(dict(time=time.attrib.get('SystemTime') if time is not None else None,message=text))
   e.clear()
 sof=[r for r in rows if 'camif_sof_irq' in r['message']]
 ts=[datetime.datetime.fromisoformat(r['time'].replace('Z','+00:00')).timestamp() for r in sof]
 gaps=[(b-a)*1000 for a,b in zip(ts,ts[1:])]
 stats=dict(count=len(sof),wallFPS=(len(ts)-1)/(ts[-1]-ts[0]) if len(ts)>1 else None,gapMedianMS=statistics.median(gaps) if gaps else None,gapMinMS=min(gaps) if gaps else None,gapMaxMS=max(gaps) if gaps else None,gapsMS=gaps)
 (B/f'research/kernel-events-{camera}079.json').write_text(json.dumps(dict(source=str(src),events=rows,groups=groups.most_common(),examples=examples,sof=stats),indent=2)+'\n')
 out.append(dict(camera=camera,sof={k:v for k,v in stats.items() if k!='gapsMS'},topGroups=groups.most_common(15),clockExamples=[r for r in rows if re.search('clock|frequency|pixel',r['message'],re.I)][:14]))
(B/'research/kernel-cadence079.json').write_text(json.dumps(dict(status='PASS079_KERNEL_TRACE_OFFLINE_CADENCE_EXTRACTED',rows=out),indent=2)+'\n')
print(json.dumps(out,indent=2))
