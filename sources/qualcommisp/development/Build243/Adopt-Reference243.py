"""Retain measured hardware Photo colour correction. Packaging only; no device/tuning writes."""
from pathlib import Path
import copy,datetime,hashlib,json,os,shutil,subprocess,winreg,zipfile
R=Path(r'H:\Vlad\Codex\QualcommISP');B=R/'Build243';C=Path(r'C:\ProgramData\CodexQispBuild243')
load=lambda p:json.loads(p.read_text(encoding='utf-8-sig'))
def sha(p):
 h=hashlib.sha256()
 with Path(p).open('rb') as f:
  for chunk in iter(lambda:f.read(4*1024**2),b''):h.update(chunk)
 return h.hexdigest()
assert not C.exists() and not (B/'research/adoption243.json').exists(),'Preserve prior acceptance attempt'
pre=load(B/'research/pre-adopt-state243.json')
assert pre['status']=='PASS226_CURRENT080_075B_DATA241_TEST_VARIANT_NO_OWNERS_NO_NEW_CRASH_READONLY' and not pre['errors'] and not pre['owners'] and pre['frameServer']=='Stopped'
assert pre['lockAvailable'] and pre['etw']['noQispSessions'] and pre['bugCheckRecord']==120593
trial=load(R/'Build241/research/trial241b.json')
assert trial['status']=='PASS241B_BOTH_PHOTO_NATIVE_CLOSE_FULL_MFT_LOSS0' and len(trial['runs'])==2 and not trial['pending'] and trial['ownerCloseVerified'] and trial['fullMftDecoded'] and not trial['errors']
ui=load(R/'Build242/research/ui-photos242b.json');trace=load(R/'Build242/research/ui-trace242b.json')
assert ui['status']=='PASS242b_BOTH_ACTUAL_UI_PHOTOS_SAVED_CAMERA_CLOSED' and len(ui['photos'])==2 and ui['normalCameraClose'] and not ui['errors']
assert trace['status']=='PASS242b_UI_PHOTO_TRACE_NATIVE_CLOSE_FULL_MFT_LOSS0_CURRENT241_TEST_VARIANT' and not trace['pending'] and not trace['errors']
assert trace['nativeClose']['status']=='PASS' and trace['nativeClose']['initialized']==trace['nativeClose']['closed']==2 and trace['nativeClose']['eventsLost']==trace['mft']['eventsLost']==0
state=load(Path(r'C:\ProgramData\CodexQispBuild241\state241.json'))
assert state['phase']=='candidate' and not state['refreshPending'] and not state['capturePending'] and not state['copyStarted'] and not state['pendingPid'] and state['ownerCloseVerified'] and state['fullMftDecoded'] and not state['errors']
metrics=load(R/'Build241/research/lsc-photo-comparison241.json');stable=['ceiling_mid','ceiling_right','ceiling_corner','sofa','right_wall']
means={}
for phase in metrics['phases']:
 rows=[r for r in phase['rois'] if r['roi'] in stable]
 assert len(rows)==5
 means[phase['phase']]={key:sum(r[key]['chromaDistance'] for r in rows)/len(rows) for key in ('photo-input','photo')}
assert means['b']['photo-input']<.2*means['a']['photo-input'] and means['b']['photo']<.2*means['a']['photo']
reverse=load(R/'Build238/research/reversal238.json');a=reverse['rows'][1]['metrics']['jpeg']['chromaDistance']
assert all(reverse['rows'][i]['metrics']['jpeg']['chromaDistance']<.4*a for i in (0,2))
app=load(R/'Build242/research/app-preview-photo242b.json');assert app['status']=='MEASURED242B_ACTUAL_WINDOWS_CAMERA_BOTH_PREVIEW_JPEG'
assert {r['camera'] for r in app['rows']}=={'front','rear'}
for row in app['rows']:
 assert all(abs(m['lumaDelta'])<5 for m in row['metrics'])
 assert all(m['chromaDistance']<10 for m in row['metrics'])
basepath=Path(r'C:\ProgramData\CodexQispBuild223\retained223.json');base=load(basepath);base_sha=sha(basepath)
variant=load(Path(r'C:\ProgramData\CodexQispBuild241\test-reference241.json'));assert variant['dataReference']==241 and not variant['accepted']
for f in variant['files']:assert sha(f['live'])==sha(f['backup'])==f['sha256'].lower()
for f in base['files']:assert sha(f['backup'])==f['sha256'].lower()
assert sha(base['package']['path'])==sha(base['package']['local'])==base['package']['sha256'].lower()
git=['git','-c','safe.directory=H:/Vlad/Codex/QualcommISP'];expected='11bd7a09df24fa433422f02ec45b123ce8e50703cbd78c5332f71fdc632267ec'
assert hashlib.sha256(subprocess.check_output(git+['diff','--cached','--binary'],cwd=R)).hexdigest()==expected
planned=sum(Path(f['live']).stat().st_size for f in variant['files'])+7*1024**2
assert shutil.disk_usage('C:/').free-planned>1024**3+8*1024**2,'Preserve C1GiB plus8MiB after full data backup and estimated ZIP'
def service_image(service):
 with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE,'SYSTEM\\CurrentControlSet\\Services\\'+service) as k:p=winreg.QueryValueEx(k,'ImagePath')[0]
 p=os.path.expandvars(p).replace('\\SystemRoot',os.environ['SystemRoot']).strip('"')
 if p.startswith('\\??\\'):p=p[4:]
 return Path(p)
