#define wmain ArchivedPumpWmain
#include "GpuCameraPump.cpp"
#undef wmain

int wmain(int argc,wchar_t** argv){
    if(argc!=6){fprintf(stderr,"AeGate front|rear RAW PARAMS CSV WIDTH\n");return 2;}
    bool front=wcscmp(argv[1],L"front")==0;
    if(!front && wcscmp(argv[1],L"rear"))return 2;
    int width=_wtoi(argv[5]),height=front?1224:960;
    if(width!=(front?1632:1280))return 2;
    HANDLE f=CreateFileW(argv[2],GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(f==INVALID_HANDLE_VALUE)return 3;
    std::vector<UINT16> source((size_t)width*height),work(source.size());
    DWORD got=0;BOOL ok=::ReadFile(f,source.data(),(DWORD)(source.size()*2),&got,nullptr);CloseHandle(f);
    if(!ok||got!=source.size()*2)return 4;
    AndroidParams ap;if(!LoadParams(Utf8(argv[3]),ap))return 5;
    FILE* csv=nullptr;if(_wfopen_s(&csv,argv[4],L"w")||!csv)return 6;
    fprintf(csv,"frame,scale,scene_linear,digital_gain,output_linear,step_ratio\n");
    CODEX_GPU_INFO info={};info.Pattern=front?0:1;
    GpuIspParams p={};p.rawW=width;p.rawH=height;
    LiveIqState state;float prev=0,maxStep=1;
    for(int frame=0;frame<30;frame++){
        float scale=frame<5?1.0f:frame<11?1.8f:frame<17?0.6f:frame<23?1.0f:1.35f;
        for(size_t i=0;i<source.size();i++){
            float v=64.0f+((float)source[i]-64.0f)*scale;
            work[i]=(UINT16)std::max(0.0f,std::min(1023.0f,v));
        }
        UpdateFactoryIq(front,info,work.data(),ap,p,state,true);
        float step=prev>0?std::max(state.outputScene/prev,prev/state.outputScene):1;
        maxStep=std::max(maxStep,step);
        fprintf(csv,"%d,%.3f,%.6f,%.6f,%.6f,%.6f\n",frame,scale,
                state.outputScene/p.exGain,p.exGain,state.outputScene,step);
        prev=state.outputScene;
    }
    fclose(csv);printf("AE_GATE camera=%s max_step=%.6f pass=%s\n",front?"front":"rear",maxStep,maxStep<=1.14f?"true":"false");
    return maxStep<=1.14f?0:7;
}
