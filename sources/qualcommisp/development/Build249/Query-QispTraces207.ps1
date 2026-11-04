param([switch]$ImportOnly,[switch]$DiagnosticOnly,[string]$OutputFile)
# Native read-only ETW enumeration; direct invocation returns one typed object.
# Import with -ImportOnly. Unprivileged diagnostic results never prove absence of sessions.
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
function Get-QispTraces207 {
 param([switch]$DiagnosticOnly)
 $administrator=([Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
 if(-not $administrator -and -not $DiagnosticOnly){throw 'Elevated administrator token required for complete ETW visibility; unprivileged zero is not a no-session proof'}
 if(-not ('QispEtwQuery207' -as [type])){
  Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;
public static class QispEtwQuery207 {
 [StructLayout(LayoutKind.Sequential)]
 public struct WNODE_HEADER {
  public UInt32 BufferSize, ProviderId;
  public UInt64 HistoricalContext, TimeStamp;
  public Guid Guid;
  public UInt32 ClientContext, Flags;
 }
 [StructLayout(LayoutKind.Sequential)]
 public struct EVENT_TRACE_PROPERTIES {
  public WNODE_HEADER Wnode;
  public UInt32 BufferSize, MinimumBuffers, MaximumBuffers, MaximumFileSize;
  public UInt32 LogFileMode, FlushTimer, EnableFlags;
  public Int32 AgeLimit;
  public UInt32 NumberOfBuffers, FreeBuffers, EventsLost, BuffersWritten;
  public UInt32 LogBuffersLost, RealTimeBuffersLost;
  public IntPtr LoggerThreadId;
  public UInt32 LogFileNameOffset, LoggerNameOffset;
 }
 [DllImport("advapi32.dll", CharSet=CharSet.Unicode, ExactSpelling=true)]
 private static extern UInt32 QueryAllTracesW([In,Out] IntPtr[] properties, UInt32 count, out UInt32 loggerCount);
 public sealed class Result {
  public UInt32 NativeStatus, LoggerCount;
  public Int32 PointerBytes, PropertiesBytes, NameChars, Attempts;
  public String[] SessionNames;
  public UInt32[] AttemptStatuses, AttemptLoggerCounts;
  public Int32[] AttemptCapacities;
 }
 public static Result Query() {
  if(IntPtr.Size != 8) throw new NotSupportedException("Query207 requires x64/arm64 8-byte pointers.");
  int size=Marshal.SizeOf(typeof(EVENT_TRACE_PROPERTIES));
  if(Marshal.SizeOf(typeof(WNODE_HEADER))!=48 || size!=120 ||
     Marshal.OffsetOf(typeof(WNODE_HEADER),"Guid").ToInt32()!=24 ||
     Marshal.OffsetOf(typeof(WNODE_HEADER),"Flags").ToInt32()!=44 ||
     Marshal.OffsetOf(typeof(EVENT_TRACE_PROPERTIES),"LoggerThreadId").ToInt32()!=104 ||
     Marshal.OffsetOf(typeof(EVENT_TRACE_PROPERTIES),"LogFileNameOffset").ToInt32()!=112 ||
     Marshal.OffsetOf(typeof(EVENT_TRACE_PROPERTIES),"LoggerNameOffset").ToInt32()!=116)
   throw new InvalidOperationException("Native EVENT_TRACE_PROPERTIES x64/arm64 layout differs.");
  const int chars=1024, maxCapacity=1024, maxAttempts=3;
  int bufferBytes=checked(size+4*chars), capacity=256;
  var statuses=new List<UInt32>();var counts=new List<UInt32>();var capacities=new List<Int32>();
  for(int attempt=0;attempt<maxAttempts;attempt++) {
   var buffers=new IntPtr[capacity];
   try {
    byte[] zero=new byte[bufferBytes];
    for(int i=0;i<capacity;i++) {
     buffers[i]=Marshal.AllocHGlobal(bufferBytes);
     Marshal.Copy(zero,0,buffers[i],bufferBytes);
     var p=new EVENT_TRACE_PROPERTIES();
     p.Wnode.BufferSize=(UInt32)bufferBytes;
     p.LoggerNameOffset=(UInt32)size;
     p.LogFileNameOffset=(UInt32)(size+2*chars);
     Marshal.StructureToPtr(p,buffers[i],false);
    }
    UInt32 total;
    UInt32 status=QueryAllTracesW(buffers,(UInt32)capacity,out total);
    statuses.Add(status);counts.Add(total);capacities.Add(capacity);
    if(status==234) { // ERROR_MORE_DATA: discard partial data, bounded retry only.
     if(capacity>=maxCapacity || total>maxCapacity || attempt+1>=maxAttempts)
      throw new Win32Exception(234,"QueryAllTracesW ERROR_MORE_DATA beyond bounded1024/3 retries; enumeration unproven.");
     capacity=Math.Min(maxCapacity,Math.Max(capacity*2,(int)total));
     continue;
    }
    if(status!=0) throw new Win32Exception((int)status,"QueryAllTracesW failed; nativeStatus="+status+"; enumeration unproven.");
    if(total>(UInt32)capacity) throw new InvalidOperationException("Native success returned loggerCount above array capacity.");
    var names=new List<String>();var seen=new HashSet<String>(StringComparer.OrdinalIgnoreCase);
    for(int i=0;i<(int)total;i++) {
     var p=(EVENT_TRACE_PROPERTIES)Marshal.PtrToStructure(buffers[i],typeof(EVENT_TRACE_PROPERTIES));
     if(p.Wnode.BufferSize<(UInt32)size || p.Wnode.BufferSize>(UInt32)bufferBytes ||
        p.LoggerNameOffset<(UInt32)size || p.LoggerNameOffset>p.Wnode.BufferSize-2 || (p.LoggerNameOffset&1)!=0)
      throw new InvalidOperationException("Native session name offset/extent invalid; index="+i);
     int limit=Math.Min(chars,((int)p.Wnode.BufferSize-(int)p.LoggerNameOffset)/2);
     var name=new StringBuilder();bool terminated=false;
     for(int n=0;n<limit;n++) {
      ushort c=(ushort)Marshal.ReadInt16(buffers[i],checked((int)p.LoggerNameOffset+2*n));
      if(c==0){terminated=true;break;}name.Append((char)c);
     }
     if(!terminated || name.Length==0) throw new InvalidOperationException("Native logger name missing or unterminated; index="+i);
     String text=name.ToString();if(!seen.Add(text)) throw new InvalidOperationException("Native logger name duplicate; enumeration unproven.");
     names.Add(text);
    }
    return new Result {NativeStatus=0,LoggerCount=total,PointerBytes=IntPtr.Size,PropertiesBytes=size,
      NameChars=chars,Attempts=attempt+1,SessionNames=names.ToArray(),AttemptStatuses=statuses.ToArray(),
      AttemptLoggerCounts=counts.ToArray(),AttemptCapacities=capacities.ToArray()};
   } finally {foreach(IntPtr buffer in buffers) if(buffer!=IntPtr.Zero) Marshal.FreeHGlobal(buffer);}
  }
  throw new InvalidOperationException("Bounded native enumeration exhausted without success.");
 }
}
'@ | Out-Null
 }
 $native=[QispEtwQuery207]::Query()
 $names=@($native.SessionNames)
 $qisp=@($names|Where-Object {$_ -match 'Qisp\d'})
 $status=if($administrator){'PASS207_NATIVE_ETW_ENUMERATION'}else{'LIMITED207_NATIVE_ETW_ENUMERATION_NOT_A_NO_SESSION_PROOF'}
 [pscustomobject][ordered]@{status=$status;exit=$native.NativeStatus;api='advapi32!QueryAllTracesW';nativeStatus=$native.NativeStatus;count=$native.LoggerCount;sessions=$names;qispPattern='Qisp\d';qispSessions=$qisp;qispCount=$qisp.Count;noQispSessions=($administrator -and $qisp.Count -eq 0);administrator=$administrator;fullVisibilityRequired=$true;pointerBytes=$native.PointerBytes;propertiesBytes=$native.PropertiesBytes;nameCapacityChars=$native.NameChars;attempts=$native.Attempts;attemptStatuses=@($native.AttemptStatuses);attemptLoggerCounts=@($native.AttemptLoggerCounts);attemptCapacities=@($native.AttemptCapacities);WMIUsed=$false;logmanUsed=$false;sessionChanges=$false;cameraActions=$false;deviceActions=$false;processTermination=$false;queried=(Get-Date -Format o)}
}
function Assert-NoQispTraces207 {
 $report=Get-QispTraces207
 if($report.status -ne 'PASS207_NATIVE_ETW_ENUMERATION' -or $report.exit -ne 0 -or -not $report.administrator -or -not $report.noQispSessions -or $report.qispCount -ne 0){throw ('Native Qisp ETW sessions remain or enumeration rejected: '+($report.qispSessions -join ', '))}
 $report
}
if(-not $ImportOnly){
 $report=Get-QispTraces207 -DiagnosticOnly:$DiagnosticOnly
 if($OutputFile){if(Test-Path -LiteralPath $OutputFile){throw 'Preserve prior207 native query receipt'};$report|ConvertTo-Json -Depth 6|Set-Content -LiteralPath $OutputFile -Encoding UTF8}
 $report
}
