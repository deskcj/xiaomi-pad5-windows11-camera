// GpuIsp.h - GPUISP2: common parameter block shared by HLSL, the D3D12 core and the CPU reference.
// The struct mirrors the HLSL cbuffer layout byte for byte; do not reorder without changing IspCs.hlsl.
#pragma once

#include <windows.h>
#include <d3d12.h>
#include <cstdint>

#define GPUISP_LSC_MAX_CELLS (32 * 32)
#define GPUISP_TONE_LUT      256

// Flags bits (must match ISP_FLAG_* in IspCs.hlsl)
#define GPUISP_FLAG_LSC   0x0001u
#define GPUISP_FLAG_AWB   0x0002u
#define GPUISP_FLAG_CCM   0x0004u
#define GPUISP_FLAG_TONE  0x0008u
#define GPUISP_FLAG_GAMMA 0x0010u
#define GPUISP_FLAG_FULL  0x0020u   // FrontEndCS also writes the full-resolution RGB texture
#define GPUISP_FLAG_DENOISE 0x0040u // Nv12CS runs the 5x5 bilateral, width = params.denoise (0..1 luma)
#define GPUISP_FLAG_SHARP   0x0080u // SharpYCS runs luma-only unsharp on the finished NV12 Y plane
#define GPUISP_FLAG_CHROMA_SMOOTH 0x0100u // ChromaSmoothCS runs on the finished UV plane: 5x5 binomial,
                                          // then two 5x5 luma-guided passes and the chromaGain below
#define GPUISP_FLAG_CHROMA_RECENTRE 0x0200u // the guided cascade also measures the frame's own chroma
#define GPUISP_FLAG_DIRECT 0x0400u // exact 1:1/full-crop only: MHC+colour in one dispatch
                                          // neutral point (exact integer sums over the near-neutral
                                          // population) and subtracts it before the gain. Needs
                                          // CHROMA_SMOOTH; with chromaGain <= 0 it re-centres at gain 1.

// Bayer mosaic given as the parity offset that turns it into BGGR: (0,0)=BGGR (1,0)=GBRG (0,1)=GRBG (1,1)=RGGB
enum GpuIspPattern
{
    GPUISP_BGGR = 0,
    GPUISP_GRBG = 1,
    GPUISP_GBRG = 2,
    GPUISP_RGGB = 3
};

// Layout notes: fxc gives every element of a scalar float[] in a cbuffer its own 16-byte
// register, so a tightly packed 9-float CCM would be read by the shader as nine separate
// registers. The mirror below therefore stores the matrices as float4 rows, exactly like
// IspCs.hlsl; the row lanes .w are padding and must stay zero.
#pragma pack(push, 4)
struct GpuIspParams
{
    UINT32 rawW, rawH, outW, outH;              //   0
    UINT32 cropX, cropY, cropW, cropH;          //  16
    UINT32 lscCols, lscRows, flags, cfaOff;     //  32  cfaOff = offX | (offY<<1)
    float  blackLevel, whiteLevel, exGain, sharpen;   //  48
    float  awb[4];                              //  64  R, Gr, Gb, B
    float  ccm[3][4];                           //  80, 96, 112
    float  yuvM[3][4];                          // 128, 144, 160
    float  yuvMul[4];                           // 176
    float  yuvAdd[4];                           // 192
    float  gamma, denoise, lscMode, chromaGain;  // 208
    float  sharpenRadius, sharpenThreshold, sharpenLimit, sharpenEdgeLo;  // 224
    float  sharpenEdgeHi, sharpenEdgeScale, nrRangeA, nrRangeB;   // 240
    // Build014's grey target: a static bias on the finished U code, applied after stage 5's re-centring.
    // 0.0 reproduces every preset that never mentions it. Only the U lane is read.
    // chromaInvSigma is stage 5's luma-guided tolerance as the reciprocal the shader multiplies by;
    // 0.0 (the zero-initialised struct) keeps the width every preset before BuildC007 shipped with.
    float  chromaBiasU, chromaBiasV, chromaInvSigma, chromaBiasPad2;   // 256 -> 272
};
#pragma pack(pop)

// Flat 0..8 access into the padded 3x3 rows, so the CPU reference keeps the shader's indexing.
inline float  GpuIspCcm(const GpuIspParams& p, int i) { return p.ccm[i / 3][i % 3]; }
inline float& GpuIspCcm(GpuIspParams& p, int i) { return p.ccm[i / 3][i % 3]; }
inline float  GpuIspYuvM(const GpuIspParams& p, int i) { return p.yuvM[i / 3][i % 3]; }
inline float& GpuIspYuvM(GpuIspParams& p, int i) { return p.yuvM[i / 3][i % 3]; }

