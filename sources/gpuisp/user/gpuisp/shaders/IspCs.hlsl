// =====================================================================
// IspCs.hlsl - GPUISP2 D3D12 compute ISP, compiled to cs_5_0 bytecode at build time on the
// development PC (fxc). Nothing is compiled at runtime on the tablet.
//
//   FrontEndCS  : RAW16 Bayer -> full-resolution linear RGB (black level + lens shading + demosaic)
//   ColorDownCS : anti-aliased box downscale (groupshared) + AWB + CCM + tone curve + gamma
//   Nv12CS      : optional 5x5 bilateral on the post-tone RGB, then RGB -> NV12 (Y plane then
//                 interleaved UV plane, 256-byte row alignment)
//   SharpYCS    : optional luma-only unsharp over the finished NV12 Y plane (ISP_FLAG_SHARP)
//   ChromaSmoothCS : optional 5x5 binomial + 5x5 luma-guided x2 + saturation gain on the finished
//                 NV12 UV plane only (ISP_FLAG_CHROMA_SMOOTH, strength in ChromaGain)
//
// bench/src/GpuIspRef.cpp mirrors this arithmetic operation for operation; the bench asserts
// GPU == CPU within an 8-bit tolerance so a D3D12 binding bug can never look like a tuning
// problem. Keep both in step when changing either.
//
// Layout invariants (checked on the CPU side in GpuIspCore.cpp):
//   outW % 4 == 0, outH % 2 == 0, CropX+CropW <= RawW, CropY+CropH <= RawH,
//   CropW*OutW < 2^31 and 8-bit downscale factor <= (MAXTW-1)/GS.
// =====================================================================

#define GS        8      // thread group edge, in output pixels
#define MAXTW     24     // shared tile edge: needs GS * maxDownscale <= MAXTW - 1
#define TONE_LUT  256
#define ISP_FLAG_FULL 0x0020u

#ifndef DN_R
#define DN_R      2                   // bilateral radius; taps = (2*DN_R+1)^2 = 5x5
#endif
#define DN_TW     (GS * 4 + 2 * DN_R) // Nv12CS tile columns = 36, must stay <= MAXTW
#define DN_TH     (GS + 1 + 2 * DN_R) // Nv12CS tile rows = 13 (odd rows also read by+1 for chroma)
#define DN_GT     (GS * GS / 2)       // Nv12CS threads per group: each owns a 4x2 block of pixels

#define ISP_FLAG_LSC   0x0001u
#define ISP_FLAG_AWB   0x0002u
#define ISP_FLAG_CCM   0x0004u
#define ISP_FLAG_TONE  0x0008u
#define ISP_FLAG_GAMMA 0x0010u
#define ISP_FLAG_DENOISE 0x0040u
#define ISP_FLAG_SHARP 0x0080u
#define ISP_FLAG_CHROMA_SMOOTH 0x0100u
#define ISP_FLAG_CHROMA_RECENTRE 0x0200u

#define SH_RMAX 4                                // SharpYCS max runtime blur radius (halo loaded)
#define SH_OW   (GS * 4)                         // SharpYCS owned columns per group = 32
#define SH_TW   (SH_OW + 2 * SH_RMAX)            // SharpYCS tile columns = 40
#define SH_TH   (GS     + 2 * SH_RMAX)           // SharpYCS tile rows = 16
#define SH_VW   SH_TW                            // vertical-partial columns: the horizontal pass reads
                                                 //  [t-R, t+R] for t in [R, R+SH_OW-1] = exactly these
#define SH_VH   GS                               // vertical partials only on the owned rows

#define CS_R    2                                // ChromaSmoothCS binomial radius, in CHROMA pixels
#define CS_G    2                                // ChromaGuide1CS / ChromaGuide2CS guided radius
#define CS_TW   (GS * 2 + 2 * CS_R)              // binomial tile columns: a group owns GS UV words
#define CS_TH   (GS     + 2 * CS_R)              // binomial tile rows = GS chroma rows
#define CS_GW   (GS * 2 + 2 * CS_G)              // guided tile columns (equal to CS_TW only while both
#define CS_GH   (GS     + 2 * CS_G)              //  radii are 2; named separately so they cannot drift)
#define CS_CK        8              // ChromaGuide1CS near-neutral window: L1 in chroma codes
#define CS_CK_SCALE  64             // the re-centring offset is quantised to 1/CS_CK_SCALE code
#define CS_STAT_SLOTS 4             // IspChromaStat's elements: (sumU, sumV, count, pad)

#define CH_R  0u
#define CH_GR 1u
#define CH_GB 2u
#define CH_B  3u

// fxc allocates one 16-byte register per element of a scalar float[] in a cbuffer, so every
// matrix here is a float4 row (a tightly packed C mirror would read the wrong lanes; verified by
// disassembling ColorDownCS). GpuIspParams in src/GpuIsp.h mirrors these byte offsets.
cbuffer IspParams : register(b0)
{
    uint  RawW, RawH, OutW, OutH;                 //   0
    uint  CropX, CropY, CropW, CropH;             //  16
    uint  LscCols, LscRows, Flags, CfaOff;        //  32  CfaOff: bit0 = x parity, bit1 = y parity
    float BlackLevel, WhiteLevel, ExGain, Sharpen; //  48
    float4 Awb;                                   //  64  R, Gr, Gb, B gains
    float4 CcmR, CcmG, CcmB;                      //  80, 96, 112
    float4 YuvR, YuvG, YuvB;                      // 128, 144, 160
    float4 YuvMul;                                // 176  x = Y scale, y = U, z = V
    float4 YuvAdd;                                // 192
    float  Gamma, Denoise, LscMode, ChromaGain;   // 208
    float  SharpenRadius, SharpenThreshold, SharpenLimit, SharpenEdgeLo;   // 224
    float  SharpenEdgeHi, SharpenEdgeScale, NrRangeA, NrRangeB;   // 240
    float  ChromaBiasU, ChromaBiasV, ChromaInvSigma, _biasPad2;   // 256 -> 272
};

Texture2D<uint>          RawTex     : register(t0);   // R16_UINT, sensor resolution
Texture2D<float4>        RgbFullSrv : register(t1);   // RGBA16F, sensor resolution
Texture2D<float4>        RgbOutSrv  : register(t2);   // RGBA16F, outW x outH
StructuredBuffer<float4> LscTex     : register(t3);   // LscRows*LscCols cells, (R,Gr,Gb,B)
StructuredBuffer<float>  ToneLut    : register(t4);   // 3 * TONE_LUT

RWTexture2D<float4>  RgbFullUav : register(u0);
RWTexture2D<float4>  RgbOutUav  : register(u1);
RWStructuredBuffer<uint> Nv12       : register(u2);    // NV12 bytes reinterpreted as uint words
RWStructuredBuffer<uint> Nv12Sharp  : register(u3);    // SharpYCS destination: the Y plane only
RWStructuredBuffer<uint> Nv12Uv     : register(u4);    // ChromaSmoothCS destination: the UV plane only
// Stage 5's intermediate chroma fields, in the shipped UV byte order (pitch*cy + 2*cx + ch), one float
// per chroma sample. Chunked as four words per UV word so the store index is the same expression the
// final pack uses. See the stage-5 header for why these are separate dispatches instead of a
// groupshared cascade inside one kernel.
RWStructuredBuffer<float> IspChromaA : register(u5);   // ChromaSmoothCS binomial, ChromaGuide1CS input
RWStructuredBuffer<float> IspChromaB : register(u6);   // ChromaGuide1CS result, ChromaGuide2CS input
// Stage 5's re-centring statistic: the whole frame's rounded near-neutral chroma deviations, summed in
// exact integers by ChromaGuide1CS as (sumU, sumV, count, pad). Integers rather than float atomics
// because a float sum depends on the order groups retire, and the offset would then differ between two
// runs of the same frame - integer adds are associative, so this value is order-free.
RWStructuredBuffer<int> IspChromaStat : register(u7);

groupshared float TileR[MAXTW * MAXTW];   // ColorDownCS downscale tile
groupshared float TileG[MAXTW * MAXTW];   // (only ColorDownCS references these, so fxc drops the
groupshared float TileB[MAXTW * MAXTW];   //  allocation from every other entry point's bytecode)

