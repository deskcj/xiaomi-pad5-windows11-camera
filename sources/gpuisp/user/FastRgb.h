#pragma once
#include <arm_neon.h>
// User mode ONLY: final NV12 packing for the RGB32 AVStream contract.
// Demosaic, lens correction, WB, CCM, tone and NR have already run on Adreno.
// Q13 BT.601 limited-range coefficients. 32-bit products avoid overflow,
// saturating shifts reproduce the floating-point path within one code value.
static uint8x8_t Pack8(int32x4_t a,int32x4_t b){
    return vqmovn_u16(vcombine_u16(vqshrun_n_s32(a,13),vqshrun_n_s32(b,13)));
}
static void Rgb8(uint8x8_t y,uint8x8_t u,uint8x8_t v,BYTE* dst){
    int16x8_t yi=vsubq_s16(vreinterpretq_s16_u16(vmovl_u8(y)),vdupq_n_s16(16));
    int16x8_t ui=vsubq_s16(vreinterpretq_s16_u16(vmovl_u8(u)),vdupq_n_s16(128));
    int16x8_t vi=vsubq_s16(vreinterpretq_s16_u16(vmovl_u8(v)),vdupq_n_s16(128));
    int32x4_t r[2],g[2],b[2];
    for(int i=0;i<2;i++){
        int16x4_t y=vget_low_s16(yi),u=vget_low_s16(ui),v=vget_low_s16(vi);
        if(i){y=vget_high_s16(yi);u=vget_high_s16(ui);v=vget_high_s16(vi);}
        int32x4_t l=vmull_n_s16(y,9539);
        r[i]=vmlal_n_s16(l,v,13075);
        g[i]=vsubq_s32(vsubq_s32(l,vmull_n_s16(u,3209)),vmull_n_s16(v,6646));
        b[i]=vmlal_n_s16(l,u,16525);
    }
    uint8x8x4_t pixels;pixels.val[0]=Pack8(b[0],b[1]);pixels.val[1]=Pack8(g[0],g[1]);
    pixels.val[2]=Pack8(r[0],r[1]);pixels.val[3]=vdup_n_u8(255);vst4_u8(dst,pixels);
}
static void FastRgb32(const BYTE* nv12,UINT32 w,UINT32 h,UINT32 pitch,bool rotate,BYTE* dst,UINT32 targetH){
    const BYTE* uv=nv12+(size_t)pitch*h;UINT32 pad=(targetH-h)/2;
    for(UINT32 y=0;y<pad;y++)for(UINT32 x=0;x<w;x++){
        ((UINT32*)dst)[(size_t)y*w+x]=0xff000000;((UINT32*)dst)[(size_t)(targetH-1-y)*w+x]=0xff000000;
    }
    for(UINT32 y=0;y<h;y++){
        UINT32 sy=rotate?h-1-y:y;const BYTE* yr=nv12+(size_t)sy*pitch;const BYTE* ur=uv+(size_t)(sy/2)*pitch;
        BYTE* row=dst+(size_t)(targetH-1-y-pad)*w*4;
        for(UINT32 x=0;x<w;x+=16){
            UINT32 sx=rotate?w-16-x:x;
            uint8x16_t ys=vld1q_u8(yr+sx);uint8x8x2_t chroma=vld2_u8(ur+sx);
            if(rotate){ys=vcombine_u8(vrev64_u8(vget_high_u8(ys)),vrev64_u8(vget_low_u8(ys)));chroma.val[0]=vrev64_u8(chroma.val[0]);chroma.val[1]=vrev64_u8(chroma.val[1]);}
            uint8x8x2_t us=vzip_u8(chroma.val[0],chroma.val[0]),vs=vzip_u8(chroma.val[1],chroma.val[1]);
            Rgb8(vget_low_u8(ys),us.val[0],vs.val[0],row+x*4);
            Rgb8(vget_high_u8(ys),us.val[1],vs.val[1],row+x*4+32);
        }
    }
}


