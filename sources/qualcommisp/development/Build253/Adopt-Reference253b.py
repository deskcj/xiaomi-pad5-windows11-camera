"""Package measured rear Android metering; no device or live tuning writes."""
from pathlib import Path
import copy,datetime,hashlib,json,shutil,subprocess,zipfile
R=Path(r'H:\Vlad\Codex\QualcommISP');B=R/'Build253';C=Path(r'C:\ProgramData\CodexQispBuild253')
load=lambda p:json.loads(Path(p).read_text(encoding='utf-8-sig'))
def sha(p):
 h=hashlib.sha256()
 with Path(p).open('rb') as f:
  for z in iter(lambda:f.read(1024*1024),b''):h.update(z)
 return h.hexdigest()
assert not C.exists() and not(B/'research/adoption253.json').exists()
pre=load(B/'research/pre-adopt-state253.json');assert pre['status'].startswith('PASS249_') and not pre['errors'] and not pre['owners'] and pre['frameServer']=='Stopped' and pre['lockAvailable'] and pre['etw']['noQispSessions'] and pre['bugCheckRecord']==120593
state=load('C:/ProgramData/CodexQispBuild249/state249.json');assert state['phase']=='candidate' and not state['refreshPending'] and not state['capturePending'] and not state['copyStarted'] and not state['pendingPid'] and state['ownerCloseVerified'] and state['fullMftDecoded'] and not state['errors']
for build,name in [(247,'247b'),(249,'249'),(250,'250'),(252,'252')]:
 t=load(R/f'Build{build}/research/trial{name}.json');assert t['status'].startswith('PASS') and len(t['runs'])==2 and not t['pending'] and t['ownerCloseVerified'] and t['fullMftDecoded'] and not t['errors']
 assert all(v['eventsLost']==0 and v['nativeClose']=='PASS' for v in t['runs'])
cmp=load(R/'Build251/research/ae-aba251.json');assert cmp['candidateDarkRoiInkNormalizedContrastRatioToMeanA']>1.05
assert all(m['registrationCorrelation']>.7 for row in cmp['rows'] for m in row['measurements'])
a0,a2=[row['measurements'][0]['meanY'] for row in (cmp['rows'][0],cmp['rows'][2])];assert abs(a0-a2)<.02*a0
photo=load(R/'Build252/research/native-photo252.json');assert len(photo['rows'])==2 and photo['status'].startswith('MEASURED252_')
assert {v['camera']:v['jpegSize'] for v in photo['rows']}=={'rear':[4160,3120],'front':[3264,2448]}
flash=load(R/'Build254/research/physical-flash254.json');assert flash['status']=='PASS254_FORCED_REAR_PHOTOFLASH_PHYSICAL_LIGHT_RESTORED' and flash['forcedFlashPhysical'] and flash['controlsRestored']
ft=load(R/'Build254/research/trial254.json');assert ft['status'].startswith('PASS254_') and len(ft['runs'])==1 and not ft['pending'] and ft['ownerCloseVerified'] and ft['fullMftDecoded'] and not ft['errors']
for name in ['native-proof248.json','container-proof248.json']:assert load(R/'Build248/research/metering'/name)['status'].startswith('PASS248_')
basepath=Path('C:/ProgramData/CodexQispBuild243/retained243.json');base=load(basepath);base_sha=sha(basepath);assert base_sha=='189a8a210932e0ce060cd7f04169c75807cf898b57f8ec7b918203619f0ffa13'
assert sha(base['package']['path'])==sha(base['package']['local'])==base['package']['sha256']
old223=Path('C:/ProgramData/CodexQispBuild223/retained223.json');old223sha=sha(old223)
variant=load('C:/ProgramData/CodexQispBuild249/test-reference249.json');assert variant['dataReference']==249 and not variant['accepted']
for f in variant['files']:assert sha(f['live'])==sha(f['backup'])==f['sha256'].lower()
for f in base['files']:assert sha(f['backup'])==f['sha256'].lower()
rear=next(f for f in variant['files'] if f['side']=='RearData' and f['name'].startswith('com.qti.tuned.'));assert rear['sha256']=='b2ae0cfe6f4759f4c394cba8e05c9eca4f473e9c747bed858695e8ddf81bf33c'
for f in variant['files']:
 original=next(v for v in base['files'] if v['live']==f['live'])
 if f is not rear:assert f['sha256'].lower()==original['sha256'].lower()