// Nv12CS has its own tile at the true bilateral footprint (36x13, not the 40x40 ColorDown alias).
// Luma rides in .w because the range kernel needs it for the centre and all 25 taps of every sample;
// recomputing that dot product cost ~200 dots per thread. One float4 per sample instead of four scalar
// arrays moves the same bytes but costs the tap loop one LDS per tap rather than four, and this stage is
// latency/issue-bound, so the instruction count is what the frame time follows.
groupshared float4 DnRGBL[DN_TW * DN_TH];
groupshared float3 DnFiltered[GS * 4 * GS];

groupshared float ShY[SH_TW * SH_TH];           // SharpYCS Y-plane tile, its own 40x16 footprint
// SharpYCS's separable form: the binomial vertical collapse of the tile, and the window min/max the edge
// protection needs, both kept per (owned row, tile column). See SharpYCS for why this is bit-identical to
// reading the whole 2D window per pixel.
groupshared float ShV[SH_VH * SH_VW];
groupshared float ShMin[SH_VH * SH_VW];
groupshared float ShMax[SH_VH * SH_VW];
groupshared float ShW[2 * SH_RMAX + 1];         // row 2R of Pascal's triangle for the runtime radius

// ChromaSmoothCS tile: the UV window, both channels, [channel][row][col] with channel 0 = U, 1 = V.
groupshared float CsT[2 * CS_TW * CS_TH];

// The guided passes' tiles, the same footprint: CsF = the input field window both channels, CsG = its
// luma guide at chroma resolution. The field could be read straight from IspChromaA/B - that measured
// 33 ms per pass, because each thread's four sites share almost all of their 5x5 windows and re-gather
// them independently, 300 UAV loads per thread. One load per site into groupshared is the same numbers
// read once, and fxc only reserves arrays an entry point actually references, so these cost the binomial
// kernel nothing.
groupshared float CsF[2 * CS_GW * CS_GH];
groupshared float CsG[CS_GW * CS_GH];

// ChromaGuide1CS's per-group integer partial sums (sumU, sumV, count), reduced from the group's 64
// threads before one atomic flush per group. Three ints, so it costs the guided passes no occupancy.
groupshared int CsStat[3];
// MHC needs a two-pixel halo. Normalize each raw tap once per group instead of
// repeating black/LSC normalization for each interpolated colour channel.
#define FE_TW (GS + 4)
groupshared float FeRaw[FE_TW * FE_TW];

// ---------------------------------------------------------------- helpers

static uint ChanAt(uint2 p)
{
    uint yy = (p.y + ((CfaOff >> 1u) & 1u)) & 1u;
    uint xx = (p.x + (CfaOff & 1u)) & 1u;
    if (yy == 0u)
        return (xx == 0u) ? CH_B : CH_GB;
    return (xx == 0u) ? CH_GR : CH_R;
}

static float LscGainFor(uint ch, float4 g)
{
    if (ch == CH_R) return g.x;
    if (ch == CH_GR) return g.y;
    if (ch == CH_GB) return g.z;
    return g.w;
}

float4 LscBilinear(uint2 q)
{
    float gx = ((float)q.x + 0.5) / (float)RawW * (float)LscCols - 0.5;
    float gy = ((float)q.y + 0.5) / (float)RawH * (float)LscRows - 0.5;
    gx = clamp(gx, 0.0, (float)LscCols - 1.0);
    gy = clamp(gy, 0.0, (float)LscRows - 1.0);
    int x0 = (int)floor(gx), y0 = (int)floor(gy);
    int x1 = min(x0 + 1, (int)LscCols - 1);
    int y1 = min(y0 + 1, (int)LscRows - 1);
    float fx = gx - (float)x0, fy = gy - (float)y0;
    int c0 = (int)LscCols;
    float4 a = LscTex[y0 * c0 + x0], b = LscTex[y0 * c0 + x1];
    float4 c = LscTex[y1 * c0 + x0], d = LscTex[y1 * c0 + x1];
    return lerp(lerp(a, b, fx), lerp(c, d, fx), fy);
}

// Linear sensor value in [0,1]: black level, lens shading, white level, exposure gain.
float RawNorm(int2 p)
{
    // Reflect about the edge pixel: preserves Bayer parity at image boundaries.
    int cx = p.x < 0 ? -p.x : p.x;
    int cy = p.y < 0 ? -p.y : p.y;
    cx = cx >= (int)RawW ? 2 * ((int)RawW - 1) - cx : cx;
    cy = cy >= (int)RawH ? 2 * ((int)RawH - 1) - cy : cy;
    uint2 q = uint2(cx, cy);
    float v = (float)RawTex.Load(int3(q, 0));
    float g = 1.0;
    if ((Flags & ISP_FLAG_LSC) != 0u && LscCols > 1u && LscRows > 1u)
        g = LscGainFor(ChanAt(q), LscBilinear(q));
    float range = max((float)WhiteLevel - (float)BlackLevel, 1.0);
    // Retain linear headroom through AWB/CCM; early G clipping turns whites magenta.
    return max((v - (float)BlackLevel) * g * ExGain / range, 0.0);
}

float ToneAt(int ch, float v)
{
    float f = saturate(v) * (float)(TONE_LUT - 1);
    int i0 = (int)f;
    int i1 = min(i0 + 1, TONE_LUT - 1);
    float fr = f - (float)i0;
    float a = ToneLut[ch * TONE_LUT + i0];
    float b = ToneLut[ch * TONE_LUT + i1];
    return a + (b - a) * fr;
}

// ---------------------------------------------------------------- demosaic

float FeTap(int2 p) { return FeRaw[p.y * FE_TW + p.x]; }

float3 ApplyColour(float3 c)
{
    if ((Flags & ISP_FLAG_AWB) != 0u)
        c = float3(c.r * Awb.x, c.g * Awb.y, c.b * Awb.w);
    if ((Flags & ISP_FLAG_CCM) != 0u)
        c = float3(CcmR.x * c.r + CcmR.y * c.g + CcmR.z * c.b,
                   CcmG.x * c.r + CcmG.y * c.g + CcmG.z * c.b,
                   CcmB.x * c.r + CcmB.y * c.g + CcmB.z * c.b);
    c = saturate(c);
    if ((Flags & ISP_FLAG_TONE) != 0u)
        c = float3(ToneAt(0, c.r), ToneAt(1, c.g), ToneAt(2, c.b));
    if ((Flags & ISP_FLAG_GAMMA) != 0u)
        c = pow(max(c, 0.0), 1.0 / max(Gamma, 1.0));
    return c;
}

// Malvar/He/Cutler 5x5 gradient-corrected linear reconstruction, ICASSP 2004 Fig.2.
// Crucial invariant: constant R/G/B planes reconstruct exactly, regardless of CFA site.
// The previous 3x3 formula added G-B at green sites and R into G at red sites;
// it therefore printed a 2x2 lattice even on a perfectly uniform coloured patch.
float3 DemosaicMhc(int2 p, int2 local)
{
    uint ch = ChanAt((uint2)p);
    float v=FeTap(local);
    float h1=FeTap(local+int2(-1,0))+FeTap(local+int2(1,0));
    float v1=FeTap(local+int2(0,-1))+FeTap(local+int2(0,1));
    float h2=FeTap(local+int2(-2,0))+FeTap(local+int2(2,0));
    float v2=FeTap(local+int2(0,-2))+FeTap(local+int2(0,2));
    float diag=FeTap(local+int2(-1,-1))+FeTap(local+int2(1,-1))+
               FeTap(local+int2(-1,1))+FeTap(local+int2(1,1));
    if (ch == CH_GR || ch == CH_GB)
    {
        bool horizIsR = (ChanAt(uint2((uint)p.x + 1u, (uint)p.y)) == CH_R);
        float horizontal=(5.0*v+4.0*h1-h2-diag+0.5*v2)*0.125;
        float vertical=(5.0*v+4.0*v1-v2-diag+0.5*h2)*0.125;
        return max(float3(horizIsR?horizontal:vertical,v,horizIsR?vertical:horizontal),0.0);
    }
    float G=(4.0*v+2.0*(h1+v1)-(h2+v2))*0.125;
    float other=(6.0*v+2.0*diag-1.5*(h2+v2))*0.125;
    return max(ch==CH_R?float3(v,G,other):float3(other,G,v),0.0);
}

