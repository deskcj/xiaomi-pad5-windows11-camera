"""Original focus GET producer and in-place block correction in private ARM64 memory.

No camera, installed DLL or sensor/motor commands are used. Virtual property
lookups, locks, CFG, logging and MSVC frame helpers are declared test stubs.
"""
from pathlib import Path
import importlib.util,hashlib,json,struct,gc
from unicorn import Uc,UC_ARCH_ARM64,UC_MODE_ARM,UC_HOOK_CODE
from unicorn.arm64_const import *
R=Path(__file__).resolve().parents[2];OUT=R/'Build075/research/focus'
spec=importlib.util.spec_from_file_location('pe075',R/'Build074/research/convergence/NativeGraphReplay074.py')
pe=importlib.util.module_from_spec(spec);spec.loader.exec_module(pe)
original=pe.dll;patched=bytearray(original);patches=[]
def offset(rva):
 return next(ro+rva-va for name,va,ro,rs in pe.sections if va<=rva<va+rs)
block=(OUT/'flags075b.bin').read_bytes()
assert len(block)==0x5c
for i in range(0,len(block),4):
 rva=0x16d91c+i;at=offset(rva)
 before=struct.unpack_from('<I',original,at)[0];after=struct.unpack_from('<I',block,i)[0]
 struct.pack_into('<I',patched,at,after)
 patches.append(dict(rva=hex(rva),fileOffset=at,old=hex(before),new=hex(after),meaning='Rebuild current mode/range while preserving other flags and AUTO lock.'))
patched=bytes(patched)
SYN=0x2000000;STACK=0x3000000;END=0x9f1000
CTX=SYN+0x10000;PIPE=SYN+0x1000;PVT=SYN+0x2000;CTRL=SYN+0x3000;CVT=SYN+0x4000;LVT=SYN+0x5000;PROP=SYN+0x8000
STUBS={0x9f0000:'get_controller',0x9f0010:'get_focus',0x9f0020:'lock',0x9f0030:'unlock',0x9f0040:'cfg'}
def engine(image):
 u=Uc(UC_ARCH_ARM64,UC_MODE_ARM);u.mem_map(0,0xc00000)
 for name,va,ro,rs in pe.sections:
  if rs:u.mem_write(va,image[ro:ro+rs])
 imagebase=struct.unpack_from('<Q',image,pe.pe+48)[0]
 va,ro,rs=next((va,ro,rs) for name,va,ro,rs in pe.sections if name=='.reloc');cursor=ro
 while cursor<ro+rs:
  page,z=struct.unpack_from('<II',image,cursor)
  if not page or not z:break
  for at in range(cursor+8,cursor+z,2):
   entry=struct.unpack_from('<H',image,at)[0]
   if entry>>12==10:
    addr=page+(entry&4095);v=struct.unpack('<Q',u.mem_read(addr,8))[0]
    u.mem_write(addr,struct.pack('<Q',(v-imagebase)&0xffffffffffffffff))
  cursor+=z
 u.mem_map(SYN,0x40000);u.mem_map(STACK,0x100000)
 put=lambda a,v:u.mem_write(a,struct.pack('<Q',v))
 put(0x604648,0x9f0040);put(0x972a00,0)
 put(PIPE,PVT);put(PVT+0xb0,0x9f0000);put(CTRL,CVT);put(CVT+0x38,0x9f0010)
 put(CTRL+8,LVT);put(LVT+8,0x9f0020);put(LVT+0x10,0x9f0030)
 put(CTX+0x10,PIPE);put(CTX+0xcb18,LVT)
 u.mem_write(CTX+0xcb4c,struct.pack('<ff',.25,10.));u.mem_write(CTX+0xcb5c,struct.pack('<I',333))
 calls=[]
 def ret(value=None):
  if value is not None:u.reg_write(UC_ARM64_REG_X0,value)
  u.reg_write(UC_ARM64_REG_PC,u.reg_read(UC_ARM64_REG_LR))
 def hook(uc,pc,size,_):
  if pc in (0x18b0,0x18c8):ret()
  elif pc==0xd2f8:ret(0)
  elif pc in STUBS:
   calls.append(STUBS[pc])
   if pc==0x9f0000:ret(CTRL)
   elif pc==0x9f0010:ret(PROP)
   elif pc==0x9f0040:ret()
   else:ret(0)
 u.hook_add(UC_HOOK_CODE,hook)
 return u,calls
results=[]
for label,image in (('original',original),('corrected',patched)):
 u,calls=engine(image)
 for mode in range(6):
  for focusrange in range(3):
   for flags in (0,1,2,4,5,0x100,0x104,0x20101,0x70107,0x8000000000000000,0x20300):
    u.mem_write(CTX+0x84,struct.pack('<I',mode));u.mem_write(CTX+0xbc,struct.pack('<I',focusrange))
    u.mem_write(PROP,bytes(0xa0));u.mem_write(PROP+0x70,struct.pack('<Q',flags))
    context=bytes(u.mem_read(CTX,0xcd00));calls.clear()
    for reg,v in ((UC_ARM64_REG_X0,CTX),(UC_ARM64_REG_SP,STACK+0xff000),(UC_ARM64_REG_LR,END)):u.reg_write(reg,v)
    try:u.emu_start(0x16d6f0,END,count=10000)
    except Exception as e:raise RuntimeError(f'{label} pc={u.reg_read(UC_ARM64_REG_PC):x} lr={u.reg_read(UC_ARM64_REG_LR):x} calls={calls}') from e
    assert u.reg_read(UC_ARM64_REG_PC)==END and u.reg_read(UC_ARM64_REG_X0)==0
    actual=struct.unpack('<Q',u.mem_read(PROP+0x70,8))[0]
    expected=2 if mode==0 else (flags & ~(0x1f0103 | (4 if mode in (3,4) else 0))) | (0x20000,0x10000,0x40000)[focusrange] | (0x100 if mode in (3,4) else 1)
    assert bytes(u.mem_read(CTX,0xcd00))==context,'GET modified capture mode/sensor context'
    assert calls.count('lock')==calls.count('unlock')==2
    if label=='corrected':assert actual==expected,(mode,focusrange,hex(flags),hex(actual),hex(expected))
    results.append(dict(image=label,mode=mode,range=focusrange,oldFlags=hex(flags),actual=hex(actual),expected=hex(expected),valid=actual==expected))
 del u;gc.collect()
report=dict(status='PASS075B_FOCUS_GET_MODE_RANGE_FIXED_AUTO_LOCK_PRESERVED',sourceSHA=hashlib.sha256(original).hexdigest(),unsignedCandidateSHA=hashlib.sha256(patched).hexdigest(),patches=patches,cases=results,deviceAccess=False,sensorModeUnchanged=True,
 limits=['Tests execute original ARM64 GET instructions on synthetic controller/property objects.','Runtime modes0..5/ranges0..2 are covered; AUTO lock and non-mode flags preserved. Front fixed-focus capability path is not changed.','No new motor speed or optical focus improvement is claimed.'])
(OUT/'focus-get-proof075b.json').write_text(json.dumps(report,indent=2)+'\n')
candidate=R/'Build075/research/QcDeviceMFT8150-focus075b-UNSIGNED.dll';candidate.write_bytes(patched)
print(json.dumps(dict(status=report['status'],cases=len(results),originalIncorrect=sum(not x['valid'] for x in results if x['image']=='original'),correctedIncorrect=sum(not x['valid'] for x in results if x['image']=='corrected')),indent=2))