git=[r'C:\Users\Xiaomi\.cache\codex-runtimes\codex-primary-runtime\dependencies\native\git\cmd\git.exe','-c','safe.directory=H:/Vlad/Codex/QualcommISP'];assert Path(git[0]).is_file()
indexsha='11bd7a09df24fa433422f02ec45b123ce8e50703cbd78c5332f71fdc632267ec';assert hashlib.sha256(subprocess.check_output(git+['diff','--cached','--binary'],cwd=R)).hexdigest()==indexsha
planned=sum(Path(f['live']).stat().st_size for f in variant['files'])+8*1024**2;assert shutil.disk_usage('C:/').free-planned>1024**3+8*1024**2
package=load(R/'Build243/package/MANIFEST-SHA256.json')['files'];assert len(package)==23
prepared=[]
for row in package:
 relative=Path(row['path']);assert relative.parts[0]=='Unified'
 changed=next((f for f in variant['files'] if relative.as_posix()=='Unified/'+f['side']+'/'+f['name']),None)
 source=Path(changed['live']) if changed else Path(row['source']);expected=changed['sha256'].lower() if changed else row['sha256'].lower();assert sha(source)==expected
 prepared.append((source,relative,expected))
C.mkdir();files=[]
for f in variant['files']:
 entry=copy.deepcopy(f);dest=C/'reference-data'/f['side']/f['name'];dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(f['live'],dest);assert sha(dest)==f['sha256'].lower();entry['backup']=str(dest);files.append(entry)
manifest=[]
for source,relative,expected in prepared:
 dest=B/'package'/relative;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,dest);assert sha(dest)==expected;manifest.append(dict(path=relative.as_posix(),source=str(source),bytes=dest.stat().st_size,sha256=expected))
readme='''КОНФИГУРАЦИЯ253 — REAR ANDROID METERING, обе аппаратные камеры.
SYS080/oem414.inf1.0.2324.80 и DeviceMFT075b прежние;253 обозначает конфигурацию.
Поверх принятой243 заменён только полный Metering задней из оригинального
Android BIN, root2792B и7связей. Native075b parser/container подтвердили
точное соответствие всех значений. Front243 и исправление Photo BPS обеих целы.
Значения RGB/насыщенности не подбирались.
A243/B249/A243 при стабильном ночном свете подтвердил меньший пересвет жёлтого
стикера при AEпо тёмному участку; строгая одна маска штрихов и регистрациякадров.
Общий замер стал темнее, реакцияAE мягче/дольше. Это локальная проверка качества,
полный Androidparity не доказан. Preview и nativePhoto8MP/13MP обеих сохранены.
Принудительная задняя PhotoFlash проверена OFF/ON/OFF: физическийсвет, PMIC/EXIF,
FlashEnabled/Auto восстановлены. Это неAndroid flashAUTOпороги и некалибровкастены.
Нет нового общего30FPSfix: в тёмномROIrearET50ms, в светлом33.33ms.
Нужна дневная оценка тона/теней/лица/фокуса, flashAUTO/HDR ещё не закрыты.
Прежний243 и223 сохранены целиком. АрхивCRC/SHAпроверен; это локальный снимокNABU,
не универсальный cleanOSinstaller.
Точный возврат243 на том же boot ТОЛЬКО fullyclosed/native/fullMFT/noPending:
 & 'C:/ProgramData/CodexQispBuild249/Apply-Data249.ps1' -Restore243
Нужна вся папкаC249 (assets/state/original/candidate/helpers), не отдельныйPS1.
Еслиbootизменился/естьpending/неизвестныеSHA, не обходитьguard: сначала новыйаудит.
C243старыеоператорыдальнейшегоотката применятьтолькоПОСЛЕexact243restore.
Фото H:/Vlad/cam-results/QISP/Build251-Rear-AE-ABA и Build252-Native-4x3-Photo.
'''
(B/'package/README-RU.txt').write_text(readme,encoding='utf-8-sig');(C/'README-RU.txt').write_text(readme,encoding='utf-8-sig')
(B/'package/MANIFEST-SHA256.json').write_text(json.dumps(dict(dataReference=253,files=manifest),indent=2)+'\n')
archive=B/'QISP-Reference253-080-075b-AndroidRearMetering.zip'
with zipfile.ZipFile(archive,'x',compression=zipfile.ZIP_DEFLATED,compresslevel=6) as z:
 for p in sorted((B/'package').rglob('*')):
  if p.is_file():z.write(p,p.relative_to(B/'package').as_posix())