// ---------------------------------------------------------------- stage 1: full-res RGB

[numthreads(GS, GS, 1)]
void FrontEndCS(uint3 tid : SV_DispatchThreadID, uint3 gtid : SV_GroupThreadID, uint3 gid : SV_GroupID)
{
    uint lane=gtid.y*GS+gtid.x;
    for(uint i=lane;i<FE_TW*FE_TW;i+=GS*GS)
        FeRaw[i]=RawNorm((int2)gid.xy*GS+int2(i%FE_TW,i/FE_TW)-2);
    GroupMemoryBarrierWithGroupSync();
    if (tid.x >= RawW || tid.y >= RawH) return;
    float3 c = DemosaicMhc((int2)tid.xy,(int2)gtid.xy+2);
    if ((Flags & 0x0400u) == 0u || (Flags & ISP_FLAG_FULL) != 0u)
        RgbFullUav[(int2)tid.xy] = float4(c.x, c.y, c.z, 1.0);
    if ((Flags & 0x0400u) != 0u)
    {
        // Preserve the former RGBA16F stage boundary, including round-to-nearest-even.
        c = f16tof32(f32tof16(c));
        float area=(float)(OutW*OutH);
        c = (c * area) / area; // same weighted 1:1 sample arithmetic as ColorDownCS
        c = ApplyColour(c);
        RgbOutUav[(int2)tid.xy] = float4(c,1.0);
    }
}

// ---------------------------------------------------------------- stage 2: downscale + colour

// Exact same area weights and accumulation order, using the texture cache directly.
// Avoids 19KB/group shared arrays, barriers, and shared-tile indirection on Adreno.
[numthreads(GS, GS, 1)]
void ColorDownCS(uint3 tid : SV_DispatchThreadID)
{
    uint ox=tid.x,oy=tid.y;
    if(ox>=OutW || oy>=OutH)return;
    uint xn0=ox*CropW,xn1=(ox+1u)*CropW;
    uint yn0=oy*CropH,yn1=(oy+1u)*CropH;
    int px0=(int)(xn0/OutW),px1=(int)((xn1+OutW-1u)/OutW);
    int py0=(int)(yn0/OutH),py1=(int)((yn1+OutH-1u)/OutH);
    float3 acc=0;int wSum=0;
    for(int y=py0;y<py1;y++){
        int wy=min((int)yn1,(y+1)*(int)OutH)-max((int)yn0,y*(int)OutH);
        for(int x=px0;x<px1;x++){
            int wx=min((int)xn1,(x+1)*(int)OutW)-max((int)xn0,x*(int)OutW);
            float w=(float)(wx*wy);
            float3 c=RgbFullSrv.Load(int3(x+(int)CropX,y+(int)CropY,0)).rgb;
            acc+=w*c;wSum+=wx*wy;
        }
    }
    float3 result=ApplyColour(acc/(float)max(wSum,1));
    RgbOutUav[int2(ox,oy)]=float4(result,1.0);
}

// One thread owns a 4x2 block - four luma pixels of row by and the four of row by+1 - so every write
// is one aligned uint (probe-verified view type) and every 5x5 bilateral is evaluated EXACTLY once.
// Thread (bx, ty): output rows 2*ty and 2*ty+1, columns 4*bx .. 4*bx+3.

// Separable binomial (row 4 of Pascal's triangle) spatial taps. A function rather than a
// const array because fxc will not index a static const array from a loop counter.
float DnTap(int o)
{
    if (o == 2) return 6.0;
    if (o == 1 || o == 3) return 4.0;
    return 1.0;
}

// 5x5 bilateral over the groupshared tile of post-tone RGB. The range test uses gamma-encoded
// luma (the Y row of the output matrix), so edges in the rendered image survive while the noise
// the tone curve amplifies does not. GpuIspRef.cpp reproduces this loop tap for tap.
float3 DnFilter(int tx, int ty)
{
    int ci = ty * DN_TW + tx;
    float lumC = DnRGBL[ci].w;
    // NrRangeB == 0 (and a zero-initialised struct) keeps the shipped uniform reciprocal exactly,
    // including fxc's own lowering of that division. Opting in switches to a fitted line in gamma
    // luma, which is one mad: no division, no sqrt, so the CPU mirror can reproduce it bit for bit.
    float invRange = (NrRangeB == 0.0) ? (1.0 / max(Denoise, 1e-6))
                                       : (NrRangeA + NrRangeB * lumC);
    float accR = 0.0, accG = 0.0, accB = 0.0, accW = 0.0;
    for (int dy = -DN_R; dy <= DN_R; dy++)
    {
        float wy = DnTap(dy + DN_R);
        for (int dx = -DN_R; dx <= DN_R; dx++)
        {
            int o = (ty + dy) * DN_TW + (tx + dx);
            float4 tap = DnRGBL[o];
            float w = wy * DnTap(dx + DN_R) * max(0.0, 1.0 - abs(tap.w - lumC) * invRange);
            accR += w * tap.r;
            accG += w * tap.g;
            accB += w * tap.b;
            accW += w;
        }
    }
    float inv = 1.0 / max(accW, 1e-6);
    return float3(accR * inv, accG * inv, accB * inv);
}

[numthreads(GS, GS / 2, 1)]
void Nv12CSBlock(uint3 tid : SV_DispatchThreadID, uint3 gtid : SV_GroupThreadID, uint3 gid : SV_GroupID)
{
    uint bx = tid.x, by = tid.y * 2u;
    uint cols = OutW / 4u;

    // The tile is loaded (and the barrier reached) by every thread of the group, including the
    // edge threads that store nothing - same rule as ColorDownCS.
    const bool dn = (Flags & ISP_FLAG_DENOISE) != 0u;
    int txBase = (int)(gid.x * (uint)GS * 4u) - DN_R;
    int tyBase = (int)(gid.y * (uint)GS) - DN_R;
    if (dn)
    {
        for (uint li = gtid.y * (uint)GS + gtid.x; li < DN_TW * DN_TH; li += (uint)DN_GT)
        {
            uint tx = li % (uint)DN_TW, ty = li / (uint)DN_TW;
            int px = clamp(txBase + (int)tx, 0, (int)OutW - 1);
            int py = clamp(tyBase + (int)ty, 0, (int)OutH - 1);
            float4 tc = RgbOutSrv.Load(int3(px, py, 0));
            int ti = (int)ty * DN_TW + (int)tx;
            DnRGBL[ti] = float4(tc.r, tc.g, tc.b,
                                YuvR.x * tc.r + YuvR.y * tc.g + YuvR.z * tc.b);
        }
        GroupMemoryBarrierWithGroupSync();
    }

    if (bx >= cols || by >= OutH) return;

    int lx = (int)bx * 4 - txBase;     // tile column of output pixel x0
    int ly = (int)by - tyBase;         // tile row of output row by

    // s[0..3] is row by, s[4..7] row by+1: the four luma pixels and the two 2x2 chroma blocks.
    // Two explicit branches rather than a select: with a select fxc evaluates DnFilter even when
    // the flag is off, which cost 60 ms of shared-memory traffic on the 1920x1080 bench frame.
    // dn is uniform across the group (it comes from Flags), so the denoise-off path may also skip
    // the tile load and the barrier outright and read the eight samples from the texture; the
    // clamped coordinates are the same ones the load loop would have used, so the values match.
    float3 s[8];
    // Both rows of the pair belong to this thread, so all eight bilateral filters are its own work;
    // no second thread ever recomputes them. OutH is even and by is even, so an unpaired row can only
    // come from the host rounding the grid up, and the guard keeps that thread from reading past it.
    uint sampleCount = (by + 1u < OutH) ? 8u : 4u;
    if (dn)
    {
        for (uint k = 0u; k < sampleCount; k++)
            s[k] = DnFilter(lx + (int)(k & 3u), ly + (int)(k >> 2u));
    }
    else
    {
        for (uint m = 0u; m < sampleCount; m++)
        {
            int px = clamp((int)bx * 4 + (int)(m & 3u), 0, (int)OutW - 1);
            int py = clamp((int)by + (int)(m >> 2u), 0, (int)OutH - 1);
            s[m] = RgbOutSrv.Load(int3(px, py, 0)).rgb;
        }
    }

    uint yPitch = (OutW + 255u) & ~255u;
    uint uvPitch = (OutW + 255u) & ~255u;   // a UV row holds outW/2 interleaved pairs = OutW bytes

    uint packedY = 0u;
    for (uint i = 0u; i < 4u; i++)
    {
        float3 c = s[i];
        float yv = YuvMul.x * (YuvR.x * c.r + YuvR.y * c.g + YuvR.z * c.b) + YuvAdd.x;
        packedY |= ((uint)clamp(yv + 0.5, 0.0, 255.0)) << (8u * i);
    }
    Nv12[(yPitch >> 2u) * by + bx] = packedY;

    if (by + 1u >= OutH) return;

    uint packedY1 = 0u;
    for (uint i1 = 0u; i1 < 4u; i1++)
    {
        float3 c1 = s[4u + i1];
        float yv1 = YuvMul.x * (YuvR.x * c1.r + YuvR.y * c1.g + YuvR.z * c1.b) + YuvAdd.x;
        packedY1 |= ((uint)clamp(yv1 + 0.5, 0.0, 255.0)) << (8u * i1);
    }
    Nv12[(yPitch >> 2u) * (by + 1u) + bx] = packedY1;

    uint packedUV = 0u;
    for (uint j = 0u; j < 2u; j++)
    {
        float3 c00 = s[j * 2u];
        float3 c10 = s[j * 2u + 1u];
        float3 c01 = s[4u + j * 2u];
        float3 c11 = s[4u + j * 2u + 1u];
        float3 m = (c00 + c10 + c01 + c11) * 0.25;
        float u = YuvMul.y * (YuvG.x * m.r + YuvG.y * m.g + YuvG.z * m.b) + YuvAdd.y;
        float v = YuvMul.z * (YuvB.x * m.r + YuvB.y * m.g + YuvB.z * m.b) + YuvAdd.z;
        uint bu = (uint)clamp(u + 0.5, 0.0, 255.0);
        uint bv = (uint)clamp(v + 0.5, 0.0, 255.0);
        packedUV |= bu << (8u * (j * 2u));
        packedUV |= bv << (8u * (j * 2u + 1u));
    }
    // UV plane follows the Y plane; both pitches are multiples of 256 so the /4 stays exact.
    Nv12[((yPitch * OutH) >> 2u) + (uvPitch >> 2u) * (by >> 1u) + bx] = packedUV;
}

