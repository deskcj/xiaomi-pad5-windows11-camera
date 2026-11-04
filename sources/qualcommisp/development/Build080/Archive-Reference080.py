from pathlib import Path
import json,hashlib,zipfile,os,stat
B=Path(__file__).resolve().parents[1];R=B.parent
load=lambda p:json.loads(p.read_text(encoding='utf-8-sig'))
ret=load(Path(r'C:\ProgramData\CodexQispBuild080\retained080.json'));assert ret['status']=='PASS080_RETAINED_BOTH_CAMERAS_VIDEO30FPS_MFT075_DATA074'
root=Path(ret['retained']);archive=B/'QualcommISP-Build080-Reference-NABU.zip';assert not archive.exists()
files=[]
for p in root.rglob('*'):
 if p.is_file():files.append((p,'retained080/'+p.relative_to(root).as_posix()))
for p in [B/'research/OUTCOME080-RU.md',B/'research/CHANGE080-RU.md',B/'research/retained080.json',B/'research/validation080.json',B/'research/speed080.json',B/'research/cpu080.json',B/'research/signed-pe-proof080.json',B/'research/release-replay080.json',B/'research/kernel-cadence080.json',B/'results/Video-default-20261024-034112/verified-profile/raw-proof080.json',R/'Build077/research/torch-optical-proof077.json',R/'Build077/research/OUTCOME077-RU.md']:
 files.append((p,'evidence/'+p.name))
manifest=[]
with zipfile.ZipFile(archive,'x',compression=zipfile.ZIP_DEFLATED,compresslevel=6) as z:
 for p,name in files:
  d=p.read_bytes();z.writestr(name,d);manifest.append(dict(path=name,bytes=len(d),sha256=hashlib.sha256(d).hexdigest()))
 z.writestr('README-RU.md','# Эталон080 NABU\n\nУстановлен080/oem414; обе камеры IFE→IPE, Video1080p около30FPS, MFT075 и data074. Это проверенная полная резервная копия7 пакетов и данных, а не универсальный автоустановщик. На текущем планшете повторная установка не нужна. Читать evidence/OUTCOME080-RU.md; дневное качество и фото-строб ещё отдельно. Прежние075/061 сохранены.\n')
 z.writestr('manifest.json',json.dumps(manifest,indent=2)+'\n')
with zipfile.ZipFile(archive) as z:
 assert z.testzip() is None
 for f in manifest:assert hashlib.sha256(z.read(f['path'])).hexdigest()==f['sha256']
d=archive.read_bytes();sha=hashlib.sha256(d).hexdigest();os.chmod(archive,stat.S_IREAD)
(archive.with_suffix('.zip.sha256')).write_text(sha+'  '+archive.name+'\n')
out=dict(status='PASS080_REFERENCE_ARCHIVE_EVERY_MEMBER_SHA_VERIFIED',path=str(archive),sha256=sha,bytes=len(d),files=len(files)+2,readonly=True)
(B/'research/reference-archive080.json').write_text(json.dumps(out,indent=2)+'\n');print(json.dumps(out,indent=2))
