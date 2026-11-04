"""One explicit documented file-cache working-set flush after measured pressure.

No hard limits requested, no registry changes, no process termination or deletion.
https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-setsystemfilecachesize
"""
from pathlib import Path
import ctypes as c,ctypes.wintypes as w,json,datetime,subprocess,os
R=Path(__file__).resolve().parents[1];dest=R/'research/cache-trim079.json'
if dest.exists():raise RuntimeError('Single-use report exists; inspect it, do not repeat automatically')
k=c.WinDLL('kernel32',use_last_error=True);a=c.WinDLL('advapi32',use_last_error=True)
class Memory(c.Structure):
 _fields_=[('length',w.DWORD),('load',w.DWORD)]+[(n,c.c_ulonglong) for n in ('totalPhys','availPhys','totalPage','availPage','totalVirtual','availVirtual','availExtended')]
class Luid(c.Structure):_fields_=[('low',w.DWORD),('high',w.LONG)]
class Priv(c.Structure):_fields_=[('count',w.DWORD),('luid',Luid),('attrs',w.DWORD)]
k.GlobalMemoryStatusEx.argtypes=[c.POINTER(Memory)];k.GlobalMemoryStatusEx.restype=w.BOOL
k.GetSystemFileCacheSize.argtypes=[c.POINTER(c.c_size_t),c.POINTER(c.c_size_t),c.POINTER(w.DWORD)];k.GetSystemFileCacheSize.restype=w.BOOL
k.SetSystemFileCacheSize.argtypes=[c.c_size_t,c.c_size_t,w.DWORD];k.SetSystemFileCacheSize.restype=w.BOOL
k.CreateFileW.argtypes=[w.LPCWSTR,w.DWORD,w.DWORD,c.c_void_p,w.DWORD,w.DWORD,w.HANDLE];k.CreateFileW.restype=w.HANDLE
k.GetCurrentProcess.restype=w.HANDLE;k.CloseHandle.argtypes=[w.HANDLE]
a.OpenProcessToken.argtypes=[w.HANDLE,w.DWORD,c.POINTER(w.HANDLE)];a.OpenProcessToken.restype=w.BOOL
a.LookupPrivilegeValueW.argtypes=[w.LPCWSTR,w.LPCWSTR,c.POINTER(Luid)];a.LookupPrivilegeValueW.restype=w.BOOL
a.AdjustTokenPrivileges.argtypes=[w.HANDLE,w.BOOL,c.POINTER(Priv),w.DWORD,c.POINTER(Priv),c.POINTER(w.DWORD)];a.AdjustTokenPrivileges.restype=w.BOOL
def checked(ok):
 if not ok:raise c.WinError(c.get_last_error())
def memory():
 m=Memory();m.length=c.sizeof(m);checked(k.GlobalMemoryStatusEx(c.byref(m)));return dict(availableMiB=m.availPhys//1048576,loadPercent=m.load,availableCommitMiB=m.availPage//1048576)
def limits():
 lo=c.c_size_t();hi=c.c_size_t();flags=w.DWORD();checked(k.GetSystemFileCacheSize(c.byref(lo),c.byref(hi),c.byref(flags)));return dict(minimum=lo.value,maximum=hi.value,flags=flags.value)
report=dict(status='FAILED',pid=os.getpid(),time=datetime.datetime.now().astimezone().isoformat(),filesDeleted=False,processesTerminated=False,registryChanged=False,driverChanged=False,errors=[])
lock=k.CreateFileW(r'C:\ProgramData\CodexQispBuild062\operation.lock',0xc0000000,0,None,3,0x80,None)
if lock==c.c_void_p(-1).value:raise c.WinError(c.get_last_error())
token=w.HANDLE();previous=Priv();adjusted=False
try:
 svc=subprocess.run(['C:/Windows/System32/sc.exe','query','FrameServer'],capture_output=True,text=True,check=True).stdout
 if 'STOPPED' not in svc:raise RuntimeError('FrameServer must be stopped for this resource-maintenance scope')
 report['beforeMemory']=memory();report['beforeLimits']=limits()
 if report['beforeMemory']['availableMiB']>=650:report['status']='SKIPPED_MEMORY_ALREADY_SUFFICIENT'
 else:
  checked(a.OpenProcessToken(k.GetCurrentProcess(),0x28,c.byref(token)))
  request=Priv();request.count=1;request.attrs=2
  checked(a.LookupPrivilegeValueW(None,'SeIncreaseQuotaPrivilege',c.byref(request.luid)))
  length=w.DWORD(c.sizeof(previous));c.set_last_error(0)
  checked(a.AdjustTokenPrivileges(token,False,c.byref(request),c.sizeof(previous),c.byref(previous),c.byref(length)))
  if c.get_last_error():raise c.WinError(c.get_last_error())
  adjusted=True
  checked(k.SetSystemFileCacheSize(c.c_size_t(-1),c.c_size_t(-1),0))
  report['afterLimits']=limits();report['afterMemory']=memory()
  if report['afterLimits']!=report['beforeLimits']:
   old=report['beforeLimits'];flags=(1 if old['flags']&1 else 2)|(4 if old['flags']&4 else 8)
   checked(k.SetSystemFileCacheSize(old['minimum'],old['maximum'],flags))
   report['restoredLimits']=limits()
   if report['restoredLimits']!=old:raise RuntimeError('Cache limit restoration mismatch')
  report['status']='PASS079_ONE_FILE_CACHE_WORKING_SET_FLUSH_LIMITS_RETAINED'
except Exception as e:report['errors'].append(str(e))
finally:
 if adjusted:a.AdjustTokenPrivileges(token,False,c.byref(previous),0,None,None)
 if token:k.CloseHandle(token)
 k.CloseHandle(lock)
 dest.write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