// One filter per lane, retaining the same 32x8 output tile and host dispatch grid.
// The block version above runs eight filters in each of only 32 lanes. Sharing
// their fp32 results lets 256 lanes perform independent filters before packing.
// No fp16 conversion is introduced at this boundary.
[numthreads(GS * 4, GS, 1)]
void Nv12CS(uint3 tid : SV_DispatchThreadID, uint3 gtid : SV_GroupThreadID, uint3 gid : SV_GroupID)
{
    const uint tileW = GS * 4u;
    const uint threadIndex = gtid.y * tileW + gtid.x;
    const bool dn = (Flags & ISP_FLAG_DENOISE) != 0u;
    int txBase = (int)(gid.x * tileW) - DN_R;
    int tyBase = (int)(gid.y * (uint)GS) - DN_R;
    if (dn)
    {
        for (uint li = threadIndex; li < DN_TW * DN_TH; li += GS * GS * 4u)
        {
            uint tx = li % (uint)DN_TW, ty = li / (uint)DN_TW;
            int px = clamp(txBase + (int)tx, 0, (int)OutW - 1);
            int py = clamp(tyBase + (int)ty, 0, (int)OutH - 1);
            float4 tc = RgbOutSrv.Load(int3(px, py, 0));
            DnRGBL[ty * DN_TW + tx] = float4(tc.r, tc.g, tc.b,
                YuvR.x * tc.r + YuvR.y * tc.g + YuvR.z * tc.b);
        }
        GroupMemoryBarrierWithGroupSync();
    }
    float3 c = float3(0.0, 0.0, 0.0);
    if (tid.x < OutW && tid.y < OutH)
    {
        if (dn) c = DnFilter((int)gtid.x + DN_R, (int)gtid.y + DN_R);
        else c = RgbOutSrv.Load(int3(tid.xy, 0)).rgb;
    }
    DnFiltered[threadIndex] = c;
    GroupMemoryBarrierWithGroupSync();
    if ((gtid.x & 3u) != 0u || tid.x >= OutW || tid.y >= OutH) return;
    uint yPitch = (OutW + 255u) & ~255u;
    uint uvPitch = yPitch;
    uint packedY = 0u;
    for (uint i = 0u; i < 4u; i++)
    {
        float3 p = DnFiltered[threadIndex + i];
        float yv = YuvMul.x * (YuvR.x * p.r + YuvR.y * p.g + YuvR.z * p.b) + YuvAdd.x;
        packedY |= ((uint)clamp(yv + 0.5, 0.0, 255.0)) << (8u * i);
    }
    Nv12[(yPitch >> 2u) * tid.y + (tid.x >> 2u)] = packedY;
    if ((gtid.y & 1u) != 0u || tid.y + 1u >= OutH) return;
    uint packedUV = 0u;
    for (uint j = 0u; j < 2u; j++)
    {
        uint k = threadIndex + j * 2u;
        float3 c00 = DnFiltered[k];
        float3 c10 = DnFiltered[k + 1u];
        float3 c01 = DnFiltered[k + tileW];
        float3 c11 = DnFiltered[k + tileW + 1u];
        float3 m = (c00 + c10 + c01 + c11) * 0.25;
        float u = YuvMul.y * (YuvG.x * m.r + YuvG.y * m.g + YuvG.z * m.b) + YuvAdd.y;
        float v = YuvMul.z * (YuvB.x * m.r + YuvB.y * m.g + YuvB.z * m.b) + YuvAdd.z;
        packedUV |= (uint)clamp(u + 0.5, 0.0, 255.0) << (16u * j);
        packedUV |= (uint)clamp(v + 0.5, 0.0, 255.0) << (16u * j + 8u);
    }
    Nv12[((yPitch * OutH) >> 2u) + (uvPitch >> 2u) * (tid.y >> 1u) + (tid.x >> 2u)] = packedUV;
}

// ---------------------------------------------------------------- stage 4: luma unsharp

// Row 2r of Pascal's triangle, written as one assigned value so fxc cannot merge the
// early-return branches into an uninitialised read (it warns X4000 on the return form).
static float SmTap(int o, int r)
{
    float w = 1.0;
    if (r == 1)
    {
        if (o == 1) w = 2.0;
    }
    else if (r == 2)
    {
        if (o == 2) w = 6.0;
        else if (o == 1 || o == 3) w = 4.0;
    }
    else if (r == 3)
    {
        if (o == 3) w = 20.0;
        else if (o == 2 || o == 4) w = 15.0;
        else if (o == 1 || o == 5) w = 6.0;
    }
    else
    {
        if (o == 4) w = 70.0;
        else if (o == 3 || o == 5) w = 56.0;
        else if (o == 2 || o == 6) w = 28.0;
        else if (o == 1 || o == 7) w = 8.0;
    }
    return w;
}

// Y' = Y + amount * (Y - binomialBlur(Y, R)) on the FINAL 8-bit NV12 luma plane.
// Runs strictly after Nv12CS, so the blur base is already denoised - it cannot resurrect
// the grain the bilateral removed. Chroma is never touched, keeping every colour
// calibration made against the UV plane bit-identical.
// Reads Nv12 (u2) and writes Nv12Sharp (u3), a Y-plane-only scratch: writing back into
// Nv12 would race, because a group's +/-SH_RMAX px halo reaches columns that the neighbouring
// group produces in the same dispatch, so the blur base would depend on scheduling.
//
// Two entry points, one arithmetic. SharpYCS reads the whole (2R+1)^2 window per pixel;
// SharpYCSep collapses the binomial vertically in groupshared first, so a column of the tile
// costs (2R+1) loads instead of every pixel costing (2R+1)^2. They give the same integer because
// every weight and sample is an integer and both accumulators peak at 255 * 2^(4R) = 16,711,680,
// which fp32 holds exactly, so summation order cannot matter. The gate is a zero byte diff between
// the two forms on both live geometries at R = 1..4. GpuIspRef.cpp's GpuIspRefSharpY stays the
// tap-for-tap 2D form, so the CPU mirror checks both instead of mirroring one.
// The core picks by radius: measured warm on the front geometry the collapse costs +1.7 ms at R=1
// and saves 10.8 ms at R=4, and fxc only reserves groupshared an entry point references, so keeping
// both forms costs the direct one nothing.

