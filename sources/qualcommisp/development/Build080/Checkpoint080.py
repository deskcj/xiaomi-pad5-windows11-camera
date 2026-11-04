from pathlib import Path
import json,hashlib,shutil,datetime,re,os
B=Path(__file__).resolve().parents[1];R=B.parent;load=lambda p:json.loads(p.read_text(encoding='utf-8-sig'))
ret=load(B/'research/retained080.json');assert ret['status'].startswith('PASS080_RETAINED')
header='''<!-- NIGHT080_CURRENT -->
# Рабочий эталон080: обе камеры IFE/IPE, Video1080p около30FPS

24 октября2026 около03:55 +05: установлена080/oem414 + MFT075/data074. Убрана ненужная старая relay-копия одним stock release call; front27.996→29.938, rear23.417→30.014FPS; повтор29.939/30.020. CPU старого цикла12.4s→0. Шесть сессий/nativeclose/cache/AF restore PASS, boot прежний, WPR остановлен. Preview в темноте около12FPS из-за выдержки; дневные цвет/оптическийAF/фото-строб ещё отдельно. Torch077 физически подтверждён. Читать `QualcommISP/Build080/research/OUTCOME080-RU.md` (изrepo `Build080/research/OUTCOME080-RU.md`). C:/ProgramData/CodexQispBuild080/retained080.json; откат075: C:/ProgramData/CodexQispBuild080/rollback075-20261024-033931/Restore-Avs075.ps1. Старые075 и061 сохранены. Предыдущие заголовки ниже — история.
<!-- END_NIGHT080_CURRENT -->

'''
for p in [R.parent/'START-HERE.md',R/'HANDOFF.md',R/'research/night073-20261023/CHECKPOINT-RU.md',R/'research/ACTIVE-BUILD-POLICY-RU.md']:
 old=p.read_text(encoding='utf-8-sig')
 if '<!-- NIGHT080_CURRENT -->' in old:old=old.split('<!-- END_NIGHT080_CURRENT -->',1)[1].lstrip()
 p.write_text(header+old,encoding='utf-8')
root=Path(r'C:\Users\Xiaomi\Documents\Codex\2026-10-23\new-chat\outputs');dest=root/('Night080-checkpoint-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S'));assert not dest.exists()
sources=[]
for name in ('Build079','Build080'):
 for sub in ('src','tools','bin','tests','research'):
  for p in (R/name/sub).rglob('*'):
   if p.is_file() and not any(x in p.parts for x in ('project','tmp','profile','__pycache__')) and p.suffix.lower() not in ('.obj','.exp','.lib','.etlx','.xml','.pyc'):
    sources.append((p,Path(name)/p.relative_to(R/name)))
run=Path(load(B/'research/cpu080.json')['run'])
for p in run.rglob('*'):
 if p.is_file() and (p.suffix.lower() in ('.json','.log','.etl') or p.name.startswith('frame-') and p.suffix=='.bin'):
  sources.append((p,Path('Build080')/p.relative_to(B)))
etl=Path(load(B/'research/cpu080.json')['etl']);sources.append((etl,Path('Build080')/etl.relative_to(B)))
for p in [R/'HANDOFF.md',R.parent/'START-HERE.md',R/'research/night073-20261023/CHECKPOINT-RU.md',R/'research/ACTIVE-TUNING-LATEST.json',R/'Build077/research/OUTCOME077-RU.md',R/'Build077/research/torch-optical-proof077.json',B/'QualcommISP-Build080-Reference-NABU.zip',B/'research/reference-archive080.json']:
 sources.append((p,Path('notes')/p.name))
total=sum(p.stat().st_size for p,_ in sources);assert shutil.disk_usage(root).free>total+400*1024*1024;dest.mkdir();manifest=[]
def sha(p):
 h=hashlib.sha256()
 with p.open('rb') as f:
  while block:=f.read(1024*1024):h.update(block)
 return h.hexdigest()
for p,rel in sources:
 target=dest/rel;target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(p,target);digest=sha(p);assert sha(target)==digest
 manifest.append(dict(path=str(rel),source=str(p),bytes=p.stat().st_size,sha256=digest))
report=dict(status='PASS080_INTERNAL_CHECKPOINT_SHA256_VERIFIED',path=str(dest),created=datetime.datetime.now().astimezone().isoformat(),files=len(manifest),bytes=total,manifest=manifest)
(dest/'manifest.json').write_text(json.dumps(report,indent=2)+'\n');(root/'NIGHT080-CHECKPOINT-LATEST.txt').write_text(str(dest)+'\n')
print(json.dumps({k:v for k,v in report.items() if k!='manifest'},indent=2))
