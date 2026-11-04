"""Offline import, inspect and guarded binary editing; no device/registry access."""
from pathlib import Path
import argparse,json,hashlib,struct,re,zipfile
ROOT=Path(__file__).resolve().parents[1]
def sha(d):return hashlib.sha256(d).hexdigest()
def rva_offset(d,rva):
    pe=struct.unpack_from('<I',d,60)[0];n=struct.unpack_from('<H',d,pe+6)[0];at=pe+24+struct.unpack_from('<H',d,pe+20)[0]
    for i in range(n):
        vs,va,size,raw=struct.unpack_from('<IIII',d,at+i*40+8)
        if va<=rva<va+size:return raw+rva-va
    raise ValueError('RVA outside raw sections')
def import_zip(archive,dest):
    assert not dest.exists(),'Choose a new output directory'
    records=json.loads((ROOT/'release-files.json').read_text())
    with zipfile.ZipFile(archive) as z:
        for row in records:
            names=[n for n in z.namelist() if n.endswith('/Payload/Unified/'+row['path'])]
            assert len(names)==1,row['path']
            data=z.read(names[0]);assert sha(data)==row['sha256'],row['path']
            p=dest/row['path'];assert p.resolve().is_relative_to(dest.resolve());p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data)
    print('PASS: imported exact published QISP253 payload')
def tuning(path,out):
    d=path.read_bytes();assert d.startswith(b'QTI Chromatix Header')
    rows=[]
    # Verified native container layout: header section table at 0x98,
    # directory section0 and payload section1, with 72-byte records.
    directory_offset,section_count=struct.unpack_from('<II',d,0x98)
    assert directory_offset+24*section_count<=len(d)
    sections={tag:(off,size) for tag,off,size in (struct.unpack_from('<QQQ',d,directory_offset+24*i) for i in range(section_count))}
    start,directory_bytes=sections[0];database,data_bytes=sections[1]
    assert start+directory_bytes<=len(d) and database+data_bytes<=len(d) and directory_bytes%72==0
    for pos in range(start,start+directory_bytes,72):
        tag,name,aux0,aux1,relative,size=struct.unpack_from('<Q40sIIQQ',d,pos)
        assert relative+size<=data_bytes
        off=database+relative
        block=d[off:off+size]
        rows.append({'name':name[:32].split(b'\0',1)[0].decode(),'versionHex':name[32:40].hex(),'tag':tag,'directoryOffset':pos,'relativeDataOffset':relative,'fileOffset':off,'aux0':aux0,'aux1':aux1,'bytes':size,'sha256':sha(block),'hex':block.hex()})
    assert rows
    out.write_text(json.dumps({'fileSHA256':sha(d),'parser':d[0x28:0x48].split(b'\0')[0].decode(),'rows':rows},indent=2)+'\n')
    print('Exported',len(rows),'exact directory blocks; resolve references/schema before interpreting values')
def patch(source,recipe,out):
    d=bytearray(source.read_bytes());spec=json.loads(recipe.read_text())
    assert sha(d)==spec['inputSHA256'],'Wrong input build'
    assert source.resolve()!=out.resolve() and not out.exists(),'Use a new output file'
    spans=[]
    for row in spec['patches']:
        offset=rva_offset(d,int(row['rva'],0)) if 'rva' in row else int(str(row['offset']),0)
        old,new=bytes.fromhex(row['beforeHex']),bytes.fromhex(row['afterHex'])
        assert len(old)==len(new) and offset+len(old)<=len(d)
        assert d[offset:offset+len(old)]==old,'Original bytes mismatch'
        assert all(offset+len(old)<=a or offset>=b for a,b in spans),'Overlapping edits'
        spans.append((offset,offset+len(old)));d[offset:offset+len(old)]=new
    if spec.get('outputSHA256'):assert sha(d)==spec['outputSHA256']
    out.write_bytes(d);print('Written offline candidate:',sha(d),'Signature/catalog must be regenerated after editing')
ap=argparse.ArgumentParser();sub=ap.add_subparsers(dest='cmd',required=True)
for cmd,names in [('import',['archive','dest']),('tuning',['file','output']),('patch',['file','recipe','output'])]:
    s=sub.add_parser(cmd)
    for name in names:s.add_argument(name,type=Path)
a=ap.parse_args()
if a.cmd=='import':import_zip(a.archive,a.dest)
elif a.cmd=='tuning':tuning(a.file,a.output)
else:patch(a.file,a.recipe,a.output)