// The injection tail both forms share: deadzone, symmetric pre/post limits, edge protection.
static float SharpInject(float c, float blur, float vmin, float vmax)
{
    float thr = SharpenThreshold;
    float limit = SharpenLimit;
    // guards in this order: deadzone, pre-multiply limit, post-multiply limit.
    // Both limiters are symmetric: clamping only the positive side would let an isolated
    // dark spike through at full strength (amount*|hp| up to limit instead of amount*limit).
    float hp = c - blur;
    float a = abs(hp);
    if (a > thr)
    {
        hp = (hp > 0.0) ? (a - thr) : (thr - a);
        if (limit > 0.0 && hp > limit) hp = limit;
        if (limit > 0.0 && hp < -limit) hp = -limit;
        hp = Sharpen * hp;
        if (limit > 0.0 && hp > limit) hp = limit;
        if (limit > 0.0 && hp < -limit) hp = -limit;
    }
    else
    {
        hp = 0.0;
    }
    // Edge protection: fade the injection out as the local contrast of the very window the blur just
    // read rises, so real contours stop ringing while flat texture keeps its boost.
    // SharpenEdgeScale == 0 - which is also the zero-initialised struct - means protection off.
    if (SharpenEdgeScale > 0.0)
        hp = saturate((SharpenEdgeHi - (vmax - vmin)) * SharpenEdgeScale) * hp;
    return clamp(c + hp, 0.0, 255.0);
}

// Load the group's Y tile, haloed by SH_RMAX on every side, clamping the reads to the frame.
static void SharpLoadTile(uint3 gtid, uint3 gid, int txBase, int tyBase, uint yPitch)
{
    for (uint li = gtid.y * (uint)GS + gtid.x; li < SH_TW * SH_TH; li += (uint)(GS * GS))
    {
        uint tx = li % (uint)SH_TW, ty = li / (uint)SH_TW;
        int px = clamp(txBase + (int)tx, 0, (int)OutW - 1);
        int py = clamp(tyBase + (int)ty, 0, (int)OutH - 1);
        uint w = Nv12[(yPitch >> 2u) * (uint)py + (uint)px / 4u];
        ShY[(int)ty * SH_TW + (int)tx] = (float)((w >> (8u * ((uint)px & 3u))) & 255u);
    }
}

[numthreads(GS, GS, 1)]
void SharpYCSBlock(uint3 tid : SV_DispatchThreadID, uint3 gtid : SV_GroupThreadID, uint3 gid : SV_GroupID)
{
    uint bx = tid.x, by = tid.y;
    uint cols = OutW / 4u;
    uint yPitch = (OutW + 255u) & ~255u;

    int txBase = (int)(gid.x * (uint)SH_OW) - SH_RMAX;
    int tyBase = (int)(gid.y * (uint)GS) - SH_RMAX;
    SharpLoadTile(gtid, gid, txBase, tyBase, yPitch);
    GroupMemoryBarrierWithGroupSync();

    if (bx >= cols || by >= OutH) return;

    int R = clamp((int)(SharpenRadius + 0.5), 1, SH_RMAX);
    // The 2D binomial sum is 2^(4R), so the accumulator peaks at 255 * 2^(4R). fp32 keeps integer
    // sums exact below 2^24, which is what caps SH_RMAX at 4 (16,711,680); R=5 would silently lose
    // the GPU==CPU bit-identity this stage is built on.
    float invDiv = 1.0 / (float)(1u << (4u * (uint)R));

    int lx = (int)bx * 4 - txBase;
    int ly = (int)by - tyBase;

    uint packedY = 0u;
    for (uint i = 0u; i < 4u; i++)
    {
        int tx = lx + (int)i;
        float c = ShY[ly * SH_TW + tx];

        // dy outer / dx inner in ascending order - the CPU mirror keeps this exact order.
        // Integer samples x integer weights are exact in fp32 and invDiv is a power of two,
        // so blur is bit-identical GPU vs CPU.
        float acc = 0.0, vmin = 255.0, vmax = 0.0;
        for (int dy = -R; dy <= R; dy++)
            for (int dx = -R; dx <= R; dx++)
            {
                float s = ShY[(ly + dy) * SH_TW + (tx + dx)];
                acc += SmTap(dy + R, R) * SmTap(dx + R, R) * s;
                vmin = min(vmin, s);
                vmax = max(vmax, s);
            }
        packedY |= ((uint)clamp(SharpInject(c, acc * invDiv, vmin, vmax) + 0.5, 0.0, 255.0)) << (8u * i);
    }
    Nv12Sharp[(yPitch >> 2u) * by + bx] = packedY;
}

groupshared uint SharpFiltered[GS * 4 * GS];

// Same exact integer binomial sum/min/max and SharpInject as SharpYCSBlock.
// One lane filters one pixel; one lane in four writes the finished packed word.
[numthreads(GS * 4, GS, 1)]
void SharpYCS(uint3 tid : SV_DispatchThreadID, uint3 gtid : SV_GroupThreadID, uint3 gid : SV_GroupID)
{
    const uint tileW = GS * 4u;
    const uint lane = gtid.y * tileW + gtid.x;
    const uint yPitch = (OutW + 255u) & ~255u;
    const int txBase = (int)(gid.x * tileW) - SH_RMAX;
    const int tyBase = (int)(gid.y * GS) - SH_RMAX;
    for (uint li = lane; li < SH_TW * SH_TH; li += GS * GS * 4u)
    {
        uint tx = li % (uint)SH_TW, ty = li / (uint)SH_TW;
        int px = clamp(txBase + (int)tx, 0, (int)OutW - 1);
        int py = clamp(tyBase + (int)ty, 0, (int)OutH - 1);
        uint word = Nv12[(yPitch >> 2u) * (uint)py + (uint)px / 4u];
        ShY[ty * SH_TW + tx] = (float)((word >> (8u * ((uint)px & 3u))) & 255u);
    }
    GroupMemoryBarrierWithGroupSync();
    uint result = 0u;
    if (tid.x < OutW && tid.y < OutH)
    {
        int R = clamp((int)(SharpenRadius + 0.5), 1, SH_RMAX);
        int tx = (int)gtid.x + SH_RMAX, ty = (int)gtid.y + SH_RMAX;
        float center = ShY[ty * SH_TW + tx];
        float acc = 0.0, vmin = 255.0, vmax = 0.0;
        for (int dy = -R; dy <= R; dy++)
            for (int dx = -R; dx <= R; dx++)
            {
                float value = ShY[(ty + dy) * SH_TW + tx + dx];
                acc += SmTap(dy + R, R) * SmTap(dx + R, R) * value;
                vmin = min(vmin, value);vmax = max(vmax, value);
            }
        float invDiv = 1.0 / (float)(1u << (4u * (uint)R));
        result = (uint)clamp(SharpInject(center, acc * invDiv, vmin, vmax) + 0.5, 0.0, 255.0);
    }
    SharpFiltered[lane] = result;
    GroupMemoryBarrierWithGroupSync();
    if ((gtid.x & 3u) != 0u || tid.x >= OutW || tid.y >= OutH) return;
    uint packed = SharpFiltered[lane] | (SharpFiltered[lane + 1u] << 8u) |
                  (SharpFiltered[lane + 2u] << 16u) | (SharpFiltered[lane + 3u] << 24u);
    Nv12Sharp[(yPitch >> 2u) * tid.y + (tid.x >> 2u)] = packed;
}

