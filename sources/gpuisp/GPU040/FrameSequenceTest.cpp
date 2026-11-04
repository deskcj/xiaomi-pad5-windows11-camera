#define SNAPSHOT_REPLAY_NO_MAIN
#include "SnapshotReplay.cpp"

struct InputFrame {
    CODEX_GPU_FRAME header={};GpuIspParams params={};GpuIspTone tone;
    std::vector<UINT8> raw,live,expected;
    std::vector<float> lsc;
};
static bool LoadFrame(const std::string& stem,InputFrame& f) {
    std::vector<UINT8> cells;
    if(!ObjectFile(stem+".header",f.header) || !ObjectFile(stem+".params",f.params) ||
       !ObjectFile(stem+".tone",f.tone) || !ReadFile(stem+".raw16",f.raw) ||
       !ReadFile(stem+".lsc",cells) || !ReadFile(stem+".nv12",f.live))return false;
    if(f.raw.size()!=(size_t)f.params.rawW*f.params.rawH*2 ||
       cells.size()!=(size_t)f.params.lscCols*f.params.lscRows*16)return false;
    f.lsc.resize(cells.size()/sizeof(float));memcpy(f.lsc.data(),cells.data(),cells.size());
    f.expected.resize((size_t)GpuIspNv12SizeBytes(f.params.outW,f.params.outH));return true;
}
static bool RunFrame(GpuIspCore* core,const InputFrame& f,std::vector<UINT8>& output,bool fused) {
    output.resize(f.expected.size());double ms=0;
    return fused ? GpuIspRunFused(core,f.params,f.tone,f.lsc.data(),f.raw.data(),f.raw.size(),output.data(),&ms)
                 : GpuIspRun(core,f.params,f.tone,f.lsc.data(),f.raw.data(),f.raw.size(),output.data());
}
static bool SameActive(const InputFrame& f,const std::vector<UINT8>& output,const char* name,int index) {
    UINT32 pitch=GpuIspUVRowPitch(f.params.outW);
    Diff y=PlaneDiff(f.expected,output,f.params.outW,f.params.outH,pitch,0);
    Diff uv=PlaneDiff(f.expected,output,f.params.outW,f.params.outH/2,pitch,(size_t)pitch*f.params.outH);
    printf("SEQUENCE mode=%s step=%d generation=%lu y_diff=%llu y_max=%d uv_diff=%llu uv_max=%d\n",
           name,index,f.header.Generation,y.count,y.max,uv.count,uv.max);fflush(stdout);
    return y.count==0 && uv.count==0;
}
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    bool pass=true;
    for(const char* camera : {"front","rear"}) {
        printf("CAMERA %s\n",camera);fflush(stdout);
        InputFrame input[3];
        for(int i=0;i<3;++i) {
            std::string stem=std::string(argv[1])+"\\matched-"+camera+"\\"+camera+"-snapshots\\snapshot-"+std::to_string(i);
            if(!LoadFrame(stem,input[i]))return 3;
            if(!strcmp(camera,"front"))GpuIspSetNrRange(input[i].params,8.0f,4.0f);
            GpuIspCore* fresh=GpuIspCreate(0,false);if(!fresh)return 4;
            bool ok=RunFrame(fresh,input[i],input[i].expected,true);
            GpuIspDestroy(fresh);if(!ok)return 5;
            SameActive(input[i],input[i].live,"recorded033_baseline",i);
        }
        const int order[]={0,1,2,0,2,1};
        for(bool fused : {true,false}) {
            GpuIspCore* reused=GpuIspCreate(0,false);if(!reused)return 4;
            for(int step=0;step<6;++step) {
                const InputFrame& f=input[order[step]];std::vector<UINT8> output;
                if(!RunFrame(reused,f,output,fused)){GpuIspDestroy(reused);return 5;}
                pass=SameActive(f,output,fused?"fused":"staged",step)&&pass;
            }
            GpuIspDestroy(reused);
        }
    }
    printf("FRAME_SEQUENCE %s: changing RAW and params, reused core must equal fresh core\n",pass?"PASS":"FAIL");
    return pass?0:1;
}
