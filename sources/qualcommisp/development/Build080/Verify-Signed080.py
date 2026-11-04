from pathlib import Path
import json,struct,hashlib
B=Path(__file__).resolve().parents[1];R=B.parent
old=(R/'Build075/candidate-focus-b/AVStream/qccamavs8150.sys').read_bytes();new=(B/'candidate/AVStream/qccamavs8150.sys').read_bytes()
p=struct.unpack_from('<I',old,60)[0];o=p+24;st=o+struct.unpack_from('<H',old,p+20)[0];n=struct.unpack_from('<H',old,p+6)[0];end=st+n*40
assert old[st:end]==new[st:end]
proof=json.loads((B/'research/prepared080.json').read_text());patch=proof['patchFileOffset'];allow=set(range(patch,patch+4));assert struct.unpack_from('<I',new,patch)[0]==0x94000e65
sections=[];diff=[]
for i in range(n):
 h=st+i*40;size,raw=struct.unpack_from('<II',old,h+16)
 for j in range(raw,raw+size):
  if old[j]!=new[j]:assert j in allow;diff.append(j)
 sections.append(old[h:h+8].rstrip(b'\0').decode())
headallow=set(range(o+64,o+68))|set(range(o+144,o+152));assert all(old[j]==new[j] or j in headallow for j in range(end))
assert old[o+136:o+144]==new[o+136:o+144] # exception directory exact
assert (R/'Build075/candidate-focus-b/AVStream/QcDeviceMFT8150.dll').read_bytes()==(B/'candidate/AVStream/QcDeviceMFT8150.dll').read_bytes()
out=dict(status='PASS080_SIGNED_PE_ONLY_ONE_STOCK_RELEASE_INSTRUCTION_PLUS_SIGNATURE',sha256=hashlib.sha256(new).hexdigest(),sourceSHA256=hashlib.sha256(old).hexdigest(),sections=sections,changedSectionBytes=diff,instructionRVA='0x7f5bc',oldWord='0x94004b51',newStockWord='0x94000e65',layoutExceptionUnwindImportsAndAllOtherCodeUnchanged=True,mft075Exact=True,tuningOrPixelChanges=False,deviceAccess=False)
(B/'research/signed-pe-proof080.json').write_text(json.dumps(out,indent=2)+'\n');print(json.dumps(out,indent=2))