[numthreads(GS, GS, 1)]
void SharpYCSep(uint3 tid : SV_DispatchThreadID, uint3 gtid : SV_GroupThreadID, uint3 gid : SV_GroupID)
{
    uint bx = tid.x, by = tid.y;
    uint cols = OutW / 4u;
    uint yPitch = (OutW + 255u) & ~255u;

    int R = clamp((int)(SharpenRadius + 0.5), 1, SH_RMAX);
    int txBase = (int)(gid.x * (uint)SH_OW) - SH_RMAX;
    int tyBase = (int)(gid.y * (uint)GS) - SH_RMAX;
    SharpLoadTile(gtid, gid, txBase, tyBase, yPitch);
    if (gtid.x == 0u && gtid.y == 0u)
    {
        for (int k = 0; k <= 2 * SH_RMAX; k++) ShW[k] = SmTap(k, R);
    }
    GroupMemoryBarrierWithGroupSync();

    // Owned row lr lives at tile row lr + SH_RMAX (the halo is the maximum radius, not the runtime
    // one), so its window is tile rows [lr + SH_RMAX - R, lr + SH_RMAX + R] - inside the loaded tile
    // for every lr and R, which is why the vertical pass costs no extra halo.
    for (uint vi = gtid.y * (uint)GS + gtid.x; vi < SH_VH * SH_VW; vi += (uint)(GS * GS))
    {
        int c = (int)(vi % (uint)SH_VW), lr = (int)(vi / (uint)SH_VW);
        float acc = 0.0, vmin = 255.0, vmax = 0.0;
        for (int k = 0; k <= 2 * R; k++)
        {
            float s = ShY[(lr + SH_RMAX - R + k) * SH_TW + c];
            acc += ShW[k] * s;
            vmin = min(vmin, s);
            vmax = max(vmax, s);
        }
        ShV[lr * SH_VW + c] = acc;
        ShMin[lr * SH_VW + c] = vmin;
        ShMax[lr * SH_VW + c] = vmax;
    }
    GroupMemoryBarrierWithGroupSync();

    if (bx >= cols || by >= OutH) return;

    float invDiv = 1.0 / (float)(1u << (4u * (uint)R));
    int lx = (int)bx * 4 - txBase;
    int lr = (int)by - (int)(gid.y * (uint)GS);

    uint packedY = 0u;
    for (uint i = 0u; i < 4u; i++)
    {
        int tx = lx + (int)i;
        float c = ShY[(lr + SH_RMAX) * SH_TW + tx];
        float acc = 0.0, vmin = 255.0, vmax = 0.0;
        for (int k = 0; k <= 2 * R; k++)
        {
            int vc = tx - R + k;
            acc += ShW[k] * ShV[lr * SH_VW + vc];
            vmin = min(vmin, ShMin[lr * SH_VW + vc]);
            vmax = max(vmax, ShMax[lr * SH_VW + vc]);
        }
        packedY |= ((uint)clamp(SharpInject(c, acc * invDiv, vmin, vmax) + 0.5, 0.0, 255.0)) << (8u * i);
    }
    Nv12Sharp[(yPitch >> 2u) * by + bx] = packedY;
}

// ---------------------------------------------------------------- stage 5: chroma denoise + gain

// Row 4 of Pascal's triangle: 1 4 6 4 1, 2D sum 256 = 2^8.
static float CsTap(int o)
{
    float w = 1.0;
    if (o == 2) w = 6.0;
    else if (o == 1 || o == 3) w = 4.0;
    return w;
}

// Luma-similarity falloff width of the guided passes, in luma codes. BuildC007 made it a host parameter
// (chroma_sigma) because the shipped 20 leaves the front's flat areas with 1.4x Android's chroma high
// frequency, and a saturation gain cannot close that: it scales noise and signal alike. It still arrives
// as a reciprocal the shader multiplies by, and the default stays the literal both sides used before, so
// a preset that never mentions it reproduces the earlier plane byte for byte.
static float CsInvSigma()
{
    return ChromaInvSigma > 0.0 ? ChromaInvSigma : (1.0 / 20.0);
}

// The guide is the luma plane that actually ships. SharpYCS writes into the separate Nv12Sharp scratch,
// so reading Nv12 here would hand the filter the pre-sharpen Y while the CPU reference - which sharpens
// in place before stage 5 - would use the post-sharpen one. EmitNv12Readback picks the plane the same way.
static float CsGuideLuma(uint lx, uint ly, uint wordRow)
{
    uint i = wordRow * ly + (lx >> 2u);
    uint sh = 8u * (lx & 3u);
    if ((Flags & ISP_FLAG_SHARP) != 0u)
        return (float)((Nv12Sharp[i] >> sh) & 255u);
    return (float)((Nv12[i] >> sh) & 255u);
}

// One 5x5 binomial of the loaded UV tile, as the exact float mean: integer samples x integer weights
// are exact in fp32 (peak 255*256) and 1/256 is a power of two, so this is the same value the gain-off
// path below quantises. dy outer / dx inner in ascending order is the order the CPU mirror reproduces.
static float CsBinomAt(int ch, int tx, int ty)
{
    int off = ch * CS_TW * CS_TH + ty * CS_TW + tx;
    float acc = 0.0;
    for (int dy = -CS_R; dy <= CS_R; dy++)
        for (int dx = -CS_R; dx <= CS_R; dx++)
            acc += CsTap(dy + CS_R) * CsTap(dx + CS_R) * CsT[off + dy * CS_TW + dx];
    return acc * (1.0 / 256.0);
}

// Load one guided pass's window: the input field's chroma tile and the luma guide under it. The clamp is
// at tile load, which for a single pass over a full-frame field is the same coordinate the per-tap clamp
// in GpuIspRefChromaSmooth would name - the frame's own values are what gets replicated into the halo,
// not values some earlier pass computed from a replicated halo. That distinction is the whole reason the
// stage is three dispatches: with the cascade inside one kernel and a halo wide enough for both guided
// passes, the intermediate planes near the border were built from replicated chroma, and every one of the
// 1352+1768 sites where that shader disagreed with its mirror sat within six sites of a frame edge.
static void CsGuidedTiles(uint3 gtid, int txBase, int tyBase, RWStructuredBuffer<float> field)
{
    uint pitch = (OutW + 255u) & ~255u;
    uint wordRow = pitch >> 2u;
    for (uint li = gtid.y * (uint)GS + gtid.x; li < 2u * CS_GW * CS_GH; li += (uint)(GS * GS))
    {
        uint ch = li / (CS_GW * CS_GH);
        uint p = li % (CS_GW * CS_GH);
        uint tx = p % (uint)CS_GW, ty = p / (uint)CS_GW;
        int cx = clamp(txBase + (int)tx, 0, (int)(OutW / 2u) - 1);
        int cyy = clamp(tyBase + (int)ty, 0, (int)(OutH / 2u) - 1);
        CsF[(int)ch * CS_GW * CS_GH + (int)ty * CS_GW + (int)tx] =
            field[(uint)cyy * pitch + (uint)cx * 2u + ch];
        // One guide sample per chroma site: the top-left luma byte of its 2x2 block. Averaging all four
        // would be a second arithmetic order for the CPU mirror to track, and at chroma resolution the
        // block is one surface either way.
        if (ch == 0u)
            CsG[(int)ty * CS_GW + (int)tx] = CsGuideLuma(2u * (uint)cx, 2u * (uint)cyy, wordRow);
    }
    GroupMemoryBarrierWithGroupSync();
}

// One 5x5 luma-guided chroma average of the loaded window: binomial weights times a linear falloff in the
// luma distance to the centre, so a flat neighbourhood gets the full blur and a contour keeps its own
// colour. den is at least the centre weight (36), so the single explicit division - one, as in ColorDownCS,
// never a reciprocal-multiply - cannot see a zero. dy outer / dx inner in ascending order is the order the
// CPU mirror reproduces.
static float CsGuidedAt(int ch, int tx, int ty)
{
    int src = ch * CS_GW * CS_GH + ty * CS_GW + tx;
    int ys = ty * CS_GW + tx;
    float lumC = CsG[ys];
    const float invSigma = CsInvSigma();   // uniform: read once, not per tap
    float num = 0.0, den = 0.0;
    for (int dy = -CS_G; dy <= CS_G; dy++)
        for (int dx = -CS_G; dx <= CS_G; dx++)
        {
            float w = CsTap(dy + CS_G) * CsTap(dx + CS_G) *
                      max(0.0, 1.0 - abs(CsG[ys + dy * CS_GW + dx] - lumC) * invSigma);
            num += w * CsF[src + dy * CS_GW + dx];
            den += w;
        }
    return num / den;
}

