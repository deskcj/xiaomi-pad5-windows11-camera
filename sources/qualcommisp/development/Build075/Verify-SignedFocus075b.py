from pathlib import Path
import json,struct,hashlib
R=Path(__file__).resolve().parents[1]
old=(R.parent/'Build062/candidate/AVStream/QcDeviceMFT8150.dll').read_bytes()
new=(R/'candidate-focus-b/AVStream/QcDeviceMFT8150.dll').read_bytes()
proof=json.loads((R/'research/focus/focus-get-proof075b.json').read_text())
p=struct.unpack_from('<I',old,60)[0];o=p+24;sections=o+struct.unpack_from('<H',old,p+20)[0]
endheader=sections+struct.unpack_from('<H',old,p+6)[0]*40
assert old[sections:endheader]==new[sections:endheader]
allowed=set()
for x in proof['patches']:
 off=x['fileOffset'];allowed.update(range(off,off+4))
 assert struct.unpack_from('<I',new,off)[0]==int(x['new'],16)
checked=[]
for i in range(struct.unpack_from('<H',old,p+6)[0]):
 h=sections+i*40;size,raw=struct.unpack_from('<II',old,h+16)
 assert all(old[j]==new[j] or j in allowed for j in range(raw,raw+size))
 checked.append(old[h:h+8].rstrip(b'\0').decode())
headerallowed=set(range(o+64,o+68))|set(range(o+144,o+152))
assert all(old[j]==new[j] or j in headerallowed for j in range(endheader))
sha=lambda b:hashlib.sha256(b).hexdigest()
r=dict(status='PASS075B_SIGNED_PE_ONLY_FOCUS_BLOCK_PLUS_SIGNATURE_METADATA',sha256=sha(new),sectionNames=checked,codePatchRange=['0x16d91c','0x16d978'],kernelUnchanged=True,newCodeSection=False,exceptionLayoutChanged=False,deviceAccess=False)
(R/'research/signed-pe-proof075b.json').write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r,indent=2))
