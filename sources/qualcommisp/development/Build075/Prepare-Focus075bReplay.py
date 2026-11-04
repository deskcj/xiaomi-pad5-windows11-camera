from pathlib import Path
import struct
R=Path(__file__).resolve().parents[1];obj=(R/'tests/FocusFlags075b.obj').read_bytes()
machine,n,_,_,_,opt,_=struct.unpack_from('<HHIIIHH',obj)
assert machine==0xaa64
blocks=[]
for i in range(n):
 at=20+opt+i*40;name=obj[at:at+8].rstrip(b'\0');size,raw,relocs=struct.unpack_from('<III',obj,at+16)
 if name==b'.text':
  assert struct.unpack_from('<H',obj,at+32)[0]==0,'Relocations not allowed'
  blocks.append(obj[raw:raw+size])
assert len(blocks)==1 and len(blocks[0])==0x5c,(len(blocks),[len(x) for x in blocks])
(R/'research/focus/flags075b.bin').write_bytes(blocks[0])
old=(R/'tools/Replay-FocusGet075.py').read_text()
start=old.index('for rva in (0x16d934,0x16d940,0x16d94c):')
end=old.index('patched=bytes(patched)',start)
replacement="""block=(OUT/'flags075b.bin').read_bytes()
assert len(block)==0x5c
for i in range(0,len(block),4):
 rva=0x16d91c+i;at=offset(rva)
 before=struct.unpack_from('<I',original,at)[0];after=struct.unpack_from('<I',block,i)[0]
 struct.pack_into('<I',patched,at,after)
 patches.append(dict(rva=hex(rva),fileOffset=at,old=hex(before),new=hex(after),meaning='Rebuild current mode/range while preserving other flags and AUTO lock.'))
"""
old=old[:start]+replacement+old[end:]
old=old.replace('for mode in range(5):','for mode in range(6):')
old=old.replace('for flags in (0,1,2,0x100,0x20101,0x70107):','for flags in (0,1,2,4,5,0x100,0x104,0x20101,0x70107,0x8000000000000000,0x20300):')
old=old.replace("expected=2 if mode==0 else (0x20000,0x10000,0x40000)[focusrange]|(0x100 if mode in (3,4) else 1)","expected=2 if mode==0 else (flags & ~(0x1f0103 | (4 if mode in (3,4) else 0))) | (0x20000,0x10000,0x40000)[focusrange] | (0x100 if mode in (3,4) else 1)")
old=old.replace('PASS075_ORIGINAL_FOCUS_GET_STALE_FLAGS_REPRODUCED_AND_CORRECTED','PASS075B_FOCUS_GET_MODE_RANGE_FIXED_AUTO_LOCK_PRESERVED')
old=old.replace('focus-get-proof075.json','focus-get-proof075b.json').replace('QcDeviceMFT8150-focus075-UNSIGNED.dll','QcDeviceMFT8150-focus075b-UNSIGNED.dll')
old=old.replace('three-word correction','in-place block correction').replace('Only known runtime modes0..4/ranges0..2 are covered.','Runtime modes0..5/ranges0..2 are covered; AUTO lock and non-mode flags preserved.')
(R/'tools/Replay-FocusGet075b.py').write_text(old)
print('PASS075B_PRIVATE_REPLAY_PREPARED')
