from pathlib import Path
import struct,json,bisect,re
from capstone import Cs,CS_ARCH_ARM64,CS_MODE_LITTLE_ENDIAN
B=Path(__file__).resolve().parents[1];p=B.parent/'Build075/candidate-focus-b/AVStream/qccamavs8150.sys';d=p.read_bytes()
pe=struct.unpack_from('<I',d,60)[0];o=pe+24;st=o+struct.unpack_from('<H',d,pe+20)[0];ss=[]
for i in range(struct.unpack_from('<H',d,pe+6)[0]):
 n,vs,va,rs,ro=struct.unpack_from('<8sIIII',d,st+i*40);ss.append(dict(name=n.rstrip(b'\0').decode(),va=va,ro=ro,rs=rs,vs=vs))
def raw(a,n):
 s=next(s for s in ss if s['va']<=a and a+n<=s['va']+s['rs']);return d[s['ro']+a-s['va']:s['ro']+a-s['va']+n]
cs=Cs(CS_ARCH_ARM64,CS_MODE_LITTLE_ENDIAN);cs.skipdata=True
for a,n in [(0x92660,0x1a0),(0x92000,0x520),(0x183b0,0xa0),(0x92480,0x100),(0x53a0,0x120),(0x17318,0x150)]:
 text='\n'.join(f'{x:08x} {m:10} {op}' for x,z,m,op in cs.disasm_lite(raw(a,n),a))+'\n'
 (B/f'research/kernel-hot-{a:x}.txt').write_text(text);print(text)
print(json.dumps(ss,indent=2))