// Both U and V at a chroma site use the same luma weights and denominator.
// Compute those once while retaining each channel's original ascending tap
// order and full fp32 intermediate plane. Host grid and group shape unchanged.
static float2 CsGuidedPairAt(int tx,int ty)
{
    int src=ty*CS_GW+tx;
    float lumC=CsG[src];
    const float invSigma=CsInvSigma();
    float2 num=float2(0.0,0.0);float den=0.0;
    for(int dy=-CS_G;dy<=CS_G;dy++)
        for(int dx=-CS_G;dx<=CS_G;dx++){
            int offset=src+dy*CS_GW+dx;
            float w=CsTap(dy+CS_G)*CsTap(dx+CS_G)*
                    max(0.0,1.0-abs(CsG[offset]-lumC)*invSigma);
            num+=w*float2(CsF[offset],CsF[CS_GW*CS_GH+offset]);
            den+=w;
        }
    return num/den;
}

// The re-centring offset from one channel's integer sums: the mean rounded deviation of the frame's
// near-neutral chroma sites, quantised to 1/CS_CK_SCALE code so the shader's fp32 and the CPU mirror's
// agree on a single bit pattern (128.0 + offset is then exact in fp32, so the subtraction downstream is
// one rounding on both sides). 0.0 for an empty population leaves the plane exactly as Build010 shipped
// it. precise keeps the IEEE divide: fxc may otherwise lower '/' to a reciprocal multiply, and gate 2
// compares this lane against the mirror byte for byte.
static float CsRecentreOffset(int sum, int count)
{
    if (count <= 0) return 0.0;
    precise float mean = (float)sum / (float)count;
    return (float)(int)round(mean * (float)CS_CK_SCALE) / (float)CS_CK_SCALE;
}

// ---------------------------------------------------------------- stage 5: chroma denoise + gain
//
// Three dispatches, one per job, in the order the mirror applies them:
//   ChromaSmoothCS   5x5 binomial            -> Nv12Uv (no cascade) or IspChromaA
//   ChromaGuide1CS   5x5 luma-guided x1      -> IspChromaB, plus the re-centring statistic when armed
//   ChromaGuide2CS   5x5 luma-guided x2 + UV gain (about the re-centred neutral point) -> Nv12Uv
// The cascade (the two guided dispatches) runs when ChromaGain > 0 or ISP_FLAG_CHROMA_RECENTRE is set:
// the second case is re-centring with a gain of 1.00, which is what the front's preset asks for.
// Split rather than one kernel with a haloed cascade tile, for two measured reasons. The binomial is a
// shipped stage (Build009, both cameras): putting the cascade's groupshared in the same entry point
// reserved it statically for the flag-only path too, and that path measured 17.32 ms against the 2.99 ms
// it shipped with, purely from occupancy. And a cascade tile cannot be bit-identical to the mirror at the
// frame border, for the reason recorded on CsGuidedTiles. Each pass therefore writes a full-frame field
// that the next pass loads with its own halo-2 tile - which is also why they need separate dispatches: a
// group's window reaches chroma sites a neighbouring group is computing in the same dispatch.
//
// The finished NV12 UV plane only, Y untouched. Stage 5 has three jobs, the first two measured before it
// existed:
//  * the colour lattice. The chroma samples come from a box downscale whose footprint is not uniform
//    when the raw-to-output ratio r is not an integer, so the lattice lands at frac(r) aliased into the
//    output grid and prints a visible beat on flat coloured areas (measured at four geometries; absent
//    in Android's files and in the 1:1 live path). The 5x5 binomial spans the beat and kills most of it.
//  * the colour level. The rear's chroma sits at a flat ~0.6x of Android's at every saturation level,
//    which is a missing gain and not missing detail - and it has to land in finished-code space, because
//    the same boost through the CCM clips against the saturate() that sits before the tone curve and
//    takes 1.29 dB of luma with it.
//  * the neutral point. Both cameras put their chroma grey 1-3 codes above code 128 (rear U +0.96 /
//    V +2.55, front U +1.61 / V +1.76 on sites both renderings call neutral, flat in luma, so a DC term
//    and not a gain error), and Build010's gain multiplies it. Subtracting the measured offset BEFORE the
//    multiplication is the whole point: an offset that survives the gain is the bug, not the fix. This is
//    the CCK block of a hardware ISP (gain and offset per channel); GPUISP had the gain and no offset.
// A gain applied to a noisy plane multiplies the noise as hard as the signal, so it comes last, after
// two guided passes have taken the noise out without running colour across the contours - the isotropic
// cascade that reaches a still lower MAE has a bleed figure no better than the unfiltered frame, which
// is the "мутные" failure the front already drew.
// Strictly after Nv12CS/SharpYCS, so it sees the shipped codes and cannot change luma at all. Each pass
// writes a scratch of its own, like SharpYCS: a site's window reaches chroma columns its neighbouring
// group produces in the same dispatch.
// GpuIspRef.cpp's GpuIspRefChromaSmooth mirrors these loops pass for pass, tap for tap.
[numthreads(GS, GS, 1)]
void ChromaSmoothCS(uint3 tid : SV_DispatchThreadID, uint3 gtid : SV_GroupThreadID, uint3 gid : SV_GroupID)
{
    uint wx = tid.x, cy = tid.y;
    uint cols = OutW / 4u;
    uint rows = OutH / 2u;
    uint pitch = (OutW + 255u) & ~255u;
    uint wordRow = pitch >> 2u;
    uint planeBase = (((OutW + 255u) & ~255u) * OutH) >> 2u;   // the UV plane starts after the Y plane

    // ChromaGain <= 0 with the re-centre bit clear (also the zero-initialised struct) is the Build009
    // shape: the field store and the two guided dispatches never happen, so a preset that sets only the
    // smooth flag reproduces the delivered UV plane byte for byte - and the host does not dispatch the
    // guided kernels at all.
    const bool recentre = (Flags & ISP_FLAG_CHROMA_RECENTRE) != 0u;
    const bool cascade = ChromaGain > 0.0 || recentre;

    // The statistic accumulates by atomic add, so it has to start every frame from zero. This dispatch
    // runs strictly before ChromaGuide1CS with a UAV barrier in between, so one thread zeroes it here -
    // cheaper than a queue-level clear, which serialises the pipeline once per frame.
    if (cascade && recentre && gid.x == 0u && gid.y == 0u && gtid.x == 0u && gtid.y == 0u)
        for (uint s = 0u; s < CS_STAT_SLOTS; s++) IspChromaStat[s] = 0;

    // One group = GS UV words (2*GS chroma columns) x GS chroma rows.
    int txBase = (int)(gid.x * (uint)GS * 2u) - CS_R;
    int tyBase = (int)(gid.y * (uint)GS) - CS_R;
    for (uint li = gtid.y * (uint)GS + gtid.x; li < 2u * CS_TW * CS_TH; li += (uint)(GS * GS))
    {
        uint ch = li / (CS_TW * CS_TH);
        uint p = li % (CS_TW * CS_TH);
        uint tx = p % (uint)CS_TW, ty = p / (uint)CS_TW;
        int cx = clamp(txBase + (int)tx, 0, (int)(OutW / 2u) - 1);
        int cyy = clamp(tyBase + (int)ty, 0, (int)rows - 1);
        uint w = Nv12[planeBase + wordRow * (uint)cyy + (uint)cx / 2u];
        // byte 2*(cx&1) + ch inside the word: the word packs U0,V0,U1,V1.
        CsT[(int)ch * CS_TW * CS_TH + (int)ty * CS_TW + (int)tx] =
            (float)((w >> (8u * (2u * ((uint)cx & 1u) + ch))) & 255u);
    }
    GroupMemoryBarrierWithGroupSync();

    if (wx >= cols || cy >= rows) return;

    int lx = (int)(wx * 2u) - txBase;
    int ly = (int)cy - tyBase;

    // The clamp at load replicates the frame's edge column into the halo, which is what the mirror's
    // per-tap chroma clamp does, so this single pass is bit-identical at the border too.
    uint packedUV = 0u;
    for (uint k = 0u; k < 4u; k++)
    {
        float v = CsBinomAt((int)(k & 1u), lx + (int)(k >> 1u), ly);
        // Two explicit branches, not a select: with a select fxc evaluates both paths whatever the gain
        // says, which is the 60 ms note above Nv12CS's own denoise branch.
        if (cascade) IspChromaA[pitch * cy + wx * 4u + k] = v;
        else         packedUV |= ((uint)(v + 0.5)) << (8u * k);
    }
    if (!cascade) Nv12Uv[wordRow * cy + wx] = packedUV;
}

