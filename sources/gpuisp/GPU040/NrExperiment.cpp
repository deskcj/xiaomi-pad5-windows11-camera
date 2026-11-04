#define SNAPSHOT_REPLAY_NO_MAIN
#include "SnapshotReplay.cpp"

int main(int argc,char** argv) {
    if(argc!=5 || (strcmp(argv[1],"front") && strcmp(argv[1],"rear")) ||
       (strcmp(argv[3],"adaptive") && strcmp(argv[3],"legacy"))) return 2;
    std::string stem=argv[2],out=argv[4];
    CODEX_GPU_FRAME h={};GpuIspParams p={};GpuIspTone tone;
    std::vector<UINT8> raw,lscBytes,gpu,cpu;
    if(!ObjectFile(stem+".header",h) || !ObjectFile(stem+".params",p) ||
       !ObjectFile(stem+".tone",tone) || !ReadFile(stem+".raw16",raw) ||
       !ReadFile(stem+".lsc",lscBytes)) return 3;
    if(raw.size()!=(size_t)p.rawW*p.rawH*2 || h.Bytes!=raw.size() ||
       lscBytes.size()!=(size_t)p.lscCols*p.lscRows*16) return 4;
    if(!strcmp(argv[1],"front") && !strcmp(argv[3],"adaptive"))
        GpuIspSetNrRange(p,8.0f,4.0f);
    std::vector<float> lsc(lscBytes.size()/sizeof(float));
    memcpy(lsc.data(),lscBytes.data(),lscBytes.size());
    size_t size=(size_t)GpuIspNv12SizeBytes(p.outW,p.outH);
    gpu.resize(size);cpu.resize(size);
    GpuIspCore* core=GpuIspCreate(0,false);if(!core)return 5;
    double ms=0,sum=0;std::vector<UINT8> staged(size);
    for(int i=0;i<12;++i) {
        if(!GpuIspRunFused(core,p,tone,lsc.data(),raw.data(),raw.size(),gpu.data(),&ms)) {
            printf("ERROR: %s\n",GpuIspLastError(core));GpuIspDestroy(core);return 6;
        }
        if(i>=4)sum+=ms;
    }
    bool ok=GpuIspRun(core,p,tone,lsc.data(),raw.data(),raw.size(),staged.data());
    GpuIspDestroy(core);if(!ok || staged!=gpu)return 7;
    GpuIspRefOutput ro;
    if(!GpuIspRefRun(p,lsc.data(),tone,(const UINT16*)raw.data(),ro,cpu.data()))return 8;
    UINT32 pitch=GpuIspUVRowPitch(p.outW);
    Diff dy=PlaneDiff(gpu,cpu,p.outW,p.outH,pitch,0);
    Diff du=PlaneDiff(gpu,cpu,p.outW,p.outH/2,pitch,(size_t)pitch*p.outH);
    printf("NR mode=%s generation=%lu gpu_ms=%.3f cpu_y_max=%d cpu_uv_max=%d staged_exact=1\n",
           argv[3],h.Generation,sum/8,dy.max,du.max);
    if(dy.max>2 || du.max>1)return 9;
    if(!WriteFile(out+".nv12",gpu.data(),gpu.size()) ||
       !WritePngFromNv12(out+".png",0,gpu.data(),p.outW,p.outH,pitch,pitch,!strcmp(argv[1],"front")))return 10;
    return 0;
}

