// GpuIspRef.h - CPU reference implementation of the GPUISP2 pipeline.
// Used by the bench to (a) prove the GPU path is wired correctly and (b) keep a working
// reference when no D3D12 adapter is available. Must stay arithmetically identical to
// gpuisp/shaders/IspCs.hlsl.
#pragma once

#include "GpuIsp.h"
#include <vector>

// raw must be tightly packed (width pixels per row, UINT16).
// nv12 uses the GpuIsp*OffsetBytes layout for params.outW/outH.
struct GpuIspRefOutput
{
    std::vector<float> rgbFull;   // rawW*rawH*4, RGBA16F equivalent (may be empty)
    std::vector<float> rgbOut;    // outW*outH*4 after colour stages
};

bool GpuIspRefFrontEnd(const GpuIspParams&, const float* lscCells, const UINT16* raw,
                       std::vector<float>& rgbFull);

bool GpuIspRefColorDown(const GpuIspParams&, const float* lscCells, const GpuIspTone&,
                        const std::vector<float>& rgbFull, std::vector<float>& rgbOut);

bool GpuIspRefNv12(const GpuIspParams&, const std::vector<float>& rgbOut, void* nv12);

// Mirror of SharpYCS: unsharp the finished NV12 luma plane in place. No-op without GPUISP_FLAG_SHARP.
bool GpuIspRefSharpY(const GpuIspParams&, void* nv12);

// Mirror of ChromaSmoothCS: 5x5 binomial on the finished NV12 UV plane in place, luma untouched.
// No-op without GPUISP_FLAG_CHROMA_SMOOTH.
bool GpuIspRefChromaSmooth(const GpuIspParams&, void* nv12);

inline bool GpuIspRefRun(const GpuIspParams& p, const float* lscCells, const GpuIspTone& tone,
                         const UINT16* raw, GpuIspRefOutput& out, void* nv12)
{
    if (!GpuIspRefFrontEnd(p, lscCells, raw, out.rgbFull)) return false;
    if (!GpuIspRefColorDown(p, lscCells, tone, out.rgbFull, out.rgbOut)) return false;
    if (!GpuIspRefNv12(p, out.rgbOut, nv12)) return false;
    if (!GpuIspRefSharpY(p, nv12)) return false;
    return GpuIspRefChromaSmooth(p, nv12);
}
