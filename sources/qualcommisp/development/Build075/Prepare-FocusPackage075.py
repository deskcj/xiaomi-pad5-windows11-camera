from pathlib import Path
import hashlib,json,re,shutil,struct
R=Path(r'H:\Vlad\Codex\QualcommISP'); B=R/'Build075'
proof=json.loads((B/'research/focus/focus-get-proof075.json').read_text())
assert proof['status']=='PASS075_ORIGINAL_FOCUS_GET_STALE_FLAGS_REPRODUCED_AND_CORRECTED'
original=R/'Build062/candidate/AVStream'; src=(original/'QcDeviceMFT8150.dll').read_bytes()
candidate=(B/'research/QcDeviceMFT8150-focus075-UNSIGNED.dll').read_bytes()
sha=lambda b:hashlib.sha256(b).hexdigest()
assert sha(src)==proof['sourceSHA'] and sha(candidate)==proof['unsignedCandidateSHA']
assert len(src)==len(candidate)
allowed=set()
for p in proof['patches']:
 o=p['fileOffset'];allowed.update(range(o,o+4))
 assert struct.unpack_from('<I',src,o)[0]==int(p['old'],16)
 assert struct.unpack_from('<I',candidate,o)[0]==int(p['new'],16)
assert all(a==b or i in allowed for i,(a,b) in enumerate(zip(src,candidate)))
cases=proof['cases']; corrected=[c for c in cases if c['image']=='corrected']
assert len(corrected)==90 and all(c['valid'] for c in corrected)
dest=B/'candidate/AVStream';dest.mkdir(parents=True,exist_ok=True)
for f in original.iterdir():
 if not f.is_file() or f.suffix.lower()=='.cat':continue
 out=dest/f.name
 if f.suffix.lower()=='.inf':
  txt=f.read_text(encoding='utf-8-sig')
  txt,n=re.subn(r'(?im)^DriverVer\s*=.*$','DriverVer = 10/24/2026, 1.0.2324.75',txt);assert n==1
  txt,n=re.subn(r'(?im)^CatalogFile\s*=.*$','CatalogFile = CodexQisp075AVStream.cat',txt);assert n==1
  out.write_text(txt,encoding='utf-8')
 elif f.name=='QcDeviceMFT8150.dll':out.write_bytes(candidate)
 else:shutil.copyfile(f,out)
(B/'tests').mkdir(exist_ok=True)
report=dict(status='PASS075_OFFLINE_PACKAGE_PREPARED_UNSIGNED_NOT_INSTALLED',sourceSHA=sha(src),candidateSHA=sha(candidate),patches=proof['patches'],originalIncorrect=sum(not c['valid'] for c in cases if c['image']=='original'),correctedIncorrect=0,caseCount=len(cases),kernelUnchanged=True,PELayoutUnchanged=True,exceptionTablesUnchanged=True,scope='GET focus state reconstruction only; no SET, motor, exposure, sensor or IPE command changes',deviceAccess=False)
(B/'research/prepared-focus075.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
