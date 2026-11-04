// Build003 user-mode GPU ISP client for the private AVStream KS bridge.
// Reuse exactly the offline-tested params/LUT/parser/exporter, never the CPU ISP path.
#define main ArchivedOfflineBenchMain
#include "bench/src/GpuIspBench.cpp"
#undef main
#include <setupapi.h>
#include <mmreg.h>
#include <unknwn.h>
#include <ks.h>
#include <ksmedia.h>
#define CODEX_GPU_CLIENT 1
#include "src/GpuBridgeProtocol.h"
#include "FactoryLiveIq.h"
#include "FastRgb.h"
#include "GpuPublication.h"
#include "OrderedPublisher.h"
#include <memory>

static bool Control(HANDLE h,CODEX_GPU_REQUEST& req,void* data,DWORD size,DWORD& got) {
    OVERLAPPED ov={};ov.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    if(!ov.hEvent)return false;
    BOOL ok=DeviceIoControl(h,IOCTL_KS_PROPERTY,&req,sizeof(req),data,size,&got,&ov);
    DWORD err=ok?0:GetLastError();
    if(!ok && err==ERROR_IO_PENDING){
        if(WaitForSingleObject(ov.hEvent,3000)!=WAIT_OBJECT_0){
            CancelIoEx(h,&ov);fprintf(stderr,"KS property timeout; await cancellation, do not kill\n");fflush(stderr);
            WaitForSingleObject(ov.hEvent,INFINITE);GetOverlappedResult(h,&ov,&got,FALSE);err=ERROR_TIMEOUT;
        }else {ok=GetOverlappedResult(h,&ov,&got,FALSE);err=ok?0:GetLastError();}
    }
    CloseHandle(ov.hEvent);SetLastError(err);return err==0;
}
static bool Property(HANDLE h,ULONG id,bool set,void* data,DWORD size,DWORD& got,ULONG epoch=0,ULONG gen=0){
    CODEX_GPU_REQUEST req={};req.Property.Set=CodexGpuPropertySet;req.Property.Id=id;
    req.Property.Flags=set?KSPROPERTY_TYPE_SET:KSPROPERTY_TYPE_GET;req.Epoch=epoch;req.AfterGeneration=gen;
    return Control(h,req,data,size,got);
}
static HANDLE OpenCamera(bool front){
    static const GUID category={STATIC_KSCATEGORY_VIDEO_CAMERA};
    HDEVINFO devices=SetupDiGetClassDevsW(&category,nullptr,nullptr,DIGCF_PRESENT|DIGCF_DEVICEINTERFACE);
    if(devices==INVALID_HANDLE_VALUE)return INVALID_HANDLE_VALUE;
    HANDLE result=INVALID_HANDLE_VALUE;
    for(DWORD i=0;;++i){
        SP_DEVICE_INTERFACE_DATA it={sizeof(it)};
        if(!SetupDiEnumDeviceInterfaces(devices,nullptr,&category,i,&it))break;
        DWORD needed=0;SetupDiGetDeviceInterfaceDetailW(devices,&it,nullptr,0,&needed,nullptr);
        if(!needed)continue;
        std::vector<BYTE> bytes(needed);auto detail=(SP_DEVICE_INTERFACE_DETAIL_DATA_W*)bytes.data();detail->cbSize=sizeof(*detail);
        SP_DEVINFO_DATA node={sizeof(node)};
        if(!SetupDiGetDeviceInterfaceDetailW(devices,&it,detail,needed,nullptr,&node))continue;
        wchar_t svc[256]={};
        if(!SetupDiGetDeviceRegistryPropertyW(devices,&node,SPDRP_SERVICE,nullptr,(BYTE*)svc,sizeof(svc),nullptr))continue;
        if(_wcsicmp(svc,front?L"NabuCamAvStreamFrontVirtual":L"NabuCamAvStreamRearVirtual"))continue;
        result=CreateFileW(detail->DevicePath,GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr);
        if(result!=INVALID_HANDLE_VALUE)break;
    }
    SetupDiDestroyDeviceInfoList(devices);return result;
}
struct BridgeHandle {
    HANDLE h=INVALID_HANDLE_VALUE;bool attached=false;
    ~BridgeHandle(){if(h!=INVALID_HANDLE_VALUE){if(attached){ULONG mode=0;DWORD got=0;bool ok=Property(h,CodexGpuMode,true,&mode,sizeof(mode),got);printf("gpu_detach=%s\n",ok?"OK":"FAIL");}CloseHandle(h);}}
};
struct CoreHandle {GpuIspCore* p=nullptr;~CoreHandle(){if(p)GpuIspDestroy(p);}};
// The pump's own working set is ~44 MB (measured in Build018/research/rear_fullraw/HANDOFF.md:13), so
// the 900 MB floor it used to enforce is not about what it needs - and on a 5.5 GB pad that floor fails
// on almost every activation. Exiting over that floor is what kept the GPU out of the live picture: the
// host relaunches the pump 5 s later, by which time the camera pin is open again and the arm loses the
// race. So the floor is what the allocation actually needs, and a low reading waits rather than exits.
// User-mode allocation floor. Kernel installation retains its separate 900 MiB guard.
static const unsigned long GPU_RAM_FLOOR_MB = 512;
static std::wstring stopPath;
static bool StopRequested(){
    return !stopPath.empty() && GetFileAttributesW(stopPath.c_str())!=INVALID_FILE_ATTRIBUTES;
}
static bool WaitForRam(unsigned long floorMB, unsigned long waitMs){
    MEMORYSTATUSEX m={sizeof(m)};const ULONGLONG until=GetTickCount64()+waitMs;
    for(;;){
        if(StopRequested())return false;
        GlobalMemoryStatusEx(&m);
        if(m.ullAvailPhys>=(unsigned long long)floorMB*1048576ull)return true;
        if(GetTickCount64()>=until){printf("RAM_WAIT timeout free_mb=%llu floor_mb=%lu\n",m.ullAvailPhys/1048576,floorMB);fflush(stdout);return false;}
        printf("RAM_WAIT free_mb=%llu floor_mb=%lu\n",m.ullAvailPhys/1048576,floorMB);fflush(stdout);
        Sleep(250);
    }
}
static double QpcMs(){
    static LARGE_INTEGER frequency=[](){LARGE_INTEGER f;QueryPerformanceFrequency(&f);return f;}();
    LARGE_INTEGER now;QueryPerformanceCounter(&now);
    return 1000.0*double(now.QuadPart)/double(frequency.QuadPart);
}
static std::string Utf8(const wchar_t* p){int n=WideCharToMultiByte(CP_UTF8,0,p,-1,nullptr,0,nullptr,nullptr);std::string s(n,'\0');WideCharToMultiByte(CP_UTF8,0,p,-1,&s[0],n,nullptr,nullptr);s.pop_back();return s;}
#include "GpuSnapshot.h"
int wmain(int argc,wchar_t** argv){
    if(argc<5){fprintf(stderr,"GpuCameraPump front|rear PARAMS OUTDIR FRAMES (0=inspect only)\n");return 2;}
    stopPath=std::wstring(argv[3])+L"\\STOP";
    bool front=wcscmp(argv[1],L"front")==0;if(!front && wcscmp(argv[1],L"rear"))return 2;
    bool dump=_wcsicmp(argv[4],L"dump")==0;
    bool serve=_wcsicmp(argv[4],L"serve")==0;
    int frames=serve?2147483647:_wtoi(argv[4]);if(!serve && (frames<0||frames>300))return 2;
    BridgeHandle bridge;bridge.h=OpenCamera(front);if(bridge.h==INVALID_HANDLE_VALUE){fprintf(stderr,"Open KS camera failed %lu\n",GetLastError());return 3;}
    CODEX_GPU_INFO info={};DWORD got=0;
    if(!Property(bridge.h,CodexGpuInfo,false,&info,sizeof(info),got)){fprintf(stderr,"GPU bridge unavailable %lu\n",GetLastError());return 4;}
    if((info.Version==1 && got!=40)||(info.Version==2 && got!=48)||
       (info.Version!=1 && info.Version!=2)||info.Width!=1280||info.Height!=960)return 5;
    if(info.Version==1){info.RawWidth=1280;info.RawHeight=960;}
    if((info.RawWidth!=1280 || info.RawHeight!=960) &&
       (info.RawWidth!=1632 || info.RawHeight!=1224))return 5;
    const ULONG rawBytes=info.RawWidth*info.RawHeight*2;
    printf("bridge_version=%lu mode=%lu epoch=%lu pattern=%lu rotation=%lu raw=%lux%lu raw_generation=%lu submitted=%lu published=%lu\n",
        info.Version,info.Mode,info.Epoch,info.Pattern,info.Rotate180*180,info.RawWidth,info.RawHeight,info.RawGeneration,info.SubmittedGeneration,info.PublishedCount);
    if(dump){
        std::vector<BYTE> snapshot(sizeof(CODEX_GPU_FRAME)+rawBytes);
        if(!Property(bridge.h,CodexGpuRaw,false,snapshot.data(),(DWORD)snapshot.size(),got,info.Epoch,0)||got!=snapshot.size())return 18;
        WriteFile(Utf8(argv[3])+"\\latest.raw16",snapshot.data()+sizeof(CODEX_GPU_FRAME),rawBytes);
        WriteFile(Utf8(argv[3])+"\\latest.header",snapshot.data(),sizeof(CODEX_GPU_FRAME));
        AndroidParams dap;
        if(LoadParams(Utf8(argv[2]),dap)){
            GpuIspParams dp={};dp.rawW=info.RawWidth;dp.rawH=info.RawHeight;
            LiveIqState ds;
            UpdateFactoryIq(front,info,(const UINT16*)(snapshot.data()+sizeof(CODEX_GPU_FRAME)),dap,dp,ds);
        }
        printf("RAW_SNAPSHOT cached_only=true bytes=%lu; sensor_not_started=true\n",rawBytes);return 0;
    }
    if(!frames)return 0;
    MEMORYSTATUSEX mem={sizeof(mem)};GlobalMemoryStatusEx(&mem);
    printf("free_ram_mb=%llu\n",mem.ullAvailPhys/1048576);
    if(!serve && !WaitForRam(GPU_RAM_FLOOR_MB,20000))return 6;
    AndroidParams ap;if(!LoadParams(Utf8(argv[2]),ap))return 7;
    GpuIspParams p={};p.rawW=info.RawWidth;p.rawH=info.RawHeight;p.outW=1280;p.outH=front?960:948;
    p.cropW=p.rawW;p.cropH=p.rawH;p.lscCols=ap.lscCols;p.lscRows=ap.lscRows;
    p.blackLevel=ap.black;p.whiteLevel=ap.white;p.exGain=ap.exGain;p.gamma=2.2f;
    memcpy(p.awb,ap.awb,sizeof(p.awb));for(int i=0;i<9;i++)GpuIspCcm(p,i)=ap.ccm[i];
    p.flags=GPUISP_FLAG_AWB|GPUISP_FLAG_CCM|GPUISP_FLAG_TONE;
    if(p.rawW==p.outW && p.rawH==p.outH)p.flags|=GPUISP_FLAG_DIRECT;
    if(p.lscCols>1 && p.lscRows>1)p.flags|=GPUISP_FLAG_LSC;
    p.denoise=ap.denoise;if(p.denoise>0)p.flags|=GPUISP_FLAG_DENOISE;
    p.sharpen=ap.sharpen;p.sharpenRadius=ap.sharpenRadius>0?ap.sharpenRadius:1;
    p.sharpenLimit=ap.sharpenLimit>=0?ap.sharpenLimit:24;p.sharpenThreshold=ap.sharpenThr>=0?ap.sharpenThr:0;
    GpuIspSetEdgeProtect(p,ap.sharpenEdgeLo>=0?ap.sharpenEdgeLo:0,ap.sharpenEdgeHi>=0?ap.sharpenEdgeHi:0);
    if(ap.nrRangeA>=0.0f&&ap.nrRangeB>0.0f)GpuIspSetNrRange(p,ap.nrRangeA,ap.nrRangeB);
    if(p.sharpen>0)p.flags|=GPUISP_FLAG_SHARP;
    if(ap.chromaSmooth>0)p.flags|=GPUISP_FLAG_CHROMA_SMOOTH;
    if(ap.chromaRecentre>0)p.flags|=GPUISP_FLAG_CHROMA_RECENTRE;
    p.chromaGain=ap.chromaGain;p.chromaBiasU=ap.chromaBiasU;p.chromaBiasV=ap.chromaBiasV;
    GpuIspSetChromaSigma(p,ap.chromaSigma);
    p.cfaOff=((info.Pattern==2||info.Pattern==3)?1u:0u)|((info.Pattern==1||info.Pattern==3)?2u:0u);
    GpuIspSetYuvMatrix(p,0);GpuIspTone tone;BuildToneLut(ap,tone);
    CoreHandle core;
    if(!serve){core.p=GpuIspCreate(0,false);if(!core.p){fprintf(stderr,"GPU create failed\n");return 8;}}
    std::vector<BYTE> raw(sizeof(CODEX_GPU_FRAME)+rawBytes);
    std::vector<BYTE> submit(sizeof(CODEX_GPU_FRAME)+CODEX_GPU_RGB_BYTES);
    std::vector<BYTE> nv12((size_t)GpuIspNv12SizeBytes(p.outW,p.outH)),firstNv12,rgb;
    ULONG mode=1,attachTry=0;
    // The kernel refuses an arm while an application holds the camera pin (ERROR_BUSY 170), and the host
    // relaunches this process every 5 s, so a single failed attempt used to exit 9 and lose that race for
    // as long as the preview stayed open. Wait for the pin instead.
    for(;;){
        if(StopRequested())return 0;
        if(Property(bridge.h,CodexGpuMode,true,&mode,sizeof(mode),got))break;
        DWORD err=GetLastError();++attachTry;
        if(err!=170 || attachTry>120){fprintf(stderr,"Attach failed %lu after %lu tries\n",err,attachTry);return 9;}
        if(attachTry==1||attachTry%20==0){printf("ATTACH_BUSY pin_held try=%lu\n",attachTry);fflush(stdout);}
        Sleep(500);
    }
    bridge.attached=true;
    if(!Property(bridge.h,CodexGpuInfo,false,&info,sizeof(info),got))return 10;
    std::string out=Utf8(argv[3]);
    GpuSnapshot snapshot(argv[3]);
    // The worker uses this existing bridge handle and its own RGB buffer. The
    // kernel serializes properties with GpuIoLock; no second platform owner.
    std::vector<BYTE> publicationBuffer;
    std::unique_ptr<OrderedPublisher<GpuPublicationJob>> publisher;
    if(serve){
        const UINT32 width=p.outW,height=p.outH,pitch=GpuIspUVRowPitch(p.outW);
        const bool rotate=info.Rotate180!=0;
        const ULONG epoch=info.Epoch;
        publisher.reset(new OrderedPublisher<GpuPublicationJob>(
            [&,width,height,pitch,rotate,epoch](const GpuPublicationJob& job,double& convertMs,double& submitMs){
                const double start=QpcMs();
                PackGpuPublication(job,width,height,pitch,rotate,publicationBuffer);
                const double converted=QpcMs();DWORD publishedBytes=0;
                if(!Property(bridge.h,CodexGpuSubmit,true,publicationBuffer.data(),
                             (DWORD)publicationBuffer.size(),publishedBytes,epoch)){
                    fprintf(stderr,"Ordered submit failed %lu generation=%lu epoch=%lu\n",
                            GetLastError(),job.header.Generation,job.header.Epoch);return false;
                }
                convertMs=converted-start;submitMs=QpcMs()-converted;return true;
            }));
    }
    const char ready[]="GPU attached; open the regular camera pin now.\n";WriteFile(out+"\\READY.txt",ready,sizeof(ready)-1);
    printf("GPU_READY epoch=%lu\n",info.Epoch);fflush(stdout);
    LiveIqState iq;
    // Arm-then-open is the required order (the kernel refuses an arm while a pin is held), so between
    // attaching and the first streamed frame there is simply no RAW yet. The bridge answers that with
    // ERROR_BAD_COMMAND rather than ERROR_NOT_READY, which the old code treated as fatal: the pump died
    // a second after GPU_READY, the host relaunched it 5 s later, and the loop never reached a frame.
    const ULONGLONG armDeadline=GetTickCount64()+120000;
    ULONG last=0;int produced=0,synchronousPublished=0;ULONGLONG deadline=GetTickCount64()+120000,lastRawAt=GetTickCount64();double sumMs=0;
    double phaseRaw=0,phaseIq=0,phaseGpu=0,phaseRgb=0,phaseSubmit=0,lastPublishAt=0,phaseWall=0;
    unsigned phaseFrames=0,wallIntervals=0,synchronousPhaseFrames=0;
    while(produced<frames){
        if(GetFileAttributesW((std::wstring(argv[3])+L"\\STOP").c_str())!=INVALID_FILE_ATTRIBUTES)break;
        if(publisher && publisher->Failed()){fprintf(stderr,"Publication worker failed\n");return 14;}
        if(!serve && GetTickCount64()>deadline){fprintf(stderr,"No new RAW within the bounded capture window; produced=%d\n",produced);break;}
        double phaseStart=QpcMs();
        if(!Property(bridge.h,CodexGpuRaw,false,raw.data(),(DWORD)raw.size(),got,info.Epoch,last)){
            DWORD err=GetLastError();
            // 121 = nothing published since AfterGeneration; 22 = the bridge has no RAW at all yet.
            if(err==ERROR_NOT_READY || err==ERROR_BAD_COMMAND){
                if(publisher && GetTickCount64()-lastRawAt>=500 && !publisher->Drain())return 14;
                snapshot.FlushIdle(GetTickCount64()-lastRawAt);
                if(serve && core.p && GetTickCount64()-lastRawAt>2000){GpuIspDestroy(core.p);core.p=nullptr;iq.initialized=false;printf("IDLE: released GPU frame resources\n");fflush(stdout);}
                if(!serve && GetTickCount64()>armDeadline){fprintf(stderr,"No RAW within 120s of arming (last error %lu)\n",err);return 11;}
                // Polling at 50 ms imposed an avoidable 20 fps wait when RAW arrived just after a GET.
                Sleep(5);continue;
            }fprintf(stderr,"Get RAW failed %lu\n",err);return 11;
        }
        CODEX_GPU_FRAME rh;memcpy(&rh,raw.data(),sizeof(rh));
        double afterRaw=QpcMs();
        if(got!=raw.size()||rh.Version!=info.Version||rh.Epoch!=info.Epoch||rh.Bytes!=rawBytes||rh.Width!=p.rawW||rh.Height!=p.rawH||rh.Stride!=p.rawW*2||rh.Pattern!=info.Pattern){
            fprintf(stderr,"RAW contract mismatch got=%lu version=%lu epoch=%lu dims=%lux%lu stride=%lu pattern=%lu bytes=%lu\n",got,rh.Version,rh.Epoch,rh.Width,rh.Height,rh.Stride,rh.Pattern,rh.Bytes);return 12;
        }
        double ms=0;
        if(!core.p){
            if(!WaitForRam(GPU_RAM_FLOOR_MB,30000)){fprintf(stderr,"RAM floor %lu MB not met at camera activation; CPU fallback\n",GPU_RAM_FLOOR_MB);return 6;}
            core.p=GpuIspCreate(0,false);if(!core.p)return 8;
        }
        lastRawAt=GetTickCount64();
        UpdateFactoryIq(front,info,(const UINT16*)(raw.data()+sizeof(rh)),ap,p,iq,serve && produced%60!=0);
        double afterIq=QpcMs();
        if(!GpuIspRunFused(core.p,p,tone,ap.lsc.data(),raw.data()+sizeof(rh),rh.Bytes,nv12.data(),&ms)){
            fprintf(stderr,"GPU failure: %s\n",GpuIspLastError(core.p));return 13;
        }
        double afterGpu=QpcMs();
        if(!publisher || snapshot.Requested()){
            // A diagnostic frame goes through the original synchronous path,
            // after all previous generations have finished publication.
            if(publisher && !publisher->Drain())return 14;
            auto sh=(CODEX_GPU_FRAME*)submit.data();*sh=rh;sh->Width=1280;sh->Height=960;sh->Stride=1280*4;sh->Bytes=CODEX_GPU_RGB_BYTES;
            const double startRgb=QpcMs();
            FastRgb32(nv12.data(),p.outW,p.outH,GpuIspUVRowPitch(p.outW),info.Rotate180!=0,submit.data()+sizeof(*sh),960);
            const double afterRgb=QpcMs();
            if(!Property(bridge.h,CodexGpuSubmit,true,submit.data(),(DWORD)submit.size(),got,info.Epoch)){
                fprintf(stderr,"Submit failed %lu\n",GetLastError());return 14;
            }
            const double afterSubmit=QpcMs();
            ++synchronousPublished;++synchronousPhaseFrames;
            phaseRgb+=afterRgb-startRgb;phaseSubmit+=afterSubmit-afterRgb;
            snapshot.Capture(rh,p,tone,ap.lsc,raw.data()+sizeof(rh),nv12,submit.data()+sizeof(*sh));
        }else{
            GpuPublicationJob job;job.header=rh;job.nv12=nv12;
            if(!publisher->Enqueue(std::move(job))){fprintf(stderr,"Publication queue rejected generation=%lu\n",rh.Generation);return 14;}
        }
        const double producedAt=QpcMs();
        phaseRaw+=afterRaw-phaseStart;phaseIq+=afterIq-afterRaw;phaseGpu+=afterGpu-afterIq;
        if(lastPublishAt>0){phaseWall+=producedAt-lastPublishAt;++wallIntervals;}
        lastPublishAt=producedAt;++phaseFrames;
        last=rh.Generation;++produced;sumMs+=ms;deadline=GetTickCount64()+15000;
        if(!serve || produced%60==0){
            const auto stats=publisher?publisher->TakeStats():OrderedPublisher<GpuPublicationJob>::Stats{};
            const size_t publishedFrames=stats.count+synchronousPhaseFrames;
            const size_t published=publisher?publisher->Completed()+synchronousPublished:synchronousPublished;
            printf("produced=%d submitted=%zu raw_generation=%lu gpu_ms=%.3f pipeline=%s\n",produced,published,last,ms,publisher?"ordered":"synchronous");
            printf("phase_ms produced_n=%u submitted_n=%zu raw_get=%.3f iq=%.3f gpu_call=%.3f nv12_rgb=%.3f rgb_submit=%.3f produce_period=%.3f publish_period=%.3f\n",
                phaseFrames,publishedFrames,phaseRaw/phaseFrames,phaseIq/phaseFrames,phaseGpu/phaseFrames,
                (phaseRgb+stats.convertMs)/(publishedFrames?publishedFrames:1),
                (phaseSubmit+stats.submitMs)/(publishedFrames?publishedFrames:1),phaseWall/(wallIntervals?wallIntervals:1u),
                publisher?stats.periodMs/(stats.intervals?stats.intervals:1):phaseWall/(wallIntervals?wallIntervals:1u));
            phaseRaw=phaseIq=phaseGpu=phaseRgb=phaseSubmit=phaseWall=0;phaseFrames=wallIntervals=synchronousPhaseFrames=0;
            fflush(stdout);
        }
        if(produced==1 && !serve)firstNv12=nv12;
    }
    // Join before querying/detaching the bridge, including every early-return
    // path via publisher's destructor. Never abandon a pending KS IOCTL.
    if(publisher){publisher->Close();if(publisher->Failed())return 14;}
    const size_t completed=synchronousPublished+(publisher?publisher->Completed():0);
    if(completed!=(size_t)produced){fprintf(stderr,"Publication accounting mismatch produced=%d submitted=%zu\n",produced,completed);return 14;}
    if(!Property(bridge.h,CodexGpuInfo,false,&info,sizeof(info),got))return 15;
    printf("RESULT frames=%zu produced=%d gpu_published=%lu last_generation=%lu mean_gpu_ms=%.3f queue_high_water=%zu cpu_isp_used=false\n",completed,produced,info.PublishedCount,info.SubmittedGeneration,completed?sumMs/completed:0,publisher?publisher->HighWater():0);
    // Finish IPC before writing to the slow removable drive. Never save megabytes per frame.
    mode=0;
    if(!Property(bridge.h,CodexGpuMode,true,&mode,sizeof(mode),got))return 17;
    bridge.attached=false;printf("gpu_detach=OK\n");
    if(completed && !serve){
        WriteFile(out+"\\submitted-last.rgb32",submit.data()+sizeof(CODEX_GPU_FRAME),CODEX_GPU_RGB_BYTES);
        WritePngFromNv12(out+"\\gpu-first.png",0,firstNv12.data(),p.outW,p.outH,GpuIspUVRowPitch(p.outW),GpuIspUVRowPitch(p.outW),info.Rotate180!=0);
        WritePngFromNv12(out+"\\gpu-last.png",0,nv12.data(),p.outW,p.outH,GpuIspUVRowPitch(p.outW),GpuIspUVRowPitch(p.outW),info.Rotate180!=0);
    }
    return (serve || completed>0) && info.PublishedCount==(ULONG)completed?0:16;
}
