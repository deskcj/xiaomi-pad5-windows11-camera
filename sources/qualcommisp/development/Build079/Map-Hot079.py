from pathlib import Path
import struct,json,bisect,re,collections,hashlib
from capstone import Cs,CS_ARCH_ARM64,CS_MODE_LITTLE_ENDIAN
B=Path(__file__).resolve().parents[1];R=B.parent;OUT=B/'research/hot-rvas';OUT.mkdir(exist_ok=True)
p=R/'Build075/candidate-focus-b/AVStream/QcDeviceMFT8150.dll';d=p.read_bytes();pe=struct.unpack_from('<I',d,60)[0];o=pe+24;st=o+struct.unpack_from('<H',d,pe+20)[0];sections=[]
for i in range(struct.unpack_from('<H',d,pe+6)[0]):
 n,vs,va,rs,ro=struct.unpack_from('<8sIIII',d,st+i*40);sections.append((n.rstrip(b'\0').decode(),va,ro,rs))
def raw(r,n):
 _,va,ro,rs=next(s for s in sections if s[1]<=r and r+n<=s[1]+s[3]);return d[ro+r-va:ro+r-va+n]
ex,size=struct.unpack_from('<II',d,o+136);bounds=[]
for i in range(0,size,8):
 b,u=struct.unpack('<II',raw(ex+i,8));length=((u>>2)&0x7ff)*4 if u&3 else (struct.unpack('<I',raw(u,4))[0]&0x3ffff)*4
 if b and length:bounds.append((b,b+length))
bounds.sort();starts=[x[0] for x in bounds]
def bound(r):
 i=bisect.bisect_right(starts,r)-1
 return bounds[i] if i>=0 and r<bounds[i][1] else None
profile=json.loads((B/'research/cpu-top079b.json').read_text());groups={}
for proc in profile['processes']:
 for column in ('topIP','topMftStack'):
  for row in proc[column]:
   m=re.fullmatch(r'qcdevicemft8150\+0x([0-9a-f]+)',row['location'],re.I)
   if not m:continue
   r=int(m[1],16);b=bound(r)
   if not b:continue
   g=groups.setdefault(b,dict(begin=hex(b[0]),end=hex(b[1]),directSamples=0,nearestMftStackSamples=0,sites=[]))
   g['directSamples' if column=='topIP' else 'nearestMftStackSamples']+=row['samples'];g['sites'].append(dict(rva=hex(r),samples=row['samples'],column=column,pid=proc['pid']))
cs=Cs(CS_ARCH_ARM64,CS_MODE_LITTLE_ENDIAN);cs.skipdata=True
def string_at(r):
 try:b=raw(r,4)
 except StopIteration:return None
 s=next(s for s in sections if s[1]<=r<s[1]+s[3]);end=min(s[1]+s[3],r+800)
 b=raw(r,end-r).split(b'\0',1)[0]
 return b.decode('ascii') if len(b)>=5 and all(x in (9,10,13) or 32<=x<127 for x in b) else None
top=sorted(groups.items(),key=lambda x:x[1]['nearestMftStackSamples'],reverse=True)[:20]
for (b,e),g in top:
 regs={};refs=[];lines=[]
 for a,z,mn,op in cs.disasm_lite(raw(b,e-b),b):
  lines.append(f'{a:08x} {mn:10} {op}')
  if mn=='adrp':
   r,v=op.split(', ');regs[r]=int(v[1:],16)
  elif mn=='add':
   m=re.fullmatch(r'(x\d+), (x\d+), #(0x[0-9a-f]+|\d+)',op)
   if m and m[2] in regs:
    regs[m[1]]=regs[m[2]]+int(m[3],0);s=string_at(regs[m[1]])
    if s:refs.append(dict(rva=hex(a),text=s))
  elif mn.startswith('b') or mn=='ret':regs={}
  elif mn not in ('str','stp','cmp','tst'):regs.pop(op.split(',')[0],None)
 g['strings']=refs;(OUT/f'mft-{b:x}.txt').write_text('\n'.join(lines)+'\n')
report=dict(status='PASS079_PROFILE_RVAS_MAPPED_TO_ORIGINAL075_FUNCTIONS',source=str(p),sha256=hashlib.sha256(d).hexdigest(),deviceAccess=False,top=[g for b,g in top],limits=['Grouping nearest MFT stack frame attributes child calls, not exclusive function CPU time.','Only top stored IPs/stack groups are included; kernel addresses not resolved here.'])
(OUT/'mapped079.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps([dict(begin=g['begin'],end=g['end'],direct=g['directSamples'],nearest=g['nearestMftStackSamples'],strings=list(dict.fromkeys(x['text'] for x in g['strings']))[:3]) for b,g in top[:12]],indent=2))
