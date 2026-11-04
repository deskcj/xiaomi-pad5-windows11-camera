// GpuIspRef.cpp - CPU reference for the GPUISP2 pipeline.
// This is a line-by-line mirror of gpuisp/shaders/IspCs.hlsl: same operation order, same float
// arithmetic, same integer spans. The bench compares GPU output against it to separate wiring
// bugs (differences here) from tuning bugs (differences against Android).
#include "GpuIspRef.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#define GS        8
#define MAXTW     24
#define TONE_LUT  256
#define DN_R      2

#define ISP_FLAG_LSC   0x0001u
#define ISP_FLAG_AWB   0x0002u
#define ISP_FLAG_CCM   0x0004u
#define ISP_FLAG_TONE  0x0008u
#define ISP_FLAG_GAMMA 0x0010u
#define ISP_FLAG_DENOISE 0x0040u

enum { CH_R = 0, CH_GR = 1, CH_GB = 2, CH_B = 3 };

namespace
{

struct Ctx
{
    const GpuIspParams* p;
    const float* lsc;          // cols*rows float4 in (R,Gr,Gb,B)
    const GpuIspTone* tone;
    const UINT16* raw;         // tightly packed, rawW per row
};

static inline uint32_t ChanAt(const Ctx& c, uint32_t x, uint32_t y)
{
    uint32_t yy = (y + ((c.p->cfaOff >> 1) & 1u)) & 1u;
    uint32_t xx = (x + (c.p->cfaOff & 1u)) & 1u;
    if (yy == 0u) return (xx == 0u) ? CH_B : CH_GB;
    return (xx == 0u) ? CH_GR : CH_R;
}

static inline float LscGainFor(uint32_t ch, const float* g)
{
    if (ch == CH_R) return g[0];
    if (ch == CH_GR) return g[1];
    if (ch == CH_GB) return g[2];
    return g[3];
}

static float LscBilinear(const Ctx& c, uint32_t qx, uint32_t qy)
{
    const uint32_t cols = c.p->lscCols, rows = c.p->lscRows;
    float gx = ((float)qx + 0.5f) / (float)c.p->rawW * (float)cols - 0.5f;
    float gy = ((float)qy + 0.5f) / (float)c.p->rawH * (float)rows - 0.5f;
    gx = std::min(std::max(gx, 0.0f), (float)cols - 1.0f);
    gy = std::min(std::max(gy, 0.0f), (float)rows - 1.0f);
    int x0 = (int)std::floor(gx), y0 = (int)std::floor(gy);
    int x1 = std::min(x0 + 1, (int)cols - 1), y1 = std::min(y0 + 1, (int)rows - 1);
    float fx = gx - (float)x0, fy = gy - (float)y0;
    const float* a = c.lsc + ((size_t)y0 * cols + x0) * 4;
    const float* b = c.lsc + ((size_t)y0 * cols + x1) * 4;
    const float* cc = c.lsc + ((size_t)y1 * cols + x0) * 4;
    const float* d = c.lsc + ((size_t)y1 * cols + x1) * 4;
    float out[4];
    for (int k = 0; k < 4; k++)
    {
        float top = a[k] + (b[k] - a[k]) * fx;
        float bot = cc[k] + (d[k] - cc[k]) * fx;
        out[k] = top + (bot - top) * fy;
    }
    return LscGainFor(ChanAt(c, qx, qy), out);
}

static inline float saturateF(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
static inline float clampF(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

static float RawNorm(const Ctx& c, int px, int py)
{
    int cx = px < 0 ? -px : px;
    int cy = py < 0 ? -py : py;
    if(cx >= (int)c.p->rawW)cx=2*((int)c.p->rawW-1)-cx;
    if(cy >= (int)c.p->rawH)cy=2*((int)c.p->rawH-1)-cy;
    uint32_t q = (uint32_t)cy * c.p->rawW + (uint32_t)cx;
    float v = (float)c.raw[q];
    float g = 1.0f;
    if ((c.p->flags & ISP_FLAG_LSC) != 0u && c.p->lscCols > 1u && c.p->lscRows > 1u)
        g = LscBilinear(c, (uint32_t)cx, (uint32_t)cy);
    float range = std::max((float)c.p->whiteLevel - (float)c.p->blackLevel, 1.0f);
    return std::max(((v - (float)c.p->blackLevel) * g * c.p->exGain) / range,0.0f);
}

static float ToneAt(const Ctx& c, int ch, float v)
{
    float f = saturateF(v) * (float)(TONE_LUT - 1);
    int i0 = (int)f;
    int i1 = std::min(i0 + 1, TONE_LUT - 1);
    float fr = f - (float)i0;
    float a = c.tone->lut[ch * TONE_LUT + i0];
    float b = c.tone->lut[ch * TONE_LUT + i1];
    return a + (b - a) * fr;
}

static void Demosaic(const Ctx& c, int px, int py, float out[3])
{
    uint32_t ch = ChanAt(c, (uint32_t)px, (uint32_t)py);
    float v = RawNorm(c, px, py);
    float h1=RawNorm(c,px-1,py)+RawNorm(c,px+1,py);
    float v1=RawNorm(c,px,py-1)+RawNorm(c,px,py+1);
    float h2=RawNorm(c,px-2,py)+RawNorm(c,px+2,py);
    float v2=RawNorm(c,px,py-2)+RawNorm(c,px,py+2);
    float diag=RawNorm(c,px-1,py-1)+RawNorm(c,px+1,py-1)+RawNorm(c,px-1,py+1)+RawNorm(c,px+1,py+1);
    if (ch == CH_GR || ch == CH_GB)
    {
        bool horizIsR = (ChanAt(c, (uint32_t)px + 1u, (uint32_t)py) == CH_R);
        float horizontal=(5.0f*v+4.0f*h1-h2-diag+0.5f*v2)*0.125f;
        float vertical=(5.0f*v+4.0f*v1-v2-diag+0.5f*h2)*0.125f;
        out[0] = std::max(horizIsR?horizontal:vertical,0.0f);
        out[1] = v;
        out[2] = std::max(horizIsR?vertical:horizontal,0.0f);
        return;
    }

    float G=std::max((4.0f*v+2.0f*(h1+v1)-(h2+v2))*0.125f,0.0f);
    float other=std::max((6.0f*v+2.0f*diag-1.5f*(h2+v2))*0.125f,0.0f);
    if (ch == CH_R) { out[0] = v; out[1] = G; out[2] = other; }
    else            { out[0] = other; out[1] = G; out[2] = v; }
}

}   // namespace

// Mirror of DnTap() in IspCs.hlsl: separable binomial row 4 (1 4 6 4 1).
static inline float DnTap(int o)
{
    if (o == 2) return 6.0f;
    if (o == 1 || o == 3) return 4.0f;
    return 1.0f;
}

// The GPU carries every stage boundary through an RGBA16F texture, so each channel is rounded to
// the nearest float16 (round-to-nearest-even) before the next stage reads it. The reference must
// reproduce that quantization or the gate compares GPU-half input against CPU-float input.
// Verified bit-for-bit against numpy's float32->float16->float32 round trip on 326395 values,
// including exhaustive mantissa/tie sweeps and the whole subnormal band.
static inline uint16_t F32ToF16Bits(float f)
{
    uint32_t x;
    memcpy(&x, &f, sizeof(x));
    uint32_t sign = (x >> 16) & 0x8000u;
    int32_t e = (int32_t)((x >> 23) & 0xFFu) - 127;
    uint32_t mant = x & 0x7FFFFFu;

    if (e == 128) return (uint16_t)(sign | 0x7C00u | (mant ? 0x200u : 0u));   // inf / NaN
    if (e > 15)   return (uint16_t)(sign | 0x7C00u);                          // overflow -> inf
    if (e < -14)                                                  // half subnormal or zero
    {
        if (e < -25) return (uint16_t)sign;
        uint32_t S = 0x800000u | mant;
        int k = (int)(-e - 1);
        uint32_t keep = S >> k;
        uint32_t rem = S & ((1u << k) - 1u);
        uint32_t halfb = 1u << (k - 1);
        if (rem > halfb || (rem == halfb && (keep & 1u))) keep++;
        return (uint16_t)(sign | keep);
    }
    uint32_t keep = mant >> 13;
    uint32_t rem = mant & 0x1FFFu;
    if (rem > 0x1000u || (rem == 0x1000u && (keep & 1u)))
    {
        keep++;
        if (keep > 0x3FFu) { keep = 0u; e++; if (e > 15) return (uint16_t)(sign | 0x7C00u); }
    }
    return (uint16_t)(sign | ((uint32_t)(e + 15) << 10) | keep);
}

static inline float F16BitsToF32(uint16_t h)
{
    uint32_t sign = ((uint32_t)h & 0x8000u) << 16;
    uint32_t e = ((uint32_t)h >> 10) & 0x1Fu;
    uint32_t m = (uint32_t)h & 0x3FFu;
    uint32_t bits;
    if (e == 0u)
    {
        if (m == 0u) bits = sign;
        else
        {
            int shift = 0;
            while (!(m & 0x400u)) { m <<= 1u; shift++; }
            m &= 0x3FFu;
            bits = sign | ((uint32_t)(113 - shift) << 23) | (m << 13);
        }
    }
    else if (e == 31u) bits = sign | 0x7F800000u | (m << 13);
    else bits = sign | ((e + 127u - 15u) << 23) | (m << 13);
    float out;
    memcpy(&out, &bits, sizeof(out));
    return out;
}

static inline float HalfRoundTrip(float f) { return F16BitsToF32(F32ToF16Bits(f)); }

bool GpuIspRefFrontEnd(const GpuIspParams& p, const float* lscCells, const UINT16* raw,
                       std::vector<float>& rgbFull)
{
    Ctx c{ &p, lscCells, nullptr, raw };
    rgbFull.assign((size_t)p.rawW * p.rawH * 4, 1.0f);
    for (uint32_t y = 0; y < p.rawH; y++)
        for (uint32_t x = 0; x < p.rawW; x++)
        {
            float rgb[3];
            Demosaic(c, (int)x, (int)y, rgb);
            float* o = &rgbFull[((size_t)y * p.rawW + x) * 4];
            // the store into RgbFullUav (RGBA16F) quantizes; mirror it, since ColorDownCS
            // and the 8-bit Y step both read that half-precision value back.
            o[0] = HalfRoundTrip(rgb[0]); o[1] = HalfRoundTrip(rgb[1]);
            o[2] = HalfRoundTrip(rgb[2]); o[3] = 1.0f;
        }
    return true;
}

bool GpuIspRefColorDown(const GpuIspParams& p, const float* lscCells, const GpuIspTone& tone,
                        const std::vector<float>& rgbFull, std::vector<float>& rgbOut)
{
    Ctx c{ &p, lscCells, &tone, nullptr };
    rgbOut.assign((size_t)p.outW * p.outH * 4, 0.0f);

    for (uint32_t oy = 0; oy < p.outH; oy++)
    {
        for (uint32_t ox = 0; ox < p.outW; ox++)
        {
            // Exact area-weighted box - must match ColorDownCS tap for tap, including the loop order.
            // Numerator units: raw pixel x owns [x*outW, (x+1)*outW), output pixel ox owns
            // [ox*cropW, (ox+1)*cropW). Integer overlaps are exact in fp32, so the only rounding is
            // the weighted sum and the single division below.
            uint32_t xn0 = ox * p.cropW;
            uint32_t xn1 = (ox + 1u) * p.cropW;
            uint32_t yn0 = oy * p.cropH;
            uint32_t yn1 = (oy + 1u) * p.cropH;
            int px0 = (int)(xn0 / p.outW);
            int px1 = (int)((xn1 + p.outW - 1u) / p.outW);
            int py0 = (int)(yn0 / p.outH);
            int py1 = (int)((yn1 + p.outH - 1u) / p.outH);
            if (px1 <= px0) px1 = px0 + 1;
            if (py1 <= py0) py1 = py0 + 1;

            float accR = 0.0f, accG = 0.0f, accB = 0.0f;
            int wSum = 0;
            for (int y = py0; y < py1; y++)
            {
                int ly = std::max((int)yn0, y * (int)p.outH);
                int hy = std::min((int)yn1, (y + 1) * (int)p.outH);
                int ovy = hy - ly;
                int ry = std::min((int)p.cropY + y, (int)p.rawH - 1);
                for (int x = px0; x < px1; x++)
                {
                    int lx = std::max((int)xn0, x * (int)p.outW);
                    int hx = std::min((int)xn1, (x + 1) * (int)p.outW);
                    float w = (float)((hx - lx) * ovy);
                    int rx = std::min((int)p.cropX + x, (int)p.rawW - 1);
                    const float* s = &rgbFull[((size_t)ry * p.rawW + rx) * 4];
                    accR += w * s[0];
                    accG += w * s[1];
                    accB += w * s[2];
                    wSum += (hx - lx) * ovy;
                }
            }
            if (wSum == 0) wSum = 1;
            float r = accR / (float)wSum, g = accG / (float)wSum, b = accB / (float)wSum;

            if ((p.flags & ISP_FLAG_AWB) != 0u) { r *= p.awb[0]; g *= p.awb[1]; b *= p.awb[3]; }
            if ((p.flags & ISP_FLAG_CCM) != 0u)
            {
                float rr = GpuIspCcm(p, 0) * r + GpuIspCcm(p, 1) * g + GpuIspCcm(p, 2) * b;
                float gg = GpuIspCcm(p, 3) * r + GpuIspCcm(p, 4) * g + GpuIspCcm(p, 5) * b;
                float bb = GpuIspCcm(p, 6) * r + GpuIspCcm(p, 7) * g + GpuIspCcm(p, 8) * b;
                r = rr; g = gg; b = bb;
            }
            r = saturateF(r); g = saturateF(g); b = saturateF(b);

            if ((p.flags & ISP_FLAG_TONE) != 0u)
            {
                r = ToneAt(c, 0, r);
                g = ToneAt(c, 1, g);
                b = ToneAt(c, 2, b);
            }
            if ((p.flags & ISP_FLAG_GAMMA) != 0u)
            {
                float inv = 1.0f / std::max(p.gamma, 1.0f);
                // IspCs.hlsl: pow(max(c, 0.0), 1.0 / max(Gamma, 1.0)) - the negative clamp
                // matters because a CCM row can drive a channel below zero before saturate.
                r = std::pow(std::max(r, 0.0f), inv);
                g = std::pow(std::max(g, 0.0f), inv);
                b = std::pow(std::max(b, 0.0f), inv);
            }
            float* o = &rgbOut[((size_t)oy * p.outW + ox) * 4];
            // same reason as above: Nv12CS reads RgbOutSrv, an RGBA16F texture.
            o[0] = HalfRoundTrip(r); o[1] = HalfRoundTrip(g); o[2] = HalfRoundTrip(b); o[3] = 1.0f;
        }
    }
    return true;
}

// Mirror of DnFilter() in IspCs.hlsl: 5x5 bilateral on post-tone RGB, border pixels replicated,
// same tap order and the same accumulation order so the GPU/CPU gate stays at <=1 code.
static void RefDnSample(const GpuIspParams& p, const std::vector<float>& rgb, int cx, int cy, float o[3])
{
    const float* s = &rgb[((size_t)cy * p.outW + cx) * 4];
    const float lumC = GpuIspYuvM(p, 0) * s[0] + GpuIspYuvM(p, 1) * s[1] + GpuIspYuvM(p, 2) * s[2];
    // Mirror of DnFilter's select: a zero slope keeps the shipped uniform reciprocal, so every
    // preset that has not opted in stays byte-identical. With a slope the width is a line in the
    // same gamma luma the shader keys on -- one mad, no division, so both sides agree bit for bit.
    const float invRange = (p.nrRangeB == 0.0f)
                               ? (1.0f / std::max(p.denoise, 1e-6f))
                               : (p.nrRangeA + p.nrRangeB * lumC);
    float accR = 0.0f, accG = 0.0f, accB = 0.0f, accW = 0.0f;
    for (int dy = -DN_R; dy <= DN_R; dy++)
    {
        for (int dx = -DN_R; dx <= DN_R; dx++)
        {
            int x = std::min(std::max(cx + dx, 0), (int)p.outW - 1);
            int y = std::min(std::max(cy + dy, 0), (int)p.outH - 1);
            const float* t = &rgb[((size_t)y * p.outW + x) * 4];
            float lum = GpuIspYuvM(p, 0) * t[0] + GpuIspYuvM(p, 1) * t[1] + GpuIspYuvM(p, 2) * t[2];
            float w = DnTap(dy + DN_R) * DnTap(dx + DN_R) *
                      std::max(0.0f, 1.0f - std::fabs(lum - lumC) * invRange);
            accR += w * t[0];
            accG += w * t[1];
            accB += w * t[2];
            accW += w;
        }
    }
    float inv = 1.0f / std::max(accW, 1e-6f);
    o[0] = accR * inv; o[1] = accG * inv; o[2] = accB * inv;
}

static inline void RefSampleAt(const GpuIspParams& p, const std::vector<float>& rgb,
                               uint32_t x, uint32_t y, bool dn, float o[3])
{
    const float* s = &rgb[((size_t)y * p.outW + x) * 4];
    if (!dn) { o[0] = s[0]; o[1] = s[1]; o[2] = s[2]; return; }
    RefDnSample(p, rgb, (int)x, (int)y, o);
}

bool GpuIspRefNv12(const GpuIspParams& p, const std::vector<float>& rgbOut, void* nv12)
{
    UINT8* out = (UINT8*)nv12;
    const uint32_t yPitch = (p.outW + 255u) & ~255u;
    const uint32_t uvPitch = (p.outW + 255u) & ~255u;   // outW/2 interleaved UV pairs = outW bytes
    const bool dn = (p.flags & ISP_FLAG_DENOISE) != 0u;
    // s[0..3] is row by, s[4..7] row by+1, exactly the eight samples Nv12CS filters per thread.
    float sm[8][3];

    for (uint32_t bx = 0; bx < p.outW / 4u; bx++)
    {
        for (uint32_t by = 0; by < p.outH; by++)
        {
            // The tile in Nv12CS replicates the border rows, so the row below the last output row
            // is the last row; mirror that clamp here.
            const uint32_t sampleCount = ((by & 1u) == 0u && by + 1u < p.outH) ? 8u : 4u;
            for (uint32_t k = 0; k < sampleCount; k++)
                RefSampleAt(p, rgbOut, bx * 4u + (k & 3u),
                            std::min(by + (k >> 2u), p.outH - 1u), dn, sm[k]);

            uint32_t packedY = 0;
            for (uint32_t i = 0; i < 4u; i++)
            {
                float yv = p.yuvMul[0] * (GpuIspYuvM(p, 0) * sm[i][0] + GpuIspYuvM(p, 1) * sm[i][1] +
                                          GpuIspYuvM(p, 2) * sm[i][2]) + p.yuvAdd[0];
                packedY |= ((uint32_t)clampF(yv + 0.5f, 0.0f, 255.0f)) << (8u * i);
            }
            memcpy(out + (size_t)by * yPitch + bx * 4u, &packedY, 4);

            if ((by & 1u) || by + 1u >= p.outH) continue;
            uint32_t packedUV = 0;
            for (uint32_t j = 0; j < 2u; j++)
            {
                // Same accumulation order as the shader: ((c00 + c10) + c01) + c11, then *0.25.
                float mr = ((sm[j * 2u][0] + sm[j * 2u + 1u][0]) + sm[4u + j * 2u][0]) + sm[4u + j * 2u + 1u][0];
                float mg = ((sm[j * 2u][1] + sm[j * 2u + 1u][1]) + sm[4u + j * 2u][1]) + sm[4u + j * 2u + 1u][1];
                float mb = ((sm[j * 2u][2] + sm[j * 2u + 1u][2]) + sm[4u + j * 2u][2]) + sm[4u + j * 2u + 1u][2];
                mr *= 0.25f; mg *= 0.25f; mb *= 0.25f;
                float u = p.yuvMul[1] * (GpuIspYuvM(p, 3) * mr + GpuIspYuvM(p, 4) * mg + GpuIspYuvM(p, 5) * mb) + p.yuvAdd[1];
                float v = p.yuvMul[2] * (GpuIspYuvM(p, 6) * mr + GpuIspYuvM(p, 7) * mg + GpuIspYuvM(p, 8) * mb) + p.yuvAdd[2];
                packedUV |= ((uint32_t)clampF(u + 0.5f, 0.0f, 255.0f)) << (8u * (j * 2u));
                packedUV |= ((uint32_t)clampF(v + 0.5f, 0.0f, 255.0f)) << (8u * (j * 2u + 1u));
            }
            memcpy(out + (size_t)yPitch * p.outH + (size_t)(by / 2u) * uvPitch + bx * 4u, &packedUV, 4);
        }
    }
    return true;
}

// ---------------------------------------------------------------- stage 4: luma unsharp
// Mirror of SharpYCS in IspCs.hlsl: same binomial tap order (dy outer, dx inner), same
// deadzone/limit guards in the same order, same border clamp. The blur reads a snapshot of
// the pre-sharpen Y plane, which is what the GPU's groupshared tile is.

static inline float SmTapR(int o, int r)
{
    if (r == 4)
    {
        if (o == 4) return 70.0f;
        if (o == 3 || o == 5) return 56.0f;
        if (o == 2 || o == 6) return 28.0f;
        if (o == 1 || o == 7) return 8.0f;
        return 1.0f;
    }
    if (r == 3)
    {
        if (o == 3) return 20.0f;
        if (o == 2 || o == 4) return 15.0f;
        if (o == 1 || o == 5) return 6.0f;
        return 1.0f;
    }
    if (r == 2)
    {
        if (o == 2) return 6.0f;
        if (o == 1 || o == 3) return 4.0f;
        return 1.0f;
    }
    return (o == 1) ? 2.0f : 1.0f;
}

bool GpuIspRefSharpY(const GpuIspParams& p, void* nv12)
{
    if ((p.flags & GPUISP_FLAG_SHARP) == 0u) return true;
    UINT8* out = (UINT8*)nv12;
    const uint32_t yPitch = (p.outW + 255u) & ~255u;
    std::vector<UINT8> src((size_t)yPitch * p.outH);
    memcpy(src.data(), out, src.size());

    int R = (int)std::floor(p.sharpenRadius + 0.5f);
    R = std::min(std::max(R, 1), 4);
    const float invDiv = 1.0f / (float)(1u << (4u * (uint32_t)R));
    const float amount = p.sharpen, thr = p.sharpenThreshold, limit = p.sharpenLimit;

    for (uint32_t by = 0; by < p.outH; by++)
        for (uint32_t bx = 0; bx < p.outW / 4u; bx++)
        {
            uint32_t packed = 0;
            for (uint32_t i = 0; i < 4u; i++)
            {
                uint32_t px = std::min(bx * 4u + i, p.outW - 1u);
                const float c = (float)src[(size_t)by * yPitch + px];
                float acc = 0.0f, vmin = 255.0f, vmax = 0.0f;
                for (int dy = -R; dy <= R; dy++)
                {
                    int yy = std::min(std::max((int)by + dy, 0), (int)p.outH - 1);
                    for (int dx = -R; dx <= R; dx++)
                    {
                        int xx = std::min(std::max((int)px + dx, 0), (int)p.outW - 1);
                        const float s = (float)src[(size_t)yy * yPitch + xx];
                        acc += SmTapR(dy + R, R) * SmTapR(dx + R, R) * s;
                        vmin = std::min(vmin, s);
                        vmax = std::max(vmax, s);
                    }
                }
                float hp = c - acc * invDiv, a = std::fabs(hp);
                if (a > thr)
                {
                    hp = (hp > 0.0f) ? (a - thr) : (thr - a);
                    if (limit > 0.0f && hp > limit) hp = limit;
                    if (limit > 0.0f && hp < -limit) hp = -limit;
                    hp = amount * hp;
                    if (limit > 0.0f && hp > limit) hp = limit;
                    if (limit > 0.0f && hp < -limit) hp = -limit;
                }
                else hp = 0.0f;
                if (p.sharpenEdgeScale > 0.0f)   // edge protection, mirror of SharpYCS
                    hp = clampF((p.sharpenEdgeHi - (vmax - vmin)) * p.sharpenEdgeScale, 0.0f, 1.0f) * hp;
                float ynew = clampF(c + hp, 0.0f, 255.0f);
                packed |= ((uint32_t)clampF(ynew + 0.5f, 0.0f, 255.0f)) << (8u * i);
            }
            memcpy(out + (size_t)by * yPitch + bx * 4u, &packed, 4);
        }
    return true;
}

// ---------------------------------------------------------------- stage 5: chroma denoise + gain
// Mirror of the three stage-5 kernels in IspCs.hlsl: the same 1 4 6 4 1 taps with dy outer / dx inner in
// ascending order, the same border clamp, and the same rounding. Taking a snapshot of the UV plane first
// is what the shader's groupshared tile plus separate destination amount to. The binomial's clamp at
// tile load is the same operation as its per-tap clamp here because one pass reaches only halo 2, so each
// border tap does land on the clamped coordinate. The two guided passes are separate dispatches reading
// the shader's float fields and clamping per tap in frame coordinates, exactly like guidedAt below - that
// is what makes the whole-plane computation here and the tile-cascade version that preceded them
// different, and why the mirror keeps a per-tap clamp: a haloed tile replicates the border into the
// intermediate plane the next pass reads, which is not the same picture. The guide is the Y plane as it
// stands now, which stage 4 sharpened in place when the flag is set: that is the plane the shader reads
// from Nv12Sharp and the plane the readback ships.
bool GpuIspRefChromaSmooth(const GpuIspParams& p, void* nv12)
{
    if ((p.flags & GPUISP_FLAG_CHROMA_SMOOTH) == 0u) return true;
    UINT8* out = (UINT8*)nv12;
    const uint32_t yPitch = (p.outW + 255u) & ~255u;
    const uint32_t uvPitch = (p.outW + 255u) & ~255u;
    const size_t uvOff = (size_t)yPitch * p.outH;
    const uint32_t cols = p.outW / 2u, rows = p.outH / 2u;
    std::vector<UINT8> src((size_t)uvPitch * rows);
    memcpy(src.data(), out + uvOff, src.size());

    static const float kTap[5] = { 1.0f, 4.0f, 6.0f, 4.0f, 1.0f };
    // CsInvSigma: the host stores the reciprocal so both sides multiply by one identical fp32 value;
    // 0 means the preset never opted in and the pre-BuildC007 literal still applies.
    const float invSigma = p.chromaInvSigma > 0.0f ? p.chromaInvSigma : 1.0f / 20.0f;
    const bool recentre = (p.flags & GPUISP_FLAG_CHROMA_RECENTRE) != 0u;
    const bool gain = p.chromaGain > 0.0f;
    // The cascade runs for a gain or for re-centring, and a re-centre without a gain is a gain of 1.00.
    const bool cascade = gain || recentre;
    const float gainOut = gain ? p.chromaGain : 1.0f;

    auto binomAt = [&](uint32_t cx, uint32_t cy, int ch) -> float {
        float acc = 0.0f;
        for (int dy = -2; dy <= 2; dy++) {
            const uint32_t yy = std::min(std::max((int)cy + dy, 0), (int)rows - 1);
            for (int dx = -2; dx <= 2; dx++) {
                const uint32_t xx = std::min(std::max((int)cx + dx, 0), (int)cols - 1);
                acc += kTap[dy + 2] * kTap[dx + 2] * (float)src[(size_t)yy * uvPitch + xx * 2u + ch];
            }
        }
        return acc * (1.0f / 256.0f);
    };

    // One 5x5 luma-guided average of a float plane laid out [ch][cy][cx]. den is at least the centre
    // weight, 36, so the single division below cannot see a zero - same argument as CsGuidedAt.
    auto guidedAt = [&](const float* t, int ch, uint32_t cx, uint32_t cy) -> float {
        const float lumC = (float)out[(size_t)(2u * cy) * yPitch + 2u * cx];
        const float* s = t + (size_t)ch * rows * cols;
        float num = 0.0f, den = 0.0f;
        for (int dy = -2; dy <= 2; dy++) {
            const uint32_t yy = std::min(std::max((int)cy + dy, 0), (int)rows - 1);
            for (int dx = -2; dx <= 2; dx++) {
                const uint32_t xx = std::min(std::max((int)cx + dx, 0), (int)cols - 1);
                const float w = kTap[dy + 2] * kTap[dx + 2] * std::max(0.0f, 1.0f -
                    std::fabs((float)out[(size_t)(2u * yy) * yPitch + 2u * xx] - lumC) * invSigma);
                num += w * s[(size_t)yy * cols + xx];
                den += w;
            }
        }
        return num / den;
    };

    if (!cascade)
    {
        for (uint32_t cy = 0; cy < rows; cy++)
            for (uint32_t wx = 0; wx < p.outW / 4u; wx++)
            {
                uint32_t packed = 0;
                for (uint32_t k = 0; k < 4u; k++)
                    packed |= ((uint32_t)(binomAt(wx * 2u + (k >> 1u), cy, (int)(k & 1u)) + 0.5f)) << (8u * k);
                memcpy(out + uvOff + (size_t)cy * uvPitch + wx * 4u, &packed, 4);
            }
        return true;
    }

    std::vector<float> a(2u * (size_t)rows * cols), b(2u * (size_t)rows * cols);
    for (uint32_t cy = 0; cy < rows; cy++)
        for (uint32_t cx = 0; cx < cols; cx++)
            for (int ch = 0; ch < 2; ch++)
                a[(size_t)ch * rows * cols + (size_t)cy * cols + cx] = binomAt(cx, cy, ch);
    for (uint32_t cy = 0; cy < rows; cy++)
        for (uint32_t cx = 0; cx < cols; cx++)
            for (int ch = 0; ch < 2; ch++)
                b[(size_t)ch * rows * cols + (size_t)cy * cols + cx] = guidedAt(&a[0], ch, cx, cy);

    // The re-centring statistic, the same lane ChromaGuide1CS accumulates in integers: over the sites of
    // the guided-once field - which is exactly what that pass stores - whose L1 distance to grey is within
    // CS_CK codes, sum their rounded deviations and count them, and take mean = sum/count. A plain
    // for-loop over the whole plane, since the group structure only exists to make the shader's atomics
    // cheap.
    // CS_CK / CS_CK_SCALE from IspCs.hlsl; both sides round with floor(x + 0.5), never a cast, because a
    // cast truncates toward zero and these deviations are signed.
    static const int kCk = 8, kCkScale = 64;
    auto recentreOffset = [](long long sum, long long count) -> float {
        if (count <= 0) return 0.0f;
        const float mean = (float)sum / (float)count;
        return (float)(int)std::round(mean * (float)kCkScale) / (float)kCkScale;
    };
    long long sumU = 0, sumV = 0, cnt = 0;
    if (recentre)
        for (uint32_t cy = 0; cy < rows; cy++)
            for (uint32_t cx = 0; cx < cols; cx++)
            {
                const size_t i = (size_t)cy * cols + cx;
                const int du = (int)std::floor(b[i] - 128.0f + 0.5f);
                const int dv = (int)std::floor(b[(size_t)rows * cols + i] - 128.0f + 0.5f);
                // The sums cover exactly the population the count covers - the window, not the frame.
                if (std::abs(du) + std::abs(dv) <= kCk) { sumU += du; sumV += dv; cnt++; }
            }
    const float cenU = 128.0f + recentreOffset(sumU, cnt);
    const float cenV = 128.0f + recentreOffset(sumV, cnt);

    for (uint32_t cy = 0; cy < rows; cy++)
        for (uint32_t wx = 0; wx < p.outW / 4u; wx++)
        {
            uint32_t packed = 0;
            for (uint32_t k = 0; k < 4u; k++)
            {
                const uint32_t cx = wx * 2u + (k >> 1u);
                float v = guidedAt(&b[0], (int)(k & 1u), cx, cy);
                // Build014/015's grey target: the per-channel bias ChromaGuide2CS adds after the
                // re-centring. 0.0f/0.0f leaves the sum bit-identical to Build012's.
                const float bias = ((k & 1u) == 0u) ? p.chromaBiasU : p.chromaBiasV;
                // The gain path only. Subtracting the measured neutral point before multiplying is the
                // point: an offset that survives the gain is the defect Build010 left behind.
                if (recentre) v = clampF(128.0f + bias + gainOut * (v - ((k & 1u) == 0u ? cenU : cenV)), 16.0f, 240.0f);
                else          v = clampF(128.0f + bias + p.chromaGain * (v - 128.0f), 16.0f, 240.0f);
                packed |= ((uint32_t)(v + 0.5f)) << (8u * k);
            }
            memcpy(out + uvOff + (size_t)cy * uvPitch + wx * 4u, &packed, 4);
        }
    return true;
}

