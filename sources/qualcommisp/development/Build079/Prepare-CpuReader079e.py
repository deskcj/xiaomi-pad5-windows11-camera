from pathlib import Path
b=Path(__file__).resolve().parents[1]
s=(b/'src/ReadCpu079d.cs').read_text(encoding='utf-8-sig').replace('ReadCpu079d','ReadCpu079e')
s=s.replace('int totalSamples=0,targetSamples=0,allSwitches=0;', '''var globalSamples=new Dictionary<string,int>();
   var ready=new Dictionary<int,double>();var dispatch=new Dictionary<int,List<double>>();var readyCauses=new Dictionary<string,int>();
   int totalReady=0;
   int totalSamples=0,targetSamples=0,allSwitches=0;''')
s=s.replace('if(!targets.TryGetValue(pid,out target))continue;', '''Count(globalSamples,((int)(sample.TimeStampRelativeMSec/1000))+":"+pid+":"+sample.ProcessName);
     if(!targets.TryGetValue(pid,out target))continue;''')
s=s.replace('var cs=ev as CSwitchTraceData;', '''var re=ev as DispatcherReadyThreadTraceData;
    if(re!=null){++totalReady;var awakened=threadAt(re.AwakenedThreadID,re.TimeStampRelativeMSec);
     if(awakened!=null){ready[awakened.ThreadID]=re.TimeStampRelativeMSec;Count(readyCauses,awakened.Process.ProcessID+":"+awakened.ThreadID+":by:"+re.ProcessID+":"+re.ThreadID+":"+re.AdjustReason+":"+re.Flags);}
     continue;
    }
    var cs=ev as CSwitchTraceData;''')
s=s.replace('var next=threadAt(cs.NewThreadID,cs.TimeStampRelativeMSec);OutState last;', '''var next=threadAt(cs.NewThreadID,cs.TimeStampRelativeMSec);OutState last;
    double readyAt;if(next!=null && ready.TryGetValue(next.ThreadID,out readyAt)){
     List<double> ds;if(!dispatch.TryGetValue(next.ThreadID,out ds)){ds=new List<double>();dispatch[next.ThreadID]=ds;}
     ds.Add(cs.TimeStampRelativeMSec-readyAt);ready.Remove(next.ThreadID);
    }''')
s=s.replace('var result=new {status=', '''var dispatchRows=dispatch.Select(k=>new {tid=k.Key,pid=tidMap[k.Key][0].Process.ProcessID,count=k.Value.Count,totalMS=k.Value.Sum(),medianMS=k.Value.OrderBy(x=>x).ElementAt(k.Value.Count/2),maxMS=k.Value.Max(),over5=k.Value.Count(x=>x>5),over10=k.Value.Count(x=>x>10),over20=k.Value.Count(x=>x>20)}).OrderByDescending(k=>k.totalMS).ToArray();
   var result=new {globalSamples=Top(globalSamples,100000),totalReady=totalReady,dispatch=dispatchRows,readyCauses=Top(readyCauses,300),status=''')
(b/'src/ReadCpu079e.cs').write_text(s,encoding='utf-8-sig')
for name in ('Build-CpuReader079d.ps1','Read-Cpu079d-WithJournal.ps1'):
 t=(b/'tools'/name).read_text(encoding='utf-8-sig').replace('079d','079e').replace('079D','079E')
 (b/'tools'/name.replace('079d','079e')).write_text(t,encoding='utf-8-sig')
print('PASS079E_OFFLINE_SOURCE_PREPARED')
