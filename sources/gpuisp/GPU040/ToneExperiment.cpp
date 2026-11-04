#define SNAPSHOT_REPLAY_NO_MAIN
#include "SnapshotReplay.cpp"
int main(int argc,char** argv) {
    if(argc!=5 || (strcmp(argv[1],"front") && strcmp(argv[1],"rear")))return 2;
    std::string stem=argv[2],out=argv[4];CODEX_GPU_FRAME h={};GpuIspParams p={};GpuIspTone tone;
    std::vector<UINT8> raw,lscBytes,nv;AndroidParams ap;
    if(!ObjectFile(stem+".header",h)||!ObjectFile(stem+".params",p)||!ReadFile(stem+".raw16",raw)||
       !ReadFile(stem+".lsc",lscBytes)||!LoadParams(argv[3],ap))return 3;
    if(raw.size()!=(size_t)p.rawW*p.rawH*2 || lscBytes.size()!=(size_t)p.lscCols*p.lscRows*16)return 4;
    std::vector<float> lsc(lscBytes.size()/sizeof(float));memcpy(lsc.data(),lscBytes.data(),lscBytes.size());
    BuildToneLut(ap,tone);nv.resize((size_t)GpuIspNv12SizeBytes(p.outW,p.outH));
    GpuIspCore* core=GpuIspCreate(0,false);if(!core)return 5;double ms=0;
    bool ok=GpuIspRunFused(core,p,tone,lsc.data(),raw.data(),raw.size(),nv.data(),&ms);
    GpuIspDestroy(core);if(!ok)return 6;
    if(!WriteFile(out+".nv12",nv.data(),nv.size()) ||
       !WritePngFromNv12(out+".png",0,nv.data(),p.outW,p.outH,GpuIspUVRowPitch(p.outW),GpuIspUVRowPitch(p.outW),!strcmp(argv[1],"front")))return 7;
    printf("TONE_EXPERIMENT only_tone_changed=true raw_generation=%lu recorded_exgain=%.6f recorded_awb=%.6f/%.6f output=%s\n",h.Generation,p.exGain,p.awb[0],p.awb[3],out.c_str());
    return 0;
}
