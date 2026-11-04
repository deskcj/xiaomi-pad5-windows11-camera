from pathlib import Path
import struct,json,itertools
from unicorn import Uc,UC_ARCH_ARM64,UC_MODE_ARM,UC_HOOK_CODE,UC_PROT_READ,UC_PROT_WRITE,UC_PROT_EXEC
from unicorn.arm64_const import *
R=Path(__file__).resolve().parents[2];B=R/'Build080';base=0x140000000;data=0x40000000;stack=0x50000000;stub=0x60000000;stop=stub+0x100
paths={'stock':R.parent/'Win11Pad5/bypass-driver/codex/AV366_QUALCOMM107_BUFFER/native/AVStreamStock/qccamavs8150.sys','candidate':B/'candidate/AVStream/qccamavs8150.sys'}
def image(p):
 d=p.read_bytes();pe=struct.unpack_from('<I',d,60)[0];o=pe+24;st=o+struct.unpack_from('<H',d,pe+20)[0];img=bytearray(struct.unpack_from('<I',d,o+56)[0]);hs=struct.unpack_from('<I',d,o+60)[0];img[:hs]=d[:hs]
 for i in range(struct.unpack_from('<H',d,pe+6)[0]):
  va,rs,ro=struct.unpack_from('<III',d,st+i*40+12);img[va:va+rs]=d[ro:ro+rs]
 return img
def execute(img,entries,mode,counts=(4,2),skip=(),missing=()):
 u=Uc(UC_ARCH_ARM64,UC_MODE_ARM);u.mem_map(base,(len(img)+4095)&~4095);u.mem_write(base,bytes(img));u.mem_map(data,0x10000);u.mem_map(stack,0x10000);u.mem_map(stub,0x1000);u.mem_write(stub,bytes.fromhex('c0035fd6')*1024)
 def q(a,v):u.mem_write(a,struct.pack('<Q',v))
 def w(a,v):u.mem_write(a,struct.pack('<I',v))
 node=data;packet=data+0x1000;inputs=data+0x3000;outputs=data+0x3100
 u.mem_write(node+8,bytes(counts));q(node+0x10,inputs);q(node+0x18,outputs)
 for group,array,count in [('in',inputs,4),('out',outputs,2)]:
  for index in range(count):
   port=data+0x4000+(index+(0 if group=='in' else 4))*0x400;manager=port+0x100;obj=port+0x200;vt=port+0x300
   q(array+index*8,0 if (group,index) in missing else port);q(port+0x20,manager);q(port,vt);q(vt+8,stub+0x10);q(vt+0x50,stub+0x20);q(manager+0x78,obj);q(obj,vt);q(port+0x78,obj)
   if (group,index) in skip:u.mem_write(manager+0x80,b'\x01')
 w(packet,0x10000000);q(packet+8,123456789);w(packet+0x20,0x80);w(packet+0x24,len(entries))
 for i,(direction,resource) in enumerate(entries):
  io=packet+0x80+i*0x114;w(io+0x154,direction);w(io+0x12c,resource)
 q(base+0x27398,stub);calls=[];forbidden=[]
 def hook(uc,addr,size,user):
  if base+0x92000<=addr<base+0x92800:forbidden.append(hex(addr));raise AssertionError('Relay entered')
  if addr==stub+0x10:uc.reg_write(UC_ARM64_REG_X0,1)
  elif addr==stub+0x20:
   calls.append((uc.reg_read(UC_ARM64_REG_X0)-data,uc.reg_read(UC_ARM64_REG_X1)));uc.reg_write(UC_ARM64_REG_X0,0)
  elif addr==base+0x1bef0:uc.reg_write(UC_ARM64_REG_PC,uc.reg_read(UC_ARM64_REG_LR))
  elif addr==stop:uc.emu_stop()
 u.hook_add(UC_HOOK_CODE,hook)
 u.reg_write(UC_ARM64_REG_SP,stack+0xf000);u.reg_write(UC_ARM64_REG_LR,stop);u.reg_write(UC_ARM64_REG_X0,node);u.reg_write(UC_ARM64_REG_X1,packet);u.reg_write(UC_ARM64_REG_X2,mode)
 saved={r:0x12340000+i*0x111 for i,r in enumerate(range(UC_ARM64_REG_X19,UC_ARM64_REG_X28+1))}
 for r,v in saved.items():u.reg_write(r,v)
 u.emu_start(base+0x7f4d8,stop+4,count=10000)
 assert u.reg_read(UC_ARM64_REG_PC)==stop and u.reg_read(UC_ARM64_REG_SP)==stack+0xf000
 assert all(u.reg_read(r)==v for r,v in saved.items())
 return dict(calls=calls,fixture=bytes(u.mem_read(data,0x10000)).hex(),forbidden=forbidden)
imgs={k:image(p) for k,p in paths.items()};rows=[]
cases=[]
for resource in range(5):
 for mode in (0,1,3):cases.append(([(1,resource)],mode,(4,2),(),()))
cases += [([],0,(4,2),(),()), ([(1,0),(1,1),(2,8),(2,9)],3,(4,2),(),()), ([(1,0)],0,(0,2),(),()), ([(1,0)],0,(4,2),(('in',0),),()), ([(1,0)],0,(4,2),(),(('in',0),)), ([(1,0),(1,0)],1,(4,2),(),())]
for i,case in enumerate(cases):
 a=execute(imgs['stock'],*case);b=execute(imgs['candidate'],*case);assert a==b,(i,case,a['calls'],b['calls'])
 rows.append(dict(case=i,entries=case[0],mode=case[1],inputOutputCounts=case[2],skip=case[3],missing=case[4],nativeReleaseCalls=a['calls'],sameFullFixture=True,calleeRegistersAndStackPreserved=True,relayEntered=False))
out=dict(status='PASS080_EXACT_ORIGINAL_ARM64_IPE_FLUSH_RELEASE_REPLAY',cases=rows,deviceAccess=False,scope='Original stock and candidate complete IpeNode::FlushIOBuffers, original82f50 wrapper, virtual release stub; no relay entry and identical full fixtures/callee registers/stack.')
(B/'research/release-replay080.json').write_text(json.dumps(out,indent=2)+'\n');print('PASS080_EXACT_STOCK_RELEASE_REPLAY cases='+str(len(rows)))
