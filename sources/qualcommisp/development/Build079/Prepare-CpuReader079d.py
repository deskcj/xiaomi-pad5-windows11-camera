from pathlib import Path
b=Path(__file__).resolve().parents[1]
s=(b/'src/ReadCpu079c.cs').read_text(encoding='utf-8-sig').replace('ReadCpu079c','ReadCpu079d')
s=s.replace('static TraceProcess system;', 'static TraceProcess system; static TraceModuleFile[] kernelModules;')
s=s.replace('if(system!=null && p!=system && address>=0xffff000000000000UL) return Location(system,address,time);', '''if(address>=0xffff000000000000UL) {
   foreach(var k in kernelModules) if(address>=k.ImageBase && address<k.ImageEnd) return k.Name+"+0x"+(address-k.ImageBase).ToString("x");
  }''')
s=s.replace('system=log.Processes.FirstOrDefault(p=>p.ProcessID==4);', '''system=log.Processes.FirstOrDefault(p=>p.ProcessID==4);
   kernelModules=log.ModuleFiles.Where(m=>m.ImageBase>=0xffff000000000000UL).ToArray();''')
s=s.replace('sessionStart=log.SessionStartTime', 'kernelModules=kernelModules.Select(m=>new {m.Name,m.FilePath,m.ImageBase,m.ImageSize,m.PdbName,m.PdbSignature,m.PdbAge}).ToArray(),sessionStart=log.SessionStartTime')
(b/'src/ReadCpu079d.cs').write_text(s,encoding='utf-8-sig')
for name in ('Build-CpuReader079c.ps1','Read-Cpu079c-WithJournal.ps1'):
 t=(b/'tools'/name).read_text(encoding='utf-8-sig').replace('079c','079d').replace('079C','079D')
 (b/'tools'/name.replace('079c','079d')).write_text(t,encoding='utf-8-sig')
print('PASS079D_SOURCE_PREPARED')
