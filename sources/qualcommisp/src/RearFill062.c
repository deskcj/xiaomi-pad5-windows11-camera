/* Windows ARM64 implementation of original NABU OV13B10 FillExposureSettings.
 * Ground truth: com.qti.sensor.nabu_ov13b10.so, RVA1178..1660.
 * Tested against execution of the original Android instructions, including
 * untouched output padding and the different GroupHoldOff delay behavior.
 * No sensor access here: this callback only produces the native command list.
 */
typedef unsigned int U32;
typedef unsigned char U8;
typedef unsigned __int64 U64;

static __forceinline U32 Read32(const U8 *p) { return *(const U32 *)p; }
static __forceinline void Write32(U8 *p, U32 x) { *(U32 *)p=x; }
static __forceinline void Emit(U8 *out, U32 i, U32 addr, U32 data) {
    U8 *p=out+40*i;
    Write32(p+8,addr);Write32(p+12,2);Write32(p+16,1);
    Write32(p+24,0);Write32(p+32,data);
}

__declspec(dllexport) U32 RearFill062(void *output,const void *input) {
    U8 *out=(U8 *)output;
    const U8 *in=(const U8 *)input,*info;
    U32 on,off,n=0,i,lines,fll,ag,dg,coarse,analog,digital;
    if(!out || !in)return 0;
    info=(const U8 *)(*(const U64 *)in);
    if(!info)return 0;
    on=Read32(info);off=Read32(info+0x198);
    /* Each native ABI group block has ten fixed command slots. */
    if(on>10 || off>10)return 0;
    lines=Read32(in+0x10);fll=Read32(in+0x14);
    ag=Read32(in+8);dg=Read32(in+12);
    coarse=Read32(info+0x334);analog=Read32(info+0x33c);digital=Read32(info+0x344);
    for(i=0;i<on;++i,++n){
        const U8 *p=info+40*i;U8 *q=out+40*n;
        Write32(q+8,Read32(p+8));Write32(q+12,Read32(p+12));
        Write32(q+16,Read32(p+16));Write32(q+24,Read32(p+24));
        Write32(q+32,Read32(p+32));
    }
    Emit(out,n++,0x3822,lines>=32760 ? 4 : 0x14);
    if(lines<32760){
        U32 addr=Read32(info+0x330);
        Emit(out,n++,addr,(fll>>8)&255);Emit(out,n++,addr+1,fll&255);
    }
    Emit(out,n++,coarse,(lines>>16)&255);
    Emit(out,n++,coarse+1,(lines>>8)&255);Emit(out,n++,coarse+2,lines&255);
    Emit(out,n++,analog,(ag>>8)&255);Emit(out,n++,analog+1,ag&255);
    Emit(out,n++,digital,(dg>>16)&255);Emit(out,n++,digital+1,(dg>>8)&255);
    Emit(out,n++,digital+2,dg&255);
    for(i=0;i<off;++i,++n){
        const U8 *p=info+0x198+40*i;U8 *q=out+40*n;
        Write32(q+8,Read32(p+8));Write32(q+12,Read32(p+12));
        Write32(q+16,Read32(p+16));Write32(q+32,Read32(p+32));
        /* The Android callback intentionally leaves this slot's delay alone. */
    }
    Write32(out,n);return n<50;
}
