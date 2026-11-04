#define main ArchivedBenchMain
#include "bench/src/GpuIspBench.cpp"
#undef main
#include "FastRgb.h"
int wmain(){
    const UINT32 w=1280;
    for(int test=0;test<3;test++){
        UINT32 h=test==2?948:960;bool rotate=test==1;
        std::vector<BYTE> nv12((size_t)w*h*3/2);
        for(UINT32 y=0;y<h;y++)for(UINT32 x=0;x<w;x++)nv12[(size_t)y*w+x]=(BYTE)(16+(x*7+y*11)%220);
        for(UINT32 y=0;y<h/2;y++)for(UINT32 x=0;x<w;x++)nv12[(size_t)w*h+(size_t)y*w+x]=(BYTE)(16+(x*19+y*23)%224);
        std::vector<BYTE> rgb,fast((size_t)w*960*4);
        std::vector<UINT32> ref((size_t)w*960,0xff000000u);
        Nv12ToRgb24(0,nv12.data(),w,h,w,w,rgb,rotate);
        UINT32 pad=(960-h)/2;
        for(UINT32 y=0;y<h;y++)for(UINT32 x=0;x<w;x++){
            auto c=rgb.data()+((size_t)y*w+x)*3;
            ref[(size_t)(959-y-pad)*w+x]=0xff000000u|((UINT32)c[0]<<16)|((UINT32)c[1]<<8)|c[2];
        }
        FastRgb32(nv12.data(),w,h,w,rotate,fast.data(),960);
        size_t different=0;int max=0;auto slow=(const BYTE*)ref.data();
        for(size_t i=0;i<fast.size();i++){int d=abs((int)fast[i]-slow[i]);if(d)different++;max=std::max(max,d);}
        printf("case=%d height=%u rotation=%d differing_bytes=%zu max_diff=%d\n",test,h,rotate?180:0,different,max);
        if(max>1)return 1;
    }
    return 0;
}