source=Path(r'C:\ProgramData\CodexQispBuild061\active\package\Unified')
mapping={'QCCamAvs':'AVStream','qcISP':'ISP','qcCameraMipiCsi':'MIPI','CameraFrontSensor':'FrontSensor','CameraRearSensor':'RearSensor'}
package_files=[]
for node in base['nodes']:
 section=mapping[node['service']];image=service_image(node['service'])
 names=[f['name'] for f in load(R/'Build080/research/signed080.json')['files']] if section=='AVStream' else [p.name for p in sorted((source/section).iterdir()) if p.is_file()]
 for name in names:
  actual=image.parent/name;origin=actual if actual.is_file() else source/section/name
  assert origin.is_file()
  if origin.suffix.lower()=='.sys':assert sha(origin)==sha(image)
  package_files.append((origin,Path(section)/name))
for f in variant['files']:package_files.append((Path(f['live']),Path(f['side'])/f['name']))
assert len(package_files)==23 and len({r.as_posix().lower() for _,r in package_files})==23
C.mkdir();entries=[]
for f in variant['files']:
 entry=copy.deepcopy(f);dest=C/'reference-data'/f['side']/f['name'];dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(f['live'],dest);assert sha(dest)==f['sha256'].lower();entry['backup']=str(dest);entries.append(entry)
destroot=B/'package/Unified';assert not destroot.exists();destroot.mkdir(parents=True);manifest=[]
for origin,rel in package_files:
 dest=destroot/rel;dest.parent.mkdir(parents=True,exist_ok=True);digest=sha(origin);shutil.copy2(origin,dest);assert sha(dest)==digest
 manifest.append(dict(path='Unified/'+rel.as_posix(),bytes=dest.stat().st_size,sha256=digest,source=str(origin)))
readme='''СБОРКА243 — PHOTO ЧЕРЕЗ АППАРАТНЫЕ QUALCOMM BPS/IPE, 25.10.2026

Уже активна на этом планшете. SYS080 / DeviceMFT075b / данные243. Версия INF
остаётся1.0.2324.80:243 обозначает сохранённую конфигурацию, без нового ядра.

Для обеих камер добавлен исходный Android CC13 BPS; для фронтальной также
полный Android LSC34 BPS, согласованный с уже принятым IFE LSC. Изменение
проверено штатными075bпарсерами и контейнером. Значения RGB/насыщенности
вручную не подбирались, обработка аппаратная. JPEG терял цвет ещё на входе:
отдельный Photo BPS→IPE использовал другое донорское tuning, чем Preview.

Задняя: B/A/B возврат223 снова давал бледный стикер; factoryCC13 уменьшает
разницу JPEG с собственным Preview. Фронтальная: пять неподвижныхROI дают
снижение средней разницы цветности Photo примерно18.2→1.1. Затем обычная
WindowsCamera сохранила обе JPEG,4192x2356 и1920x1080; native close/fullMFT
без потерь. Это относительные проверки сцены, не калиброванный AndroidΔE.
Полный паритет Android, PhotoFlash/HDR/AF и новая оценка VideoFPS не заявлены.

Unified содержит23 файла обеих камер и5 модулей из активных пакетов. Это
локальный снимок данного NABU, не универсальный установщик чистой ОС.
Сначала использовать уже установленную камеру. retained243.json и4backup
в C:/ProgramData/CodexQispBuild243 фиксируютSHA, полныйZIP также сохранёнC.

Откат без перезагрузки только после закрытия камер/native/fullMFT/noPending:
1) В повышенномWindowsPowerShell:
 & 'C:/ProgramData/CodexQispBuild241/Apply-Data241.ps1' -Restore234
2) Затем, если нужен принятый223:
 & 'C:/ProgramData/CodexQispBuild234/Apply-Data234.ps1' -Restore223
Нужны ВСЕ папкиC241/C234 сassets/state/data, отдельноPS1 переносить нельзя.
Наunknown/pending/новомboot операторы откажут: сначала свежая проверка,
не обходитьguard, не запускатьстарыйUNINSTALL061.223 и егоZIP неизменны.

Фото: H:/Vlad/cam-results/QISP/Build243-Reference-Photo-Colour.
Подробная передача: H:/Vlad/Codex/QualcommISP/Build243/research/OUTCOME-RU.md.
'''
(B/'package/README-RU.txt').write_text(readme,encoding='utf-8-sig')
(B/'package/MANIFEST-SHA256.json').write_text(json.dumps(dict(dataReference=243,files=manifest),indent=2))
archive=B/'QISP-Reference243-080-075b-FactoryPhotoColour.zip'
with zipfile.ZipFile(archive,'x',compression=zipfile.ZIP_DEFLATED,compresslevel=6) as z:
 for p in sorted((B/'package').rglob('*')):
  if p.is_file():z.write(p,p.relative_to(B/'package').as_posix())
