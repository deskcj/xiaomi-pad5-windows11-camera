from pathlib import Path
import struct,re,bisect,hashlib,json,sys
from capstone import Cs,CS_ARCH_ARM64,CS_MODE_LITTLE_ENDIAN
B=Path(__file__).resolve().parents[1]
p=Path(sys.argv[1]);label=sys.argv[2];out=B/'research'/('flow-'+label);assert not out.exists();out.mkdir()
d=p.read_bytes();pe=struct.unpack_from('<I',d,60)[0];o=pe+24;st=o+struct.unpack_from('<H',d,pe+20)[0];sections=[]
for i in range(struct.unpack_from('<H',d,pe+6)[0]):
 n,vs,va,rs,ro=struct.unpack_from('<8sIIII',d,st+i*40);sections.append(dict(name=n.rstrip(b'\0').decode(),va=va,ro=ro,rs=rs,executable=bool(struct.unpack_from('<I',d,st+i*40+36)[0]&0x20000000)))
def raw(a,n):
 s=next(s for s in sections if s['va']<=a and a+n<=s['va']+s['rs']);return d[s['ro']+a-s['va']:s['ro']+a-s['va']+n]
ex,size=struct.unpack_from('<II',d,o+136);bounds=[]
for i in range(0,size,8):
 b,u=struct.unpack('<II',raw(ex+i,8));length=((u>>2)&0x7ff)*4 if u&3 else (struct.unpack('<I',raw(u,4))[0]&0x3ffff)*4
 if b and length:bounds.append((b,b+length))
bounds.sort();starts=[b for b,e in bounds]
def bound(a):
 i=bisect.bisect_right(starts,a)-1;return bounds[i] if i>=0 and a<bounds[i][1] else None
pat=re.compile(rb'BuildFrameMetadata|SubmitPendingPackets|IpeConfigIfNeeded|IspWorkerThreadProc|NotifyFrameCompleted|RequestManager::|DeferredRequest|Scheduler::|IfeNode::|IpeNode::|CStatsPin::|MaxPending|ProcessCapture|RequestQueue|No.*[Bb]uffer|No.*[Rr]equest|[Dd]ropp?ing.*[Ff]rame')
strings={}
for m in re.finditer(rb'[\x09\x0a\x0d\x20-\x7e]{5,}\x00',d):
 if not pat.search(m[0]):continue
 sec=next((s for s in sections if s['ro']<=m.start()<s['ro']+s['rs']),None)
 if sec:strings[sec['va']+m.start()-sec['ro']]=m[0][:-1].decode()
for m in re.finditer(rb'(?:[\x20-\x7e]\x00){5,}\x00\x00',d):
 text=m[0][:-2].decode('utf-16-le')
 if not pat.search(text.encode()):continue
 sec=next((s for s in sections if s['ro']<=m.start()<s['ro']+s['rs']),None)
 if sec:strings[sec['va']+m.start()-sec['ro']]=text
cs=Cs(CS_ARCH_ARM64,CS_MODE_LITTLE_ENDIAN);cs.skipdata=True;refs=[];regs={}
for sec in sections:
 if not sec['executable']:continue
 for a,z,mn,op in cs.disasm_lite(raw(sec['va'],sec['rs']),sec['va']):
  if mn=='adrp':
   r,v=op.split(', ');regs[r]=int(v[1:],16)
  elif mn=='add':
   m=re.fullmatch(r'(x\d+), (x\d+), #(0x[0-9a-f]+|\d+)',op)
   if m and m[2] in regs:
    regs[m[1]]=regs[m[2]]+int(m[3],0)
    if regs[m[1]] in strings:refs.append(dict(rva=a,text=strings[regs[m[1]]],bound=bound(a)))
  elif mn.startswith('b') or mn=='ret':regs={}
  elif mn not in ('str','stp','cmp','tst'):regs.pop(op.split(',')[0],None)
functions=[]
for b,e in sorted({tuple(r['bound']) for r in refs if r['bound']}):
 file=out/f'{b:x}.txt';file.write_text('\n'.join(f'{a:08x} {mn:10} {op}' for a,z,mn,op in cs.disasm_lite(raw(b,e-b),b))+'\n')
 functions.append(dict(begin=hex(b),end=hex(e),names=list(dict.fromkeys(r['text'] for r in refs if r['bound']==(b,e))),file=str(file)))
report=dict(status='PASS079_NATIVE_FLOW_INDEXED_OFFLINE',source=str(p),sha256=hashlib.sha256(d).hexdigest(),strings=strings,references=refs,functions=functions)
(out/'index.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(dict(label=label,strings=len(strings),references=len(refs),functions=[dict(begin=f['begin'],end=f['end'],names=[s for s in f['names'] if '::' in s and len(s)<120][:3]) for f in functions]),indent=2))
