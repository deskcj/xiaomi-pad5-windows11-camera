#define main ArchivedSnapshotBenchMain
#include "bench/src/GpuIspBench.cpp"
#undef main
#include <mmreg.h>
#include <unknwn.h>
#include <ks.h>
#include <ksmedia.h>
#define CODEX_GPU_CLIENT 1
#include "src/GpuBridgeProtocol.h"

template<class T> static bool ObjectFile(const std::string& name,T& value) {
    std::vector<UINT8> bytes;
    if(!ReadFile(name,bytes) || bytes.size()!=sizeof(T)) return false;
    memcpy(&value,bytes.data(),sizeof(T));return true;
}
struct Diff { UINT64 count=0,sum=0; int max=0; };
static Diff PlaneDiff(const std::vector<UINT8>& a,const std::vector<UINT8>& b,
                      UINT32 width,UINT32 height,UINT32 pitch,size_t offset) {
    Diff d;
    for(UINT32 y=0;y<height;++y) for(UINT32 x=0;x<width;++x) {
        size_t i=offset+(size_t)y*pitch+x;
        int v=abs((int)a[i]-(int)b[i]);d.sum+=v;d.max=std::max(d.max,v);if(v)++d.count;
    }
    return d;
}
static bool RgbBmp(const std::string& name,const std::vector<UINT8>& rgb) {
    BITMAPFILEHEADER file={};BITMAPINFOHEADER info={};
    file.bfType=0x4d42;file.bfOffBits=sizeof(file)+sizeof(info);
    file.bfSize=file.bfOffBits+(DWORD)rgb.size();
    info.biSize=sizeof(info);info.biWidth=1280;info.biHeight=960;
    info.biPlanes=1;info.biBitCount=32;info.biSizeImage=(DWORD)rgb.size();
    std::vector<UINT8> bmp(file.bfSize);
    memcpy(bmp.data(),&file,sizeof(file));memcpy(bmp.data()+sizeof(file),&info,sizeof(info));
    memcpy(bmp.data()+file.bfOffBits,rgb.data(),rgb.size());
    return WriteFile(name,bmp.data(),bmp.size());
}
#ifndef SNAPSHOT_REPLAY_NO_MAIN
int main(int argc,char** argv) {
    if(argc!=3 || (strcmp(argv[1],"front") && strcmp(argv[1],"rear"))) return 2;
    bool rotate=strcmp(argv[1],"front")==0;
    std::string stem=argv[2];CODEX_GPU_FRAME header={};GpuIspParams params={};GpuIspTone tone;
    std::vector<UINT8> raw,live,lscBytes,rgb;
    if(!ObjectFile(stem+".header",header) || !ObjectFile(stem+".params",params) ||
       !ObjectFile(stem+".tone",tone) || !ReadFile(stem+".raw16",raw) ||
       !ReadFile(stem+".nv12",live) || !ReadFile(stem+".lsc",lscBytes) ||
       !ReadFile(stem+".rgb32",rgb)) return 3;
    size_t nvSize=(size_t)GpuIspNv12SizeBytes(params.outW,params.outH);
    if(raw.size()!=header.Bytes || raw.size()!=(size_t)params.rawW*params.rawH*2 ||
       live.size()!=nvSize || lscBytes.size()!=(size_t)params.lscCols*params.lscRows*4*sizeof(float) ||
       rgb.size()!=CODEX_GPU_RGB_BYTES || header.Width!=params.rawW || header.Height!=params.rawH ||
       header.Stride!=params.rawW*2) return 4;
    std::vector<float> lsc(lscBytes.size()/sizeof(float));memcpy(lsc.data(),lscBytes.data(),lscBytes.size());
    std::vector<UINT8> gpu(nvSize),cpu(nvSize);
    GpuIspCore* core=GpuIspCreate(0,false);if(!core)return 5;
    double ms=0;
    for(int repeat=0;repeat<4;++repeat) {
        if(!GpuIspRunFused(core,params,tone,lsc.data(),raw.data(),raw.size(),gpu.data(),&ms)) {
            printf("ERROR GPU: %s\n",GpuIspLastError(core));GpuIspDestroy(core);return 6;
        }
    }
    GpuIspDestroy(core);
    GpuIspRefOutput output;
    if(!GpuIspRefRun(params,lsc.data(),tone,(const UINT16*)raw.data(),output,cpu.data())) return 7;
    UINT32 pitch=GpuIspUVRowPitch(params.outW);size_t yBytes=(size_t)pitch*params.outH;
    Diff ly=PlaneDiff(live,gpu,params.outW,params.outH,pitch,0);
    Diff lu=PlaneDiff(live,gpu,params.outW,params.outH/2,pitch,yBytes);
    Diff cy=PlaneDiff(gpu,cpu,params.outW,params.outH,pitch,0);
    Diff cu=PlaneDiff(gpu,cpu,params.outW,params.outH/2,pitch,yBytes);
    char summary[1200];
    sprintf_s(summary,"{\n  \"Epoch\":%lu,\"Generation\":%lu,\"RawWidth\":%u,\"RawHeight\":%u,\n"
        "  \"ExGain\":%.6f,\"AwbR\":%.6f,\"AwbB\":%.6f,\"GpuMs\":%.3f,\n"
        "  \"LiveGpuYDiff\":%llu,\"LiveGpuYMax\":%d,\"LiveGpuUVDiff\":%llu,\"LiveGpuUVMax\":%d,\n"
        "  \"GpuCpuYDiff\":%llu,\"GpuCpuYMax\":%d,\"GpuCpuUVDiff\":%llu,\"GpuCpuUVMax\":%d\n}\n",
        header.Epoch,header.Generation,params.rawW,params.rawH,params.exGain,params.awb[0],params.awb[3],ms,
        ly.count,ly.max,lu.count,lu.max,cy.count,cy.max,cu.count,cu.max);
    printf("%s",summary);
    if(!WriteFile(stem+"-comparison.json",summary,strlen(summary)) ||
       !WritePngFromNv12(stem+"-live-nv12.png",0,live.data(),params.outW,params.outH,pitch,pitch,rotate) ||
       !WritePngFromNv12(stem+"-replay-gpu.png",0,gpu.data(),params.outW,params.outH,pitch,pitch,rotate) ||
       !WritePngFromNv12(stem+"-replay-cpu.png",0,cpu.data(),params.outW,params.outH,pitch,pitch,rotate) ||
       !RgbBmp(stem+"-submitted.bmp",rgb)) return 8;
    return 0;
}
#endif