static_assert(sizeof(GpuIspParams) == 272, "GpuIspParams must stay exactly 272 bytes (HLSL cbuffer mirror)");
static_assert(offsetof(GpuIspParams, awb) == 64, "awb offset drifted from IspCs.hlsl");
static_assert(offsetof(GpuIspParams, ccm) == 80, "ccm offset drifted from IspCs.hlsl");
static_assert(offsetof(GpuIspParams, yuvM) == 128, "yuvM offset drifted from IspCs.hlsl");
static_assert(offsetof(GpuIspParams, yuvMul) == 176, "yuvMul offset drifted from IspCs.hlsl");
static_assert(offsetof(GpuIspParams, yuvAdd) == 192, "yuvAdd offset drifted from IspCs.hlsl");
static_assert(offsetof(GpuIspParams, gamma) == 208, "gamma offset drifted from IspCs.hlsl");
static_assert(offsetof(GpuIspParams, chromaGain) == 220, "chromaGain offset drifted from IspCs.hlsl");
static_assert(offsetof(GpuIspParams, sharpen) == 60, "sharpen offset drifted from IspCs.hlsl");
static_assert(offsetof(GpuIspParams, sharpenRadius) == 224, "sharpenRadius offset drifted from IspCs.hlsl");
static_assert(offsetof(GpuIspParams, sharpenEdgeLo) == 236, "sharpenEdgeLo offset drifted from IspCs.hlsl");
static_assert(offsetof(GpuIspParams, sharpenEdgeHi) == 240, "sharpenEdgeHi offset drifted from IspCs.hlsl");
static_assert(offsetof(GpuIspParams, sharpenEdgeScale) == 244, "sharpenEdgeScale offset drifted from IspCs.hlsl");
static_assert(offsetof(GpuIspParams, nrRangeA) == 248, "nrRangeA offset drifted from IspCs.hlsl");
static_assert(offsetof(GpuIspParams, nrRangeB) == 252, "nrRangeB offset drifted from IspCs.hlsl");
static_assert(offsetof(GpuIspParams, chromaBiasU) == 256, "chromaBiasU offset drifted from IspCs.hlsl");
static_assert(offsetof(GpuIspParams, chromaBiasV) == 260, "chromaBiasV offset drifted from IspCs.hlsl");
static_assert(offsetof(GpuIspParams, chromaInvSigma) == 264, "chromaInvSigma offset drifted from IspCs.hlsl");

// Signal-dependent bilateral range width. The range test in Nv12CS keys on gamma luma, while sensor
// noise is roughly constant+shot in LINEAR signal, so one scalar half-width is far too wide in the
// darks (where a code of noise is a large gamma delta) and too narrow in the lights: at denoise 0.12
// the whole dark frame sits under the threshold, so the stage degenerates into a plain 5x5 blur and
// the sharpen stage re-amplifies exactly the noise it smeared.
// slope == 0 (also the zero-initialised struct) keeps the ORIGINAL in-shader 1/Denoise reciprocal, so
// a build that does not opt in stays byte-identical. With a slope the weight becomes
// nrRangeA + nrRangeB * luma, one mad with no division, which is what lets the CPU mirror and the GPU
// agree bit for bit; both halves of the line are fitted on the host from the noise model.
inline void GpuIspSetNrRange(GpuIspParams& p, float invRangeAtZeroLuma, float invRangeSlope)
{
    p.nrRangeA = invRangeAtZeroLuma;
    p.nrRangeB = invRangeSlope;
}

// Stage 5's luma-guided chroma tolerance, in luma codes: a neighbour this far from the centre's luma
// stops contributing, so a wider sigma averages flatter areas harder (less colour speckle) and starts to
// run colour across contours. The shader multiplies, so the host stores the reciprocal - computed here, in
// the same single fp32 division the CPU mirror performs, because fxc may otherwise lower '/' to a
// reciprocal-multiply and the two paths would stop agreeing bit for bit.
// sigma <= 0 (also the zero-initialised struct) leaves the field at 0, which the shader reads as the
// width every preset before BuildC007 shipped with.
inline void GpuIspSetChromaSigma(GpuIspParams& p, float sigmaCodes)
{
    p.chromaInvSigma = sigmaCodes > 0.0f ? 1.0f / sigmaCodes : 0.0f;
}