with zipfile.ZipFile(archive) as z:assert z.testzip() is None
assert shutil.disk_usage('C:/').free-archive.stat().st_size>1024**3+8*1024**2
local=C/archive.name;shutil.copy2(archive,local);assert sha(local)==sha(archive)
now=datetime.datetime.now().astimezone().isoformat();ref=copy.deepcopy(base)
ref.update(status='PASS253_RETAINED080_MFT075B_ANDROID_REAR_METERING_FACTORY_BPS_BOTH',accepted=True,photoFlashTested=True,flashAUTOAndroidParity=False,validationForcedFlash254=str(R/'Build254/research/physical-flash254.json'),validationForcedFlash254SHA=sha(R/'Build254/research/physical-flash254.json'),dataReference=253,baseDataReference=243,sourceDataVariant=249,boot=pre['boot'],adoptedAt=now,pending=False,files=files,rearTuning=base['rearTuning']+'; complete original Android Metering248 root+7refs',androidParity=False,
 package=dict(path=str(archive),local=str(local),bytes=archive.stat().st_size,sha256=sha(archive)),
 previousReference243=dict(path=str(basepath),sha256=base_sha),validation253=str(B/'research/pre-adopt-state253.json'),validation253SHA=sha(B/'research/pre-adopt-state253.json'),
 validationAE251=str(R/'Build251/research/ae-aba251.json'),validationAE251SHA=sha(R/'Build251/research/ae-aba251.json'),photoValidation252=str(R/'Build252/research/native-photo252.json'),photoValidation252SHA=sha(R/'Build252/research/native-photo252.json'),
 rollback243=dict(reference=str(basepath),referenceSHA=base_sha,operator=r'C:\ProgramData\CodexQispBuild249\Apply-Data249.ps1 -Restore243',operatorSHA=sha('C:/ProgramData/CodexQispBuild249/Apply-Data249.ps1'),sameBootOnly=True,wholeC249CacheRequired=True),
 qualification=['Rear AE-only highlight/readability improvement confirmed by same-scene A/B/A and registered fixed ink mask.','Common metering is darker; daylight scene quality remains to be checked.','Both native Preview and full-resolutionPhoto traversed hardware ISP with native close and fullMFT loss0.','No new all-scene30FPS or complete Android image-quality/AF/HDR/flashAUTO parity claim.'])
ref['qualityEvidence']['rearAE']=str(R/'Build251/research/ae-aba251.json');ref['qualityEvidence']['nativeMeteringProof']=str(R/'Build248/research/metering/native-proof248.json')
encoded=json.dumps(ref,ensure_ascii=False,indent=2)+'\n';(C/'retained253.json').write_text(encoded);(B/'research/retained253.json').write_text(encoded)
assert sha(basepath)==base_sha and sha(old223)==old223sha
assert hashlib.sha256(subprocess.check_output(git+['diff','--cached','--binary'],cwd=R)).hexdigest()==indexsha
for f in files:assert sha(f['live'])==sha(f['backup'])==f['sha256'].lower()
r=dict(status='PASS253_ACCEPTED_MEASURED_REAR_ANDROID_METERING_WITH_BOTH_NATIVE_PHOTO',reference=str(C/'retained253.json'),referenceSHA=sha(C/'retained253.json'),package=ref['package'],packageFiles=23,installedDriver=80,deviceMFT=75,dataReference=253,old243ReferenceSHA=base_sha,old223ReferenceSHA=old223sha,stagedDiffSHA=indexsha,driverActions=False,tuningWritesPerformedByAdoption=False,androidParity=False,freeCBytesAfter=shutil.disk_usage('C:/').free,adoptedAt=now)
(B/'research/adoption253.json').write_text(json.dumps(r,ensure_ascii=False,indent=2)+'\n');print(json.dumps(r,ensure_ascii=False,indent=2))
