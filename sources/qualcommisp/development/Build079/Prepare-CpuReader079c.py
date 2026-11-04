from pathlib import Path
b=Path(__file__).resolve().parents[1]
s=(b/'src/ReadCpu079.cs').read_text(encoding='utf-8-sig')
s=s.replace('public static class ReadCpu079', 'public static class ReadCpu079c')
s=s.replace('public Dictionary<string,int> MftStack=new Dictionary<string,int>();', '''public Dictionary<string,int> MftStack=new Dictionary<string,int>();
  public Dictionary<string,int> Bins=new Dictionary<string,int>();
  public Dictionary<string,int> StackBins=new Dictionary<string,int>();
  public Dictionary<string,int> FullStacks=new Dictionary<string,int>();''')
s=s.replace('static string Location(TraceProcess p,ulong address,double time) {', '''static TraceProcess system;
 static string Location(TraceProcess p,ulong address,double time) {''')
s=s.replace('return "unmapped+0x"+address.ToString("x");', '''if(system!=null && p!=system && address>=0xffff000000000000UL) return Location(system,address,time);
  return "unmapped+0x"+address.ToString("x");''')
s=s.replace('var targets=new Dictionary<int,Target>();', '''system=log.Processes.FirstOrDefault(p=>p.ProcessID==4);
   var targets=new Dictionary<int,Target>();''')
s=s.replace('Count(target.MftStack,MftOnStack(log,sample,target.Process));', '''string nearest=MftOnStack(log,sample,target.Process);
     Count(target.MftStack,nearest);
     int bin=(int)(sample.TimeStampRelativeMSec/1000);
     Count(target.Bins,bin+":"+Location(target.Process,sample.InstructionPointer,sample.TimeStampRelativeMSec));
     Count(target.StackBins,bin+":"+nearest);
     if(nearest!="none") {
      var st=log.GetCallStackForEvent(sample);int depth=0;var chain=new List<string>();
      while(st!=null && depth++<20){chain.Add(Location(target.Process,st.CodeAddress.Address,sample.TimeStampRelativeMSec));st=st.Caller;}
      Count(target.FullStacks,string.Join(";",chain.ToArray()));
     }''')
s=s.replace('topMftStack=Top(t.MftStack,100)', 'topMftStack=Top(t.MftStack,100),bins=Top(t.Bins,100000),stackBins=Top(t.StackBins,100000),fullStacks=Top(t.FullStacks,150)')
s=s.replace('source=args[0],durationMS=', 'source=args[0],sessionStart=log.SessionStartTime.ToString("o"),sessionEnd=log.SessionEndTime.ToString("o"),durationMS=')
s=s.replace('tid=x.Key,samples=x.Value', 'tid=x.Key,pid=tidMap.ContainsKey(x.Key)?tidMap[x.Key][0].Process.ProcessID:-1,samples=x.Value')
(b/'src/ReadCpu079c.cs').write_text(s,encoding='utf-8-sig')
t=(b/'tools/Build-CpuReader079b.ps1').read_text(encoding='utf-8-sig').replace('ReadCpu079b.exe','ReadCpu079c.exe').replace('src\\ReadCpu079.cs','src\\ReadCpu079c.cs').replace('PASS079B','PASS079C')
(b/'tools/Build-CpuReader079c.ps1').write_text(t,encoding='utf-8-sig')
t=(b/'tools/Read-Cpu079b-WithJournal.ps1').read_text(encoding='utf-8-sig').replace('cpu-reader079b','cpu-reader079c').replace('cpu-top079b','cpu-top079c').replace('ReadCpu079b.exe','ReadCpu079c.exe')
(b/'tools/Read-Cpu079c-WithJournal.ps1').write_text(t,encoding='utf-8-sig')
print('PASS079C_OFFLINE_SOURCE_PREPARED')
