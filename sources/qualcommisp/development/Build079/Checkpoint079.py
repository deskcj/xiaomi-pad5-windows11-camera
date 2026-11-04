from pathlib import Path
import os,json,datetime,hashlib,shutil
R=Path(__file__).resolve().parents[2];B=R/'Build079'
def load(p):return json.loads(p.read_text(encoding='utf-8-sig'))
for p in [B/'research/cpu079b.json',B/'research/cpu-reader079d.json',R/'Build078/research/gate078.json']:
 d=load(p);assert d['status'].startswith('PASS') and not d.get('pending',False),(p,d)
root=Path(r'C:\Users\Xiaomi\Documents\Codex\2026-10-23\new-chat\outputs')
dest=root/('Night079-checkpoint-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S'));assert not dest.exists()
sources=[]
for name in ('Build078','Build079'):
 for sub in ('src','tools','bin','research'):
  for p in (R/name/sub).rglob('*'):
   if p.is_file() and not any(x in p.parts for x in ('project','tmp','profile','__pycache__')) and p.suffix.lower() not in ('.obj','.exp','.lib','.etlx','.xml','.pyc'):sources.append((p,Path(name)/p.relative_to(R/name)))
run=B/'results/Video-default-20261024-025610'
for p in run.rglob('*'):
 if p.is_file() and p.suffix.lower() in ('.json','.log','.etl'):
  sources.append((p,Path('Build079')/p.relative_to(B)))
etl=B/'results/cpu-b-20261024-025608/cpu079b.etl';sources.append((etl,Path('Build079')/etl.relative_to(B)))
for p in [R/'HANDOFF.md',R.parent/'START-HERE.md',R/'research/night073-20261023/CHECKPOINT-RU.md',R/'research/ACTIVE-TUNING-LATEST.json',R/'Build075/research/OUTCOME075-RU.md',R/'Build077/research/OUTCOME077-RU.md',R/'Build077/research/torch-optical-proof077.json',R/'Build075/QualcommISP-Build075-Reference-NABU.zip']:
 sources.append((p,Path('notes')/p.name))
total=sum(p.stat().st_size for p,_ in sources);assert shutil.disk_usage(root).free>total+400*1024*1024
dest.mkdir();manifest=[]
def sha(p):
 h=hashlib.sha256()
 with p.open('rb') as f:
  while block:=f.read(1024*1024):h.update(block)
 return h.hexdigest()
for p,rel in sources:
 target=dest/rel;target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(p,target)
 digest=sha(p);assert sha(target)==digest;manifest.append(dict(path=str(rel),source=str(p),bytes=p.stat().st_size,sha256=digest))
report=dict(status='PASS079_INTERNAL_CHECKPOINT_SHA256_VERIFIED',path=str(dest),created=datetime.datetime.now().astimezone().isoformat(),files=len(manifest),bytes=total,manifest=manifest)
(dest/'manifest.json').write_text(json.dumps(report,indent=2)+'\n')
(root/'NIGHT079-CHECKPOINT-LATEST.txt').write_text(str(dest)+'\n')
print(json.dumps({k:v for k,v in report.items() if k!='manifest'},indent=2))