with zipfile.ZipFile(archive) as z:assert z.testzip() is None
assert shutil.disk_usage('C:/').free-archive.stat().st_size>1024**3+8*1024**2
localzip=C/archive.name;shutil.copy2(archive,localzip);assert sha(localzip)==sha(archive)
now=datetime.datetime.now().astimezone().isoformat();ref=copy.deepcopy(base)
ref.update(status='PASS243_RETAINED080_MFT075B_FACTORY_BPS_PHOTO_BOTH_CAMERAS',accepted=True,adoptedAt=now,dataReference=243,baseDataReference=223,sourceDataVariant=241,files=entries,pending=False,boot=pre['boot'],
 frontTuning='223 IFE/IPE/golden/LSC/Metering retained; exact Android CC13 BPS233 plus LSC34 BPS240',rearTuning='223 retained plus exact Android CC13 BPS233',
 validation243=str(B/'research/pre-adopt-state243.json'),validation243SHA=sha(B/'research/pre-adopt-state243.json'),
 photoValidation241=str(R/'Build241/research/trial241b.json'),photoValidation241SHA=sha(R/'Build241/research/trial241b.json'),
 uiValidation242b=str(R/'Build242/research/ui-trace242b.json'),uiValidation242bSHA=sha(R/'Build242/research/ui-trace242b.json'),
 qualityEvidence=dict(rearReversal=str(R/'Build238/research/reversal238.json'),frontLsc=str(R/'Build241/research/lsc-photo-comparison241.json'),frontStableRois=stable,frontStableMetrics=means,excludedRoi='left_wall: moving person may enter this region',actualUi=str(R/'Build242/research/app-preview-photo242b.json')),
 package=dict(path=str(archive),local=str(localzip),bytes=archive.stat().st_size,sha256=sha(archive)),androidParity=False,
 qualification=['Measured Photo colour/brightness improvement relative to own Preview on both cameras.','Front/rear native NV12/JPEG and ordinary WindowsCamera Photo validated with full MFT/loss0 and native close.','Scene lighting changed between runs; no absolute calibrated DeltaE or identical RAW-input claim.','No new VideoFPS/optical latency/AF/HDR/PhotoFlash parity claim.'],
 rollback223=dict(reference=str(basepath),referenceSHA=base_sha,firstRestore234=r'C:\ProgramData\CodexQispBuild241\Apply-Data241.ps1 -Restore234',thenRestore223=r'C:\ProgramData\CodexQispBuild234\Apply-Data234.ps1 -Restore223',sameBootOnly=True,wholeCacheDirectoriesRequired=True))
encoded=json.dumps(ref,ensure_ascii=False,indent=2)+'\n';(C/'retained243.json').write_text(encoded,encoding='utf-8');(B/'research/retained243.json').write_text(encoded,encoding='utf-8');(C/'README-RU.txt').write_text(readme,encoding='utf-8-sig')
gallery=Path(r'H:\Vlad\cam-results\QISP\Build243-Reference-Photo-Colour');gallery.mkdir()
for row in app['rows']:
 source=next(f['source'] for f in row['files'] if f['source'].lower().endswith('.jpg'));shutil.copy2(source,gallery/(row['camera']+'.jpg'))
shutil.copy2(Path(r'H:\Vlad\cam-results\QISP\Build242b-Windows-Camera-Preview-Photo\actual-preview-jpeg242b.png'),gallery/'actual-preview-jpeg.png')
for f in variant['files']:assert sha(f['live'])==f['sha256'].lower()
assert sha(basepath)==base_sha and hashlib.sha256(subprocess.check_output(git+['diff','--cached','--binary'],cwd=R)).hexdigest()==expected
report=dict(status='PASS243_ACCEPTED_MEASURED_HARDWARE_PHOTO_COLOUR_BOTH_CAMERAS',reference=str(C/'retained243.json'),dataReference=243,installedDriver=80,deviceMFT=75,files=entries,package=ref['package'],packageFiles=23,gallery=str(gallery),frontStableMetrics=means,old223ReferenceSHA=base_sha,old223Preserved=True,driverActions=False,tuningWritesPerformedByAdoption=False,androidParity=False,freeCBytesAfter=shutil.disk_usage('C:/').free,adoptedAt=now)
(B/'research/adoption243.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({k:v for k,v in report.items() if k!='files'},ensure_ascii=False,indent=2))