// The field keeps the binomial's exact float mean, so the guided passes filter the same numbers the
// mirror's intermediate planes hold instead of a re-quantised copy of them. Both kernels own GS UV words
// x GS chroma rows like ChromaSmoothCS, so the host dispatches all three on the same grid.
// When re-centring is armed this pass also measures the frame's own neutral point on the way out: the
// values it stores are the population the statistic is taken over, both channels of a site are in the
// same thread's registers, and the sums are exact integers, so no fourth dispatch is needed.
[numthreads(GS, GS, 1)]
void ChromaGuide1CS(uint3 tid : SV_DispatchThreadID, uint3 gtid : SV_GroupThreadID, uint3 gid : SV_GroupID)
{
    uint wx = tid.x, cy = tid.y;
    uint pitch = (OutW + 255u) & ~255u;
    int txBase = (int)(gid.x * (uint)GS * 2u) - CS_G;
    int tyBase = (int)(gid.y * (uint)GS) - CS_G;
    const bool recentre = (Flags & ISP_FLAG_CHROMA_RECENTRE) != 0u;
    if (recentre && gtid.x == 0u && gtid.y == 0u)
    {
        CsStat[0] = 0; CsStat[1] = 0; CsStat[2] = 0;
    }
    CsGuidedTiles(gtid, txBase, tyBase, IspChromaA);   // ends with the group barrier that publishes them

    const bool inside = (wx < OutW / 4u && cy < OutH / 2u);
    float u0 = 0.0, v0 = 0.0, u1 = 0.0, v1 = 0.0;
    if (inside)
    {
        int lx = (int)(wx * 2u) - txBase;
        int ly = (int)cy - tyBase;
        // The same four sites in the same order as the loop Build010 shipped, so the field is unchanged;
        // held in registers because the statistic needs the pair per chroma column.
        float2 pair0=CsGuidedPairAt(lx+0,ly),pair1=CsGuidedPairAt(lx+1,ly);
        u0=pair0.x;v0=pair0.y;u1=pair1.x;v1=pair1.y;
        IspChromaB[pitch * cy + wx * 4u + 0u] = u0;
        IspChromaB[pitch * cy + wx * 4u + 1u] = v0;
        IspChromaB[pitch * cy + wx * 4u + 2u] = u1;
        IspChromaB[pitch * cy + wx * 4u + 3u] = v1;
    }
    // With the bit clear this is exactly Build010's kernel, and every thread leaves before the barrier
    // below, so the shipped path pays nothing for the lane. The branch is on a uniform, so the barrier
    // stays group-uniform.
    if (!recentre) return;

    // floor(x + 0.5) rather than (int)(x + 0.5): the deviation is signed and a cast truncates toward
    // zero, which would round -3.6 up to -3 while the mirror's floor gives -4.
    int du0 = 0, dv0 = 0, du1 = 0, dv1 = 0, w0 = 0, w1 = 0;
    if (inside)
    {
        du0 = (int)floor(u0 - 128.0 + 0.5);
        dv0 = (int)floor(v0 - 128.0 + 0.5);
        du1 = (int)floor(u1 - 128.0 + 0.5);
        dv1 = (int)floor(v1 - 128.0 + 0.5);
        // The population is sites, not samples: a site counts once when both its channels sit inside the
        // window, so its U and V sums share the same denominator. Threads outside the frame are excluded
        // by `inside`, not by the window - their zeroed deviations would otherwise pass it.
        w0 = (abs(du0) + abs(dv0) <= CS_CK) ? 1 : 0;
        w1 = (abs(du1) + abs(dv1) <= CS_CK) ? 1 : 0;
    }
    // The sums run over exactly the population the denominator counts. Summing every site and dividing by
    // the near-neutral count was the first version's bug, and it is a big one: the near-neutral set is
    // ~83 % of the frame here, so the offset came out ~1.2x too large and the gain multiplied the error.
    InterlockedAdd(CsStat[0], du0 * w0 + du1 * w1);
    InterlockedAdd(CsStat[1], dv0 * w0 + dv1 * w1);
    InterlockedAdd(CsStat[2], w0 + w1);
    GroupMemoryBarrierWithGroupSync();
    if (gtid.x == 0u && gtid.y == 0u)
    {
        InterlockedAdd(IspChromaStat[0], CsStat[0]);
        InterlockedAdd(IspChromaStat[1], CsStat[1]);
        InterlockedAdd(IspChromaStat[2], CsStat[2]);
    }
}

// The last pass writes the shipped plane, so its store is the same pack the binomial-only path uses.
[numthreads(GS, GS, 1)]
void ChromaGuide2CS(uint3 tid : SV_DispatchThreadID, uint3 gtid : SV_GroupThreadID, uint3 gid : SV_GroupID)
{
    uint wx = tid.x, cy = tid.y;
    uint pitch = (OutW + 255u) & ~255u;
    uint wordRow = pitch >> 2u;
    int txBase = (int)(gid.x * (uint)GS * 2u) - CS_G;
    int tyBase = (int)(gid.y * (uint)GS) - CS_G;
    const bool recentre = (Flags & ISP_FLAG_CHROMA_RECENTRE) != 0u;
    CsGuidedTiles(gtid, txBase, tyBase, IspChromaB);
    if (wx >= OutW / 4u || cy >= OutH / 2u) return;

    int lx = (int)(wx * 2u) - txBase;
    int ly = (int)cy - tyBase;

    // The statistic was summed by the dispatch before this one, across the whole frame. Every group reads
    // the same three integers, so the offsets are a per-frame constant: 128.0 + offset stays exact.
    float cenU = 128.0, cenV = 128.0;
    if (recentre)
    {
        const int sn = IspChromaStat[2];
        cenU = 128.0 + CsRecentreOffset(IspChromaStat[0], sn);
        cenV = 128.0 + CsRecentreOffset(IspChromaStat[1], sn);
    }
    // ChromaGain <= 0 with the re-centre bit on asks for the offsets alone, which is a gain of 1.00. This
    // pass never runs when both the gain and the bit are clear, so the shipped path is untouched.
    const float gain = ChromaGain > 0.0 ? ChromaGain : 1.0;

    uint packedUV = 0u;
    float2 pair0=CsGuidedPairAt(lx+0,ly),pair1=CsGuidedPairAt(lx+1,ly);
    for (uint k = 0u; k < 4u; k++)
    {
        float2 pair=k<2u?pair0:pair1;
        float v=(k&1u)==0u?pair.x:pair.y;
        // The grey target: a static per-channel bias, added AFTER the re-centring, because the lane pins the
        // frame's own grey on 128 whatever upstream does - a white-balance change made in awb/tone/lsc is
        // re-centred straight back away. 0.0 leaves the arithmetic bit-identical to Build012's. The front
        // ships U only (its own V gap measured -0.010); the rear's measured V gap is +1.54, so both rows live.
        const float bias = ((k & 1u) == 0u) ? ChromaBiasU : ChromaBiasV;
        // Gain about the neutral point in finished-code space, held to the limited-range chroma band so a
        // boosted strong colour cannot walk off the plane. The binomial-only path keeps Build009's
        // unclamped rounding, which is why the clamp lives here and not on that store.
        if (recentre) v = clamp(128.0 + bias + gain * (v - ((k & 1u) == 0u ? cenU : cenV)), 16.0, 240.0);
        else          v = clamp(128.0 + bias + ChromaGain * (v - 128.0), 16.0, 240.0);
        packedUV |= ((uint)(v + 0.5)) << (8u * k);
    }
    Nv12Uv[wordRow * cy + wx] = packedUV;
}



