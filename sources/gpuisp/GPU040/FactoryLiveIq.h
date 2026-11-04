#pragma once
namespace FrontFactory {
#include "src/NabuCamAvStreamFrontVirtual/AndroidFactoryTuningFront.h"
}
namespace RearFactory {
#include "src/NabuCamAvStreamRearVirtual/AndroidFactoryTuningRear.h"
}
struct LiveIqState {bool initialized=false;float gain=2.0f,r=2.0f,b=1.4f,outputScene=0.0f;float ccm[9]={};};
// Small, decimated control statistics on CPU; all pixel ISP stages remain GPU compute.
// Reject chromatic/saturated cells using distance to the sensor's calibrated illuminant locus.
static void UpdateFactoryIq(bool front,const CODEX_GPU_INFO& info,const UINT16* raw,
                            const AndroidParams& ap,GpuIspParams& p,LiveIqState& state,bool quiet=false){
    const auto refs=front?FrontFactory::AvFactoryFrontAwbRefQ14:RearFactory::AvFactoryRearAwbRefQ14;
    const auto manualGains=front?FrontFactory::AvFactoryFrontMwbGainQ10:RearFactory::AvFactoryRearMwbGainQ10;
    const auto matrices=front?FrontFactory::AvFactoryFrontCcmQ12:RearFactory::AvFactoryRearCcmQ12;
    static const int matrixIndex[10]={4,4,3,2,1,1,1,0,0,0};
    double sr=0,sg=0,sb=0,srL=0,sgL=0,sbL=0,scene=0,sceneW=0;unsigned neutral=0;
    const UINT32 width=p.rawW,height=p.rawH;
    for(UINT32 y=16;y+16<height;y+=16)for(UINT32 x=16;x+16<width;x+=16){
        const UINT16* q=raw+(size_t)y*width+x;
        float r,g,b;
        if(info.Pattern==0){b=q[0];g=(q[1]+q[width])*0.5f;r=q[width+1];}
        else if(info.Pattern==1){g=(q[0]+q[width+1])*0.5f;r=q[1];b=q[width];}
        else if(info.Pattern==2){g=(q[0]+q[width+1])*0.5f;b=q[1];r=q[width];}
        else {r=q[0];g=(q[1]+q[width])*0.5f;b=q[width+1];}
        r=std::max(r-64,0.0f);g=std::max(g-64,0.0f);b=std::max(b-64,0.0f);
        UINT32 cx=std::min((UINT32)((x+0.5f)/width*ap.lscCols),ap.lscCols-1);
        UINT32 cy=std::min((UINT32)((y+0.5f)/height*ap.lscRows),ap.lscRows-1);
        const float* cell=&ap.lsc[((size_t)cy*ap.lscCols+cx)*4];
        // Reject black cells from metering so a dark surround cannot make a
        // bright screen overexposed. Unlike AWB, keep bright/saturated cells.
        if(g<8)continue;
        scene+=g*cell[1]/959.0;sceneW+=1;
        if(g>900 || r>940 || b>940)continue;
        float rg=r/g,bg=b/g,distance=100;
        for(int i=0;i<10;i++){float dr=rg-refs[i][0]/16384.0f,db=bg-refs[i][1]/16384.0f;distance=std::min(distance,dr*dr+db*db);}
        if(distance>0.025f)continue;
        double w=1.0/(1.0+50*distance);sr+=r*w;sg+=g*w;sb+=b*w;
        // Colour stats must describe the signal the GPU CCM actually receives:
        // each Bayer channel has a different lens-shading gain. These sparse
        // control stats run on CPU; per-pixel ISP remains on Adreno.
        srL+=r*cell[0]*w;
        sgL+=g*(cell[1]+cell[2])*0.5f*w;
        sbL+=b*cell[3]*w;
        ++neutral;
    }
    const bool reliableWb=neutral>=64 && sg>0;
    int ref=2;float distance=100;
    if(reliableWb)for(int i=0;i<10;i++){
        float dr=(float)(sr/sg)-refs[i][0]/16384.0f,db=(float)(sb/sg)-refs[i][1]/16384.0f;
        if(dr*dr+db*db<distance){distance=dr*dr+db*db;ref=i;}
    }
    const float sceneLinear=sceneW>0?(float)(scene/sceneW):0.0f;
    // No metering signal cannot justify an exposure or illuminant change.
    float targetGain=sceneLinear>0.0001f?std::max(0.25f,std::min(4.0f,0.18f/sceneLinear)):
        (state.initialized?state.gain:ap.exGain);
    float targetR=manualGains[ref][0]/1024.0f,targetB=manualGains[ref][1]/1024.0f;
    if(neutral>=256 && srL>0 && sgL>0 && sbL>0){
        // The manual gains alone leave a cast on our direct RAW->CCM path.
        // Blend toward the neutral estimate after LSC; a bounded partial step
        // won all four held-out neutral-region checks (two lighting states per
        // camera), while using the estimate alone overcorrected the rear.
        float rStat=std::max(0.75f*targetR,std::min(1.25f*targetR,(float)(sgL/srL)));
        float bStat=std::max(0.75f*targetB,std::min(1.25f*targetB,(float)(sgL/sbL)));
        float blend=front?0.75f:0.50f;
        targetR+=blend*(rStat-targetR);targetB+=blend*(bStat-targetB);
    }
    float a=state.initialized?0.15f:1.0f;
    if(state.initialized && sceneLinear>0.0001f && state.outputScene>0.0001f){
        // The sensor runs its own delayed AE. Avoid a second, lagging loop
        // that alternately flashes and darkens the GPU output after a step.
        // Let displayed mean approach target by at most 12% per frame.
        float wanted=state.outputScene+0.12f*(0.18f-state.outputScene);
        wanted=std::max(state.outputScene*0.88f,std::min(state.outputScene*1.12f,wanted));
        state.gain=std::max(0.25f,std::min(4.0f,wanted/sceneLinear));
    }else state.gain=targetGain;
    state.outputScene=state.gain*sceneLinear;
    if(reliableWb){
        state.r+=a*(targetR-state.r);state.b+=a*(targetB-state.b);
        for(int i=0;i<9;i++){float v=matrices[matrixIndex[ref]][i]/4096.0f;state.ccm[i]+=a*(v-state.ccm[i]);}
    }else if(!state.initialized){
        // Cold start uses the selected preset; subsequent low-signal frames
        // retain the last valid gains AND CCM rather than drifting to ref2.
        state.r=ap.awb[0];state.b=ap.awb[3];
        for(int i=0;i<9;i++)state.ccm[i]=ap.ccm[i];
    }
    for(int i=0;i<9;i++)GpuIspCcm(p,i)=state.ccm[i];
    state.initialized=true;p.exGain=state.gain;p.awb[0]=state.r;p.awb[1]=p.awb[2]=1;p.awb[3]=state.b;
    if(!quiet)printf("iq neutral=%u wb_ref=%d wb_hold=%u exgain=%.4f awb=%.4f/%.4f scene_linear=%.5f output_linear=%.5f\n",neutral,reliableWb?ref:-1,reliableWb?0u:1u,p.exGain,state.r,state.b,sceneLinear,state.outputScene);
}
