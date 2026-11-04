"""Index original MFT delivery strings and exact NEED_MORE_INPUT constant sites; no device access."""
from pathlib import Path
import json,re,bisect,hashlib
from capstone import Cs,CS_ARCH_ARM64,CS_MODE_LITTLE_ENDIAN

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'Build075/research/focus';OUT.mkdir(exist_ok=True)
p=ROOT/'Build062/candidate/AVStream/QcDeviceMFT8150.dll';d=p.read_bytes()
assert hashlib.sha256(d).hexdigest()=='ee9984ceb0740c960f65eda19581e22a8526e2c95551591a9b4929b808d4212b'
info=json.loads((ROOT/'research/agent-front-convergence064/fileload-lifecycle-index064.json').read_text())[1]
bounds=info['bounds'];starts=[b for b,e in bounds]
def bound(a):
 i=bisect.bisect_right(starts,a)-1
 return bounds[i] if i>=0 and a<bounds[i][1] else None
def raw(a,n):
 s=next(s for s in info['sections'] if s['va']<=a and a+n<=s['va']+s['rs']);p=s['ro']+a-s['va'];return d[p:p+n]
pattern=re.compile(rb'CProperty_Focus|ExtendedFocus|FOCUSMODE|SetFocusMode|GetFocusMode',re.I)
strings={}
for m in re.finditer(rb'[\x20-\x7e]{4,}\x00',d):
 if not pattern.search(m.group()):continue
 sec=next((s for s in info['sections'] if s['ro']<=m.start()<s['ro']+s['rs']),None)
 if sec:strings[sec['va']+m.start()-sec['ro']]=m.group().rstrip(b'\0').decode()
cs=Cs(CS_ARCH_ARM64,CS_MODE_LITTLE_ENDIAN);cs.skipdata=True
refs=[];constants=[];regs={};values={}
text=next(s for s in info['sections'] if s['name']=='.text')
for a,z,mn,op in cs.disasm_lite(raw(text['va'],text['rs']),text['va']):
 if mn=='adrp':
  r,v=op.split(', ');regs[r]=int(v[1:],16)
 elif mn=='add':
  m=re.fullmatch(r'(x\d+), (x\d+), #(0x[0-9a-f]+|\d+)',op)
  if m and m[2] in regs:
   regs[m[1]]=regs[m[2]]+int(m[3],0)
   if regs[m[1]] in strings:refs.append(dict(rva=a,stringRVA=regs[m[1]],text=strings[regs[m[1]]],bound=bound(a)))
 elif mn.startswith('b') or mn=='ret':regs={};values={}
 elif mn not in ('str','stp','cmp','tst'):regs.pop(op.split(',')[0],None)
 if mn in ('mov','movz','movn','movk'):
  m=re.fullmatch(r'(w\d+), #(-?0x[0-9a-f]+|-?\d+)(?:, lsl #(\d+))?',op)
  if m:
   r=m[1];v=int(m[2],0);shift=int(m[3] or 0)
   if mn=='movk':values[r]=(values.get(r,0)&~(0xffff<<shift))|((v&0xffff)<<shift)
   elif mn=='movn':values[r]=(~(v<<shift))&0xffffffff
   else:values[r]=(v<<shift)&0xffffffff
   if values[r]==0xc00d6d72:constants.append(dict(rva=a,instruction=mn+' '+op,register=r,bound=bound(a)))
  else:values.pop(op.split(',')[0],None)
 elif mn not in ('str','stp','cmp','tst') and not mn.startswith('b'):
  values.pop(op.split(',')[0],None)
exports=[]
for b,e in sorted({tuple(r['bound']) for r in refs+constants if r['bound']}):
 file=OUT/f'windows-{b:x}.txt'
 file.write_text('\n'.join(f'{a:08x} {mn:10} {op}' for a,z,mn,op in cs.disasm_lite(raw(b,e-b),b))+'\n')
 exports.append(dict(begin=b,end=e,file=str(file)))
report=dict(status='PASS073_ORIGINAL_DELIVERY_INDEX',source=str(p),sha256=hashlib.sha256(d).hexdigest(),strings=strings,references=refs,needMoreInputSites=constants,exports=exports,deviceAccess=False)
(OUT/'focus-index075.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(dict(strings=len(strings),references=len(refs),constantSites=constants,exports=len(exports)),indent=2))
