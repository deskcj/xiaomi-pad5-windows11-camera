from pathlib import Path
b=Path(__file__).resolve().parents[1]
s=(b/'src/ReadCpu079e.cs').read_text(encoding='utf-8-sig').replace('ReadCpu079e','ReadCpu079f')
s=s.replace('public Dictionary<string,int> FullStacks=', 'public Dictionary<string,int> CameraKernelStack=new Dictionary<string,int>(); public Dictionary<string,int> CameraKernelBins=new Dictionary<string,int>();\n  public Dictionary<string,int> FullStacks=')
s=s.replace('static object Top(', '''static string CameraOnStack(TraceLog log,TraceEvent ev,TraceProcess p){
  var st=log.GetCallStackForEvent(ev);int depth=0;
  while(st!=null && depth++<80){string n=st.CodeAddress.ModuleName??"";
   if(n.IndexOf("qccam",StringComparison.OrdinalIgnoreCase)>=0 || n.IndexOf("qcsmmu",StringComparison.OrdinalIgnoreCase)>=0) return Location(p,st.CodeAddress.Address,ev.TimeStampRelativeMSec);
   st=st.Caller;
  }return "none";
 }
 static object Top(''')
s=s.replace('if(cameraClient || mft)', 'if(cameraClient || mft || p.ProcessID==4)')
s=s.replace('if(nearest!="none") {', '''string cameraKernel=CameraOnStack(log,sample,target.Process);
     Count(target.CameraKernelStack,cameraKernel);Count(target.CameraKernelBins,bin+":"+cameraKernel);
     if(nearest!="none" || cameraKernel!="none") {''')
s=s.replace('fullStacks=Top(t.FullStacks,150)', 'fullStacks=Top(t.FullStacks,200),cameraKernelStack=Top(t.CameraKernelStack,200),cameraKernelBins=Top(t.CameraKernelBins,100000)')
(b/'src/ReadCpu079f.cs').write_text(s,encoding='utf-8-sig')
for name in ('Build-CpuReader079e.ps1','Read-Cpu079e-WithJournal.ps1'):
 t=(b/'tools'/name).read_text(encoding='utf-8-sig').replace('079e','079f').replace('079E','079F')
 (b/'tools'/name.replace('079e','079f')).write_text(t,encoding='utf-8-sig')
print('PASS079F_OFFLINE_SOURCE_PREPARED')