// Edge protection for the luma unsharp: the injection is faded out between two local-contrast
// thresholds so texture keeps its boost and real contours stop ringing. The reciprocal span is
// precomputed here because fxc is allowed to lower a float '/' to an approximate reciprocal, which
// would break the GPU==CPU bit-identity the sharpen stage is gated on.
// edgeHi <= edgeLo (and the zero-initialised struct) both mean "protection off".
inline void GpuIspSetEdgeProtect(GpuIspParams& p, float edgeLo, float edgeHi)
{
    p.sharpenEdgeLo = edgeLo;
    p.sharpenEdgeHi = edgeHi;
    p.sharpenEdgeScale = (edgeHi > edgeLo) ? 1.0f / (edgeHi - edgeLo) : 0.0f;
}

// Chroma saturation gain: params.chromaGain multiplies the finished NV12 UV plane's deviation from
// grey (code 128) after ChromaSmoothCS's denoise passes and holds the result to the limited-range
// chroma band [16,240]. It is a plain scalar with nothing to derive, so it has no setter.
// It lives in finished-code space on purpose: the rear's chroma measures a flat ~0.6x of Android's at
// every saturation level, and the same boost through the CCM clips against ColorDownCS's saturate()
// before the tone curve, which costs 1.29 dB of luma and rotates the hue.
// <= 0 - which is also the zero-initialised struct - leaves the plane exactly as the binomial alone
// delivered it, so a preset that never mentions the key is unaffected.

// Lens shading mesh, uploaded as a structured buffer of (R, Gr, Gb, B) gains per cell.
// params.lscCols / lscRows describe the geometry; cells is cols*rows*4 floats.

// Tone curve: three per-channel LUTs over [0,1] normalized linear light.
struct GpuIspTone
{
    float lut[3 * GPUISP_TONE_LUT];
};

// One frame of sensor RAW as the Windows capture driver hands it over: 16-bit per pixel,
// tightly packed (rawW pixels per row), matching Android's RAW_SENSOR plane0.bin layout.
struct GpuIspRawFrame
{
    UINT32       width, height;
    UINT32       rowPitchBytes;
    const UINT16* pixels;
};

class GpuIspCore;

GpuIspCore* GpuIspCreate(INT32 adapterIndex, bool debugLayer);
void        GpuIspDestroy(GpuIspCore*);

// Runs the full pipeline on the GPU. raw must be tightly packed rawW*rawH*2 bytes; nv12 must be
// GpuIspNv12SizeBytes(outW,outH) bytes in the padded layout described by the offset helpers.
bool GpuIspRun(GpuIspCore*, const GpuIspParams&, const GpuIspTone&, const float* lscCells,
               const void* raw, UINT64 rawSizeBytes, void* nv12);

// Perf P1: same GPU work in a single command list / single fence; totalMs is the true frame latency.
bool GpuIspRunFused(GpuIspCore*, const GpuIspParams&, const GpuIspTone&, const float* lscCells,
                    const void* raw, UINT64 rawSizeBytes, void* nv12, double* totalMs);

UINT32 GpuIspYOffsetBytes(UINT32 outW, UINT32 outH);
UINT32 GpuIspUVOffsetBytes(UINT32 outW, UINT32 outH);
UINT32 GpuIspUVRowPitch(UINT32 outW);
UINT64 GpuIspNv12SizeBytes(UINT32 outW, UINT32 outH);

// Full-resolution RGB readback (RGBA16F interleaved, rawW*rawH*8 bytes) if ISP_FLAG_FULL was set.
bool GpuIspReadFullRgb(GpuIspCore*, float* rgbaOut, UINT64 outSizeBytes);
// Same, for the post-colour preview texture (outW*outH*8 bytes). Debug/diagnostics only.
bool GpuIspReadPreviewRgb(GpuIspCore*, float* rgbaOut, UINT64 outSizeBytes);

// Human-readable last error / per-stage timings in milliseconds.
const char* GpuIspLastError(GpuIspCore*);
void        GpuIspStageTimesMs(GpuIspCore*, double* frontEnd, double* colorDown, double* nv12, double* sharp, double* chroma);
void        GpuIspSetYuvMatrix(GpuIspParams&, int mode);   // 0=BT.601 limited 1=BT.601 full 2=BT.709 limited
