from pathlib import Path
import json,struct,hashlib,shutil,re
R=Path(__file__).resolve().parents[2];B=R/'Build080';src=R/'Build075/candidate-focus-b/AVStream';dst=B/'candidate/AVStream'
proof=json.loads((R/'Build079/research/dead-relay079.json').read_text());assert proof['status'].startswith('PASS079_DEAD_RELAY')
assert not dst.exists();dst.mkdir(parents=True)
for sub in ('tools','src','tests','research','bin'): (B/sub).mkdir(exist_ok=True)
for p in src.iterdir():
 if p.is_file() and p.suffix.lower()!='.cat':shutil.copyfile(p,dst/p.name)
p=dst/'qccamavs8150.sys';d=bytearray(p.read_bytes());assert hashlib.sha256(d).hexdigest()==proof['sourceSHA256']
pe=struct.unpack_from('<I',d,60)[0];o=pe+24;st=o+struct.unpack_from('<H',d,pe+20)[0]
def offset(rva):
 for i in range(struct.unpack_from('<H',d,pe+6)[0]):
  va,rs,ro=struct.unpack_from('<III',d,st+i*40+12)
  if va<=rva<va+rs:return ro+rva-va
 raise AssertionError(hex(rva))
off=offset(0x7f5bc);assert struct.unpack_from('<I',d,off)[0]==0x94004b51
struct.pack_into('<I',d,off,0x94000e65);p.write_bytes(d)
inf=dst/'qccamavs8150.inf';s=inf.read_text(encoding='utf-8-sig');s=re.sub(r'(?im)^DriverVer\s*=.*$', 'DriverVer = 10/24/2026, 1.0.2324.80',s);s=re.sub(r'(?im)^CatalogFile\s*=.*$', 'CatalogFile = CodexQisp080AVStream.cat',s);inf.write_text(s,encoding='utf-8')
out=dict(status='PASS080_SINGLE_STOCK_RELEASE_CALL_PREPARED_UNSIGNED_NOT_INSTALLED',sourceSHA256=proof['sourceSHA256'],unsignedSHA256=hashlib.sha256(d).hexdigest(),patchRVA='0x7f5bc',patchFileOffset=off,oldWord='0x94004b51',newWord='0x94000e65',mftSHA256=hashlib.sha256((dst/'QcDeviceMFT8150.dll').read_bytes()).hexdigest(),driverVersion='1.0.2324.80',pixelOrTuningChanges=False,kernelInstructionBytes=4,sourceProof=str(R/'Build079/research/dead-relay079.json'))
(B/'research/prepared080.json').write_text(json.dumps(out,indent=2)+'\n');print(json.dumps(out,indent=2))
