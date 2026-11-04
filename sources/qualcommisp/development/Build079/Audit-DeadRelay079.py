from pathlib import Path
import struct,json,hashlib
from capstone import Cs,CS_ARCH_ARM64,CS_MODE_LITTLE_ENDIAN
B=Path(__file__).resolve().parents[1];R=B.parent
current=R/'Build075/candidate-focus-b/AVStream/qccamavs8150.sys'
stock=R.parent/'Win11Pad5/bypass-driver/codex/AV366_QUALCOMM107_BUFFER/native/AVStreamStock/qccamavs8150.sys'
def read(p):
 d=p.read_bytes();pe=struct.unpack_from('<I',d,60)[0];o=pe+24;st=o+struct.unpack_from('<H',d,pe+20)[0];ss=[]
 for i in range(struct.unpack_from('<H',d,pe+6)[0]):
  n,vs,va,rs,ro=struct.unpack_from('<8sIIII',d,st+i*40);ss.append(dict(name=n.rstrip(b'\0').decode(),va=va,ro=ro,rs=rs,vs=vs,execute=bool(struct.unpack_from('<I',d,st+i*40+36)[0]&0x20000000)))
 def raw(a,n):
  s=next(s for s in ss if s['va']<=a and a+n<=s['va']+s['rs']);return d[s['ro']+a-s['va']:s['ro']+a-s['va']+n]
 return d,ss,raw
d,ss,raw=read(current);sd,ssec,sraw=read(stock)
assert hashlib.sha256(d).hexdigest()=='5fafa6842754041d313ee6131bfecec493c533cf2e8c44d0458c2a0774252039'
assert hashlib.sha256(sd).hexdigest()=='ace51e1034a7a1cf73f81ae97d82c90be4d317ec0b1f108e004434c905ea1f9c'
assert struct.unpack('<I',raw(0x7f5bc,4))[0]==0x94004b51
assert struct.unpack('<I',sraw(0x7f5bc,4))[0]==0x94000e65
assert raw(0x183f4,4)==sraw(0x183f4,4)==bytes.fromhex('888a40b9')
cs=Cs(CS_ARCH_ARM64,CS_MODE_LITTLE_ENDIAN);cs.skipdata=True;branches=[];shadow=[]
for s in ss:
 if not s['execute']:continue
 for a,z,m,op in cs.disasm_lite(raw(s['va'],s['rs']),s['va']):
  if m in ('b','bl') and op.startswith('#0x'):
   target=int(op[1:],16)
   if 0x92000<=target<0x92800 and not 0x92000<=a<0x92800:branches.append(dict(rva=hex(a),instruction=m+' '+op,target=hex(target)))
  if m=='adrp' and op.endswith('#0x93000') and not 0x92000<=a<0x92800:shadow.append(dict(rva=hex(a),instruction=m+' '+op))
assert branches==[dict(rva='0x1756c',instruction='bl #0x92660',target='0x92660'),dict(rva='0x17584',instruction='bl #0x92660',target='0x92660'),dict(rva='0x7f5bc',instruction='bl #0x92300',target='0x92300')],branches
assert not shadow,shadow
cpu=json.loads((B/'research/cpu-top079f.json').read_text());system=next(p for p in cpu['processes'] if p['pid']==4)
samples=sum(r['samples'] for r in system['topIP'] if r['location'].startswith('qccamavs8150+0x925') or r['location'].startswith('qccamavs8150+0x924f'))
out=dict(status='PASS079_DEAD_RELAY_SINGLE_EXTERNAL_PRODUCER_AND_NATIVE_FINAL_CONFIRMED',source=str(current),sourceSHA256=hashlib.sha256(d).hexdigest(),stock=str(stock),stockSHA256=hashlib.sha256(sd).hexdigest(),externalAvcodeBranches=branches,externalShadowPageAdrp=shadow,completeFrameRVA='0x183f4',completeFrameWord='0xb9408a88',candidateChange=dict(rva='0x7f5bc',oldWord='0x94004b51',newStockWord='0x94000e65',oldTarget='0x92300 diagnostic relay producer',newTarget='0x82f50 original buffer release'),retainedLifetimeCalls=['0x1756c ->0x92660','0x17584 ->0x92660'],sampledLoopCPUms=samples,limits=['Direct ARM64 branches and ADRP references checked; signed PE diff and exact execution replay still required before installation.','Keep relay code/data/unwind sections and both native lifetime-wrapper calls in place; no layout or lifetime rewrite.','Only current single producer call will be restored; old AV381 changed three calls and is not the proposed package.'])
(B/'research/dead-relay079.json').write_text(json.dumps(out,indent=2)+'\n');print(json.dumps(out,indent=2))
