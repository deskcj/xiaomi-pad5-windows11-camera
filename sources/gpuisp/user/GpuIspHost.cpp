#define NOMINMAX
#include <windows.h>
#include <string>
#include <cstdio>

static SERVICE_STATUS_HANDLE statusHandle;
static SERVICE_STATUS status={};static HANDLE stopEvent;
static const wchar_t* serviceName=L"CodexNabuGpuIsp";
static void Status(DWORD state,DWORD hint=0){
    status.dwServiceType=SERVICE_WIN32_OWN_PROCESS;status.dwCurrentState=state;
    status.dwControlsAccepted=state==SERVICE_RUNNING?SERVICE_ACCEPT_STOP|SERVICE_ACCEPT_SHUTDOWN:0;
    status.dwWin32ExitCode=NO_ERROR;status.dwWaitHint=hint;SetServiceStatus(statusHandle,&status);
}
static DWORD WINAPI Handler(DWORD code,DWORD,void*,void*){
    if(code==SERVICE_CONTROL_STOP||code==SERVICE_CONTROL_SHUTDOWN){Status(SERVICE_STOP_PENDING,30000);SetEvent(stopEvent);}return NO_ERROR;
}
static void Marker(const std::wstring& path){
    HANDLE f=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(f!=INVALID_HANDLE_VALUE)CloseHandle(f);
}
struct Child {PROCESS_INFORMATION process={};std::wstring camera,folder;ULONGLONG nextTry=0;};
static void RecordExit(Child& c,DWORD code){
    HANDLE f=CreateFileW((c.folder+L"\\exit.log").c_str(),FILE_APPEND_DATA,FILE_SHARE_READ,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(f!=INVALID_HANDLE_VALUE){SYSTEMTIME t;GetLocalTime(&t);char line[128];int n=sprintf_s(line,"%04u-%02u-%02u %02u:%02u:%02u pid=%lu exit=%lu\r\n",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,c.process.dwProcessId,code);DWORD written;WriteFile(f,line,n,&written,nullptr);CloseHandle(f);}
    // Keep only one previous run; do not discard the evidence on automatic restart.
    MoveFileExW((c.folder+L"\\gpu.log").c_str(),(c.folder+L"\\gpu.previous.log").c_str(),MOVEFILE_REPLACE_EXISTING);
    MoveFileExW((c.folder+L"\\gpu.err").c_str(),(c.folder+L"\\gpu.previous.err").c_str(),MOVEFILE_REPLACE_EXISTING);
}
static bool Launch(Child& c,const std::wstring& base){
    DeleteFileW((c.folder+L"\\STOP").c_str());DeleteFileW((c.folder+L"\\READY.txt").c_str());
    SECURITY_ATTRIBUTES sa={sizeof(sa),nullptr,TRUE};
    HANDLE out=CreateFileW((c.folder+L"\\gpu.log").c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    HANDLE err=CreateFileW((c.folder+L"\\gpu.err").c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    HANDLE in=CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(out==INVALID_HANDLE_VALUE||err==INVALID_HANDLE_VALUE||in==INVALID_HANDLE_VALUE){if(out!=INVALID_HANDLE_VALUE)CloseHandle(out);if(err!=INVALID_HANDLE_VALUE)CloseHandle(err);if(in!=INVALID_HANDLE_VALUE)CloseHandle(in);return false;}
    STARTUPINFOW si={sizeof(si)};si.dwFlags=STARTF_USESTDHANDLES;si.hStdOutput=out;si.hStdError=err;si.hStdInput=in;
    std::wstring exe=base+L"\\GpuCameraPump.exe";
    std::wstring cmd=L"\""+exe+L"\" "+c.camera+L" \""+base+L"\\"+c.camera+L".params.txt\" \""+c.folder+L"\" serve";
    BOOL ok=CreateProcessW(exe.c_str(),&cmd[0],nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,base.c_str(),&si,&c.process);
    CloseHandle(in);CloseHandle(out);CloseHandle(err);
    if(ok){CloseHandle(c.process.hThread);c.process.hThread=nullptr;}return ok!=FALSE;
}
static void WINAPI ServiceMain(DWORD,wchar_t**){
    statusHandle=RegisterServiceCtrlHandlerExW(serviceName,Handler,nullptr);if(!statusHandle)return;
    stopEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!stopEvent){Status(SERVICE_STOPPED);return;}
    Status(SERVICE_START_PENDING,10000);
    wchar_t module[MAX_PATH];GetModuleFileNameW(nullptr,module,MAX_PATH);
    std::wstring base(module);base.resize(base.find_last_of(L"\\"));
    Child children[2];children[0].camera=L"front";children[1].camera=L"rear";
    for(auto& c:children){c.folder=base+L"\\runtime\\"+c.camera;CreateDirectoryW((base+L"\\runtime").c_str(),nullptr);CreateDirectoryW(c.folder.c_str(),nullptr);}
    Status(SERVICE_RUNNING);
    while(WaitForSingleObject(stopEvent,500)==WAIT_TIMEOUT){
        for(auto& c:children){
            if(c.process.hProcess && WaitForSingleObject(c.process.hProcess,0)==WAIT_OBJECT_0){DWORD code=0;GetExitCodeProcess(c.process.hProcess,&code);RecordExit(c,code);CloseHandle(c.process.hProcess);c.process.hProcess=nullptr;c.nextTry=GetTickCount64()+5000;}
            if(!c.process.hProcess && GetTickCount64()>=c.nextTry){if(!Launch(c,base))c.nextTry=GetTickCount64()+5000;}
        }
    }
    for(auto& c:children)Marker(c.folder+L"\\STOP");
    // Never TerminateProcess a client that may have a pending KS IOCTL or GPU fence.
    for(auto& c:children)if(c.process.hProcess){
        while(WaitForSingleObject(c.process.hProcess,1000)==WAIT_TIMEOUT){status.dwCheckPoint++;Status(SERVICE_STOP_PENDING,30000);}
        CloseHandle(c.process.hProcess);
    }
    CloseHandle(stopEvent);Status(SERVICE_STOPPED);
}
int wmain(){SERVICE_TABLE_ENTRYW table[]={{const_cast<wchar_t*>(serviceName),ServiceMain},{nullptr,nullptr}};return StartServiceCtrlDispatcherW(table)?0:(int)GetLastError();}
