// GpuIspCore.cpp - GPUISP2: D3D12 compute implementation of the ISP pipeline.
// User-mode only (the kernel .sys never touches D3D12). Shaders arrive as cs_5_0 bytecode
// headers produced by fxc at build time on the development PC.
#include "GpuIsp.h"

#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <vector>
#include <cstdio>
#include <cstring>

#include "IspCs_FrontEnd.h"
#include "IspCs_ColorDown.h"
#include "IspCs_Nv12.h"
#include "IspCs_Sharp.h"
#include "IspCs_SharpSep.h"
#include "IspCs_Chroma.h"
#include "IspCs_Guide1.h"
#include "IspCs_Guide2.h"

#define MAXTW 24
#define GS    8      // must match IspCs.hlsl
#define GS2   4      // Nv12CS group edge in 2-row blocks: numthreads(GS, GS/2, 1)

static D3D12_RESOURCE_DESC BufDesc(UINT64 width, D3D12_RESOURCE_FLAGS flags)
{
    D3D12_RESOURCE_DESC d;
    memset(&d, 0, sizeof(d));
    d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    d.Alignment = 0;                      // buffers must have zero alignment
    d.Width = width;
    d.Height = 1;
    d.DepthOrArraySize = 1;
    d.MipLevels = 1;
    d.SampleDesc.Count = 1;
    d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    d.Flags = flags;
    d.Format = DXGI_FORMAT_UNKNOWN;
    return d;
}

#define ISP_FLAG_LSC   0x0001u
#define ISP_FLAG_AWB   0x0002u
#define ISP_FLAG_CCM   0x0004u
#define ISP_FLAG_TONE  0x0008u
#define ISP_FLAG_GAMMA 0x0010u

// Descriptor heap slots (SRV table = 0..4, UAV table = 5..12; see the root signature below).
enum Slot
{
    SLOT_SRV_RAW = 0,
    SLOT_SRV_RGB_FULL,
    SLOT_SRV_RGB_OUT,
    SLOT_SRV_LSC,
    SLOT_SRV_TONE,
    SLOT_UAV_RGB_FULL,
    SLOT_UAV_RGB_OUT,
    SLOT_UAV_NV12,
    SLOT_UAV_NV12_SHARP,
    SLOT_UAV_NV12_UV,
    SLOT_UAV_CHROMA_A,
    SLOT_UAV_CHROMA_B,
    SLOT_UAV_CHROMA_STAT,
    SLOT_COUNT
};

struct ResourceBox
{
    ID3D12Resource* res = nullptr;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
};

class GpuIspCore
{
public:
    ID3D12Device* device = nullptr;
    ID3D12CommandQueue* queue = nullptr;
    ID3D12CommandAllocator* alloc = nullptr;
    ID3D12GraphicsCommandList* list = nullptr;
    ID3D12Fence* fence = nullptr;
    ID3D12InfoQueue* infoQueue = nullptr;
    UINT64 msgSeen = 0;
    HANDLE ev = nullptr;
    UINT64 fenceValue = 0;

    ID3D12RootSignature* rootSig = nullptr;
    ID3D12PipelineState* psoFront = nullptr;
    ID3D12PipelineState* psoColor = nullptr;
    ID3D12PipelineState* psoNv12 = nullptr;
    ID3D12PipelineState* psoSharp = nullptr;
    ID3D12PipelineState* psoSharpSep = nullptr;
    ID3D12PipelineState* psoChroma = nullptr;
    ID3D12PipelineState* psoGuide1 = nullptr;
    ID3D12PipelineState* psoGuide2 = nullptr;

    ID3D12DescriptorHeap* heap = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE cpuSlot[SLOT_COUNT];
    D3D12_GPU_DESCRIPTOR_HANDLE gpuSrvTable = {};
    D3D12_GPU_DESCRIPTOR_HANDLE gpuUavTable = {};
    UINT64 cpuIncrement = 0;

    ResourceBox raw, rgbFull, rgbOut, nv12, params, lsc, tone, uploadRaw, readback;
    ResourceBox nv12Sharp;                // SharpYCS destination: Y plane only
    ResourceBox nv12Uv;                   // ChromaSmoothCS destination: UV plane only
    ResourceBox chromaA;                  // stage 5 float field: the binomial, read by ChromaGuide1CS
    ResourceBox chromaB;                  // stage 5 float field: guided pass 1, read by ChromaGuide2CS
    ResourceBox chromaStat;               // stage 5 integer triple (sumU, sumV, count, pad) for re-centring
    bool hasReadback = false;
    UINT32 rawW = 0, rawH = 0, outW = 0, outH = 0;
    UINT32 rawUploadPitch = 0;
    UINT32 yPitch = 0, uvPitch = 0;
    UINT64 nv12Bytes = 0;
    UINT64 yPlaneBytes = 0;
    UINT64 uvPlaneBytes = 0;
    UINT64 chromaFieldBytes = 0;          // one float per UV code, so uvPlaneBytes * 4
    double msFront = 0, msColor = 0, msNv12 = 0, msSharp = 0, msChroma = 0;
    char error[512] = {0};

    void setError(const char* what, HRESULT hr)
    {
        if (FAILED(hr)) snprintf(error, sizeof(error), "%s failed: HRESULT 0x%08lx", what, (unsigned long)hr);
        else snprintf(error, sizeof(error), "%s failed", what);
    }
};

static HRESULT CreateCommitted(GpuIspCore* c, const D3D12_RESOURCE_DESC& desc, D3D12_HEAP_TYPE heapType,
                               D3D12_RESOURCE_STATES initialState, ID3D12Resource** out)
{
    D3D12_HEAP_PROPERTIES hp;
    memset(&hp, 0, sizeof(hp));
    hp.Type = heapType;
    hp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    hp.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    hp.CreationNodeMask = 1;
    hp.VisibleNodeMask = 1;
    return c->device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &desc, initialState,
                                              nullptr, IID_PPV_ARGS(out));
}

static D3D12_RESOURCE_DESC TexDesc(D3D12_RESOURCE_DIMENSION dim, UINT w, UINT h, WORD depth, WORD bpp,
                                   DXGI_FORMAT fmt, D3D12_RESOURCE_FLAGS flags)
{
    D3D12_RESOURCE_DESC d;
    memset(&d, 0, sizeof(d));
    d.Dimension = dim;
    d.Alignment = 0;   // committed resources: non-zero alignment is E_INVALIDARG (probe-verified)
    d.Width = w;
    d.Height = h;
    d.DepthOrArraySize = depth;
    d.MipLevels = 1;
    d.SampleDesc.Count = 1;
    d.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    d.Flags = flags;
    d.Format = fmt;
    return d;
}

static void Transition(GpuIspCore* c, ResourceBox& r, D3D12_RESOURCE_STATES to)
{
    if (r.state == to || !r.res) return;
    D3D12_RESOURCE_BARRIER b;
    memset(&b, 0, sizeof(b));
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = r.res;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = r.state;
    b.Transition.StateAfter = to;
    c->list->ResourceBarrier(1, &b);
    r.state = to;
}

// A UAV write inside a list is not visible to a later dispatch in the same list until a UAV barrier says
// so; a state transition would normally carry that flush implicitly, which is why the scratch buffers
// that never leave UNORDERED_ACCESS (stage 5's fields) need this explicit one.
static void UavBarrier(GpuIspCore* c, ID3D12Resource* res)
{
    D3D12_RESOURCE_BARRIER b;
    memset(&b, 0, sizeof(b));
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    b.UAV.pResource = res;
    c->list->ResourceBarrier(1, &b);
}

// Stage 5's re-centring statistic crosses two dispatch boundaries inside the same list (the binomial
// zeroes it, ChromaGuide1CS sums into it, ChromaGuide2CS reads it), and the buffer never leaves
// UNORDERED_ACCESS, so each handoff needs this explicit flush. With the bit clear no barrier is recorded
// at all, which keeps a Build010 frame's command list byte-for-byte the one that shipped.
static void UavBarrierStat(GpuIspCore* c, const GpuIspParams& p)
{
    if ((p.flags & GPUISP_FLAG_CHROMA_RECENTRE) == 0u) return;
    UavBarrier(c, c->chromaStat.res);
}

// Same fence pattern that the GpuProbe validated on the Adreno 640: the queue signals, so the
// value only completes after the submitted work is really done.
static bool SignalAndWait(GpuIspCore* c, UINT32 timeoutMs)
{
    c->fenceValue++;
    c->queue->Signal(c->fence, c->fenceValue);
    if (c->fence->GetCompletedValue() < c->fenceValue)
    {
        HRESULT hr = c->fence->SetEventOnCompletion(c->fenceValue, c->ev);
        if (FAILED(hr)) { c->setError("fence SetEventOnCompletion", hr); return false; }
        DWORD r = WaitForSingleObject(c->ev, timeoutMs);
        if (r != WAIT_OBJECT_0)
        {
            snprintf(c->error, sizeof(c->error), "GPU fence did not signal within %u ms (hang or TDR)", timeoutMs);
            return false;
        }
    }
    return true;
}

static void DrainInfoQueue(GpuIspCore* c)
{
    if (!c->infoQueue) return;
    UINT64 total = c->infoQueue->GetNumStoredMessages();
    UINT64 shown = 0;
    for (UINT64 i = c->msgSeen; i < total && shown < 12; i++)
    {
        D3D12_MESSAGE m;
        SIZE_T len = sizeof(m);
        if (SUCCEEDED(c->infoQueue->GetMessage(i, &m, &len)) && m.Severity <= D3D12_MESSAGE_SEVERITY_WARNING)
        {
            printf("  [d3d12] sev %d: %s\n", (int)m.Severity, m.pDescription);
            shown++;
        }
    }
    c->msgSeen = total;
}

static bool Submit(GpuIspCore* c, double* outMs)
{
    HRESULT hr = c->list->Close();
    if (FAILED(hr)) { c->setError("Close", hr); return false; }
    LARGE_INTEGER q, t0, t1;
    QueryPerformanceFrequency(&q);
    QueryPerformanceCounter(&t0);
    ID3D12CommandList* lists[] = { c->list };
    c->queue->ExecuteCommandLists(1, lists);
    if (!SignalAndWait(c, 60000)) return false;
    QueryPerformanceCounter(&t1);
    if (outMs) *outMs = (double)(t1.QuadPart - t0.QuadPart) / (double)q.QuadPart * 1000.0;
    DrainInfoQueue(c);
    return true;
}

static void ResetList(GpuIspCore* c)
{
    c->alloc->Reset();
    c->list->Reset(c->alloc, nullptr);
}

// ---------------------------------------------------------------- geometry helpers

UINT32 GpuIspYOffsetBytes(UINT32, UINT32) { return 0; }
UINT32 GpuIspUVOffsetBytes(UINT32 outW, UINT32 outH)
{
    UINT32 yPitch = (outW + 255u) & ~255u;
    return yPitch * outH;
}
UINT32 GpuIspUVRowPitch(UINT32 outW)
{
    // NV12 chroma is subsampled 2x2 but stores U and V interleaved, so a UV row holds
    // outW/2 * 2 = outW bytes: the same 256-aligned pitch as the luma row.
    return (outW + 255u) & ~255u;
}
UINT64 GpuIspNv12SizeBytes(UINT32 outW, UINT32 outH)
{
    return (UINT64)GpuIspUVOffsetBytes(outW, outH) + (UINT64)GpuIspUVRowPitch(outW) * (outH / 2u);
}

static bool CheckGeometry(GpuIspCore* c, const GpuIspParams& p)
{
    if ((p.flags & GPUISP_FLAG_DIRECT) && (p.rawW!=p.outW || p.rawH!=p.outH || p.cropX || p.cropY || p.cropW!=p.rawW || p.cropH!=p.rawH))
    {snprintf(c->error,sizeof(c->error),"DIRECT requires a full-crop 1:1 frame");return false;}
    if (p.rawW < 8 || p.rawH < 8 || p.outW < 8 || p.outH < 8) { snprintf(c->error, sizeof(c->error), "frame too small"); return false; }
    if ((p.outW % 4u) != 0u || (p.outH % 2u) != 0u) { snprintf(c->error, sizeof(c->error), "outW must be a multiple of 4 and outH even"); return false; }
    if (p.cropX + p.cropW > p.rawW || p.cropY + p.cropH > p.rawH) { snprintf(c->error, sizeof(c->error), "crop outside RAW"); return false; }
    if ((UINT64)p.cropW * p.outW >= 0x80000000ull || (UINT64)p.cropH * p.outH >= 0x80000000ull)
    { snprintf(c->error, sizeof(c->error), "span arithmetic would overflow uint32"); return false; }
    // One thread group covers GS output pixels, so the tile spans GS*crop/out source pixels.
    UINT64 maxTw = ((UINT64)p.cropW * GS + p.outW - 1) / p.outW + 2;
    UINT64 maxTh = ((UINT64)p.cropH * GS + p.outH - 1) / p.outH + 2;
    if (maxTw > MAXTW || maxTh > MAXTW) { snprintf(c->error, sizeof(c->error), "downscale factor too large for the %u-wide tile", MAXTW); return false; }
    if (p.lscCols * p.lscRows > GPUISP_LSC_MAX_CELLS) { snprintf(c->error, sizeof(c->error), "LSC mesh larger than %d cells", GPUISP_LSC_MAX_CELLS); return false; }
    return true;
}

static void ReleaseResources(GpuIspCore* c)
{
    if (c->raw.res) c->raw.res->Release();
    if (c->rgbFull.res) c->rgbFull.res->Release();
    if (c->rgbOut.res) c->rgbOut.res->Release();
    if (c->nv12.res) c->nv12.res->Release();
    if (c->nv12Sharp.res) c->nv12Sharp.res->Release();
    if (c->nv12Uv.res) c->nv12Uv.res->Release();
    if (c->chromaA.res) c->chromaA.res->Release();
    if (c->chromaB.res) c->chromaB.res->Release();
    if (c->chromaStat.res) c->chromaStat.res->Release();
    if (c->params.res) c->params.res->Release();
    if (c->lsc.res) c->lsc.res->Release();
    if (c->tone.res) c->tone.res->Release();
    if (c->uploadRaw.res) c->uploadRaw.res->Release();
    if (c->readback.res) c->readback.res->Release();
    c->raw = ResourceBox(); c->rgbFull = ResourceBox(); c->rgbOut = ResourceBox();
    c->nv12 = ResourceBox(); c->params = ResourceBox(); c->lsc = ResourceBox();
    c->tone = ResourceBox(); c->uploadRaw = ResourceBox(); c->readback = ResourceBox();
    c->nv12Sharp = ResourceBox();
    c->nv12Uv = ResourceBox();
    c->chromaA = ResourceBox();
    c->chromaB = ResourceBox();
    c->chromaStat = ResourceBox();
    c->hasReadback = false;
}

static bool AllocateFrame(GpuIspCore* c, const GpuIspParams& p)
{
    if (c->rawW == p.rawW && c->rawH == p.rawH && c->outW == p.outW && c->outH == p.outH && c->raw.res)
        return true;

    ReleaseResources(c);
    c->rawW = p.rawW; c->rawH = p.rawH; c->outW = p.outW; c->outH = p.outH;

    HRESULT hr;
    UINT32 rawPitch = (p.rawW * 2u + 255u) & ~255u;
    c->rawUploadPitch = rawPitch;
    c->yPitch = (p.outW + 255u) & ~255u;
    c->uvPitch = GpuIspUVRowPitch(p.outW);
    c->nv12Bytes = GpuIspNv12SizeBytes(p.outW, p.outH);

    D3D12_RESOURCE_DESC d;

    d = TexDesc(D3D12_RESOURCE_DIMENSION_TEXTURE2D, p.rawW, p.rawH, 1, 2, DXGI_FORMAT_R16_UINT, D3D12_RESOURCE_FLAG_NONE);
    hr = CreateCommitted(c, d, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST, &c->raw.res);
    if (FAILED(hr)) { c->setError("alloc RAW R16_UINT texture", hr); return false; }
    c->raw.state = D3D12_RESOURCE_STATE_COPY_DEST;

    d = TexDesc(D3D12_RESOURCE_DIMENSION_TEXTURE2D, p.rawW, p.rawH, 1, 8, DXGI_FORMAT_R16G16B16A16_FLOAT,
                D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    hr = CreateCommitted(c, d, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, &c->rgbFull.res);
    if (FAILED(hr)) { c->setError("alloc full-res RGBA16F", hr); return false; }
    c->rgbFull.state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    d = TexDesc(D3D12_RESOURCE_DIMENSION_TEXTURE2D, p.outW, p.outH, 1, 8, DXGI_FORMAT_R16G16B16A16_FLOAT,
                D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    hr = CreateCommitted(c, d, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, &c->rgbOut.res);
    if (FAILED(hr)) { c->setError("alloc out-res RGBA16F", hr); return false; }
    c->rgbOut.state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    d = BufDesc(c->nv12Bytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    hr = CreateCommitted(c, d, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, &c->nv12.res);
    if (FAILED(hr)) { c->setError("alloc NV12 UAV buffer", hr); return false; }
    c->nv12.state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    // SharpYCS reads the Y plane of nv12 and writes this scratch, so the stage has no
    // write-after-read hazard inside its own dispatch. Only the Y plane is needed -
    // the UV plane is still read back straight from nv12.
    c->yPlaneBytes = (UINT64)c->yPitch * p.outH;
    d = BufDesc(c->yPlaneBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    hr = CreateCommitted(c, d, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, &c->nv12Sharp.res);
    if (FAILED(hr)) { c->setError("alloc NV12 sharpen scratch", hr); return false; }
    c->nv12Sharp.state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    // ChromaSmoothCS reads the UV plane of nv12 and writes this scratch, which holds the UV plane
    // only and is indexed from 0. Allocated unconditionally: it is a few MB and the geometry may
    // not change between a flag-off and a flag-on frame, and AllocateFrame reuses buffers then.
    c->uvPlaneBytes = c->nv12Bytes - c->yPlaneBytes;
    d = BufDesc(c->uvPlaneBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    hr = CreateCommitted(c, d, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, &c->nv12Uv.res);
    if (FAILED(hr)) { c->setError("alloc NV12 chroma scratch", hr); return false; }
    c->nv12Uv.state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    // Stage 5's guided cascade holds its intermediates as float fields in GPU memory rather than a
    // groupshared tile, one float per chroma code in the UV plane's own byte order (pitch*cy + 2*cx + ch),
    // so each pass clamps its tap indices in frame coordinates exactly like GpuIspRefChromaSmooth - the
    // haloed-tile version replicated the border into the next pass and could not be bit-identical. Two
    // fields because pass 1 reads what pass 0 wrote while pass 2 reads pass 1's. 4 MB each at 1920x1080,
    // which is why they are still allocated with the frame: AllocateFrame caches on geometry, so a gain
    // turning on mid-sequence must not have to re-allocate.
    c->chromaFieldBytes = c->uvPlaneBytes * 4u;
    d = BufDesc(c->chromaFieldBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    hr = CreateCommitted(c, d, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, &c->chromaA.res);
    if (FAILED(hr)) { c->setError("alloc chroma binomial field", hr); return false; }
    c->chromaA.state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    d = BufDesc(c->chromaFieldBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    hr = CreateCommitted(c, d, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, &c->chromaB.res);
    if (FAILED(hr)) { c->setError("alloc chroma guide1 field", hr); return false; }
    c->chromaB.state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    // The re-centring statistic is one int4: (sum of rounded near-neutral U deviations, the V sum, the
    // count of near-neutral sites, pad). Allocated with the frame for the same reason as the fields.
    d = BufDesc(16, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    hr = CreateCommitted(c, d, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, &c->chromaStat.res);
    if (FAILED(hr)) { c->setError("alloc chroma recentre stat", hr); return false; }
    c->chromaStat.state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    d = BufDesc(sizeof(GpuIspParams), D3D12_RESOURCE_FLAG_NONE);
    hr = CreateCommitted(c, d, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, &c->params.res);
    if (FAILED(hr)) { c->setError("alloc params CB", hr); return false; }
    c->params.state = D3D12_RESOURCE_STATE_GENERIC_READ;

    d = BufDesc(GPUISP_LSC_MAX_CELLS * 16, D3D12_RESOURCE_FLAG_NONE);
    hr = CreateCommitted(c, d, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, &c->lsc.res);
    if (FAILED(hr)) { c->setError("alloc LSC buffer", hr); return false; }
    c->lsc.state = D3D12_RESOURCE_STATE_GENERIC_READ;

    d = BufDesc(sizeof(GpuIspTone), D3D12_RESOURCE_FLAG_NONE);
    hr = CreateCommitted(c, d, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, &c->tone.res);
    if (FAILED(hr)) { c->setError("alloc tone buffer", hr); return false; }
    c->tone.state = D3D12_RESOURCE_STATE_GENERIC_READ;

    d = BufDesc((UINT64)rawPitch * p.rawH, D3D12_RESOURCE_FLAG_NONE);
    hr = CreateCommitted(c, d, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, &c->uploadRaw.res);
    if (FAILED(hr)) { c->setError("alloc RAW upload buffer", hr); return false; }
    c->uploadRaw.state = D3D12_RESOURCE_STATE_GENERIC_READ;

    d = BufDesc(c->nv12Bytes, D3D12_RESOURCE_FLAG_NONE);
    hr = CreateCommitted(c, d, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST, &c->readback.res);
    if (FAILED(hr)) { c->setError("alloc NV12 readback buffer", hr); return false; }
    c->readback.state = D3D12_RESOURCE_STATE_COPY_DEST;
    c->hasReadback = true;

    // Descriptors: two views of the RGBA16F textures (SRV for Load, UAV for the store).
    D3D12_DESCRIPTOR_HEAP_DESC hd;
    memset(&hd, 0, sizeof(hd));
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    hd.NumDescriptors = SLOT_COUNT;
    hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    hr = c->device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&c->heap));
    if (FAILED(hr)) { c->setError("CreateDescriptorHeap", hr); return false; }
    c->cpuIncrement = c->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_CPU_DESCRIPTOR_HANDLE base = c->heap->GetCPUDescriptorHandleForHeapStart();
    for (int i = 0; i < SLOT_COUNT; i++)
        c->cpuSlot[i].ptr = base.ptr + (SIZE_T)i * (size_t)c->cpuIncrement;
    D3D12_GPU_DESCRIPTOR_HANDLE gbase = c->heap->GetGPUDescriptorHandleForHeapStart();
    c->gpuSrvTable.ptr = gbase.ptr + (UINT64)SLOT_SRV_RAW * c->cpuIncrement;
    c->gpuUavTable.ptr = gbase.ptr + (UINT64)SLOT_UAV_RGB_FULL * c->cpuIncrement;

    D3D12_SHADER_RESOURCE_VIEW_DESC sv;
    memset(&sv, 0, sizeof(sv));
    sv.Format = DXGI_FORMAT_R16_UINT;
    sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sv.Texture2D.MipLevels = 1;
    c->device->CreateShaderResourceView(c->raw.res, &sv, c->cpuSlot[SLOT_SRV_RAW]);

    memset(&sv, 0, sizeof(sv));
    sv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sv.Texture2D.MipLevels = 1;
    c->device->CreateShaderResourceView(c->rgbFull.res, &sv, c->cpuSlot[SLOT_SRV_RGB_FULL]);
    c->device->CreateShaderResourceView(c->rgbOut.res, &sv, c->cpuSlot[SLOT_SRV_RGB_OUT]);

    D3D12_UNORDERED_ACCESS_VIEW_DESC uv;
    memset(&uv, 0, sizeof(uv));
    uv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    uv.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    c->device->CreateUnorderedAccessView(c->rgbFull.res, nullptr, &uv, c->cpuSlot[SLOT_UAV_RGB_FULL]);
    c->device->CreateUnorderedAccessView(c->rgbOut.res, nullptr, &uv, c->cpuSlot[SLOT_UAV_RGB_OUT]);

    memset(&uv, 0, sizeof(uv));
    uv.Format = DXGI_FORMAT_UNKNOWN;
    uv.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uv.Buffer.FirstElement = 0;
    uv.Buffer.NumElements = (UINT)(c->nv12Bytes / 4u);
    uv.Buffer.StructureByteStride = 4;
    uv.Buffer.CounterOffsetInBytes = 0;
    uv.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
    c->device->CreateUnorderedAccessView(c->nv12.res, nullptr, &uv, c->cpuSlot[SLOT_UAV_NV12]);

    memset(&uv, 0, sizeof(uv));
    uv.Format = DXGI_FORMAT_UNKNOWN;
    uv.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uv.Buffer.NumElements = (UINT)(c->yPlaneBytes / 4u);
    uv.Buffer.StructureByteStride = 4;
    c->device->CreateUnorderedAccessView(c->nv12Sharp.res, nullptr, &uv, c->cpuSlot[SLOT_UAV_NV12_SHARP]);

    memset(&uv, 0, sizeof(uv));
    uv.Format = DXGI_FORMAT_UNKNOWN;
    uv.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uv.Buffer.NumElements = (UINT)(c->uvPlaneBytes / 4u);
    uv.Buffer.StructureByteStride = 4;
    c->device->CreateUnorderedAccessView(c->nv12Uv.res, nullptr, &uv, c->cpuSlot[SLOT_UAV_NV12_UV]);

    // The stage-5 cascade fields are RWStructuredBuffer<float>, so the element count is the byte count
    // of the UV plane - one float per chroma code.
    memset(&uv, 0, sizeof(uv));
    uv.Format = DXGI_FORMAT_UNKNOWN;
    uv.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uv.Buffer.NumElements = (UINT)(c->uvPlaneBytes);
    uv.Buffer.StructureByteStride = 4;
    c->device->CreateUnorderedAccessView(c->chromaA.res, nullptr, &uv, c->cpuSlot[SLOT_UAV_CHROMA_A]);
    c->device->CreateUnorderedAccessView(c->chromaB.res, nullptr, &uv, c->cpuSlot[SLOT_UAV_CHROMA_B]);

    // RWStructuredBuffer<int> of four elements, so the shader indexes the triple as [0..2].
    memset(&uv, 0, sizeof(uv));
    uv.Format = DXGI_FORMAT_UNKNOWN;
    uv.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uv.Buffer.NumElements = 4;
    uv.Buffer.StructureByteStride = 4;
    c->device->CreateUnorderedAccessView(c->chromaStat.res, nullptr, &uv, c->cpuSlot[SLOT_UAV_CHROMA_STAT]);

    D3D12_SHADER_RESOURCE_VIEW_DESC sb;
    memset(&sb, 0, sizeof(sb));
    sb.Format = DXGI_FORMAT_UNKNOWN;
    sb.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    sb.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sb.Buffer.FirstElement = 0;
    sb.Buffer.StructureByteStride = 16;
    // Sized to the maximum mesh, not to the current frame's: AllocateFrame caches on geometry
    // only, so a mesh-size change at the same frame size would otherwise leave this view
    // reporting the previous frame's cell count.
    sb.Buffer.NumElements = GPUISP_LSC_MAX_CELLS;
    sb.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
    c->device->CreateShaderResourceView(c->lsc.res, &sb, c->cpuSlot[SLOT_SRV_LSC]);

    memset(&sb, 0, sizeof(sb));
    sb.Format = DXGI_FORMAT_UNKNOWN;
    sb.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    sb.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sb.Buffer.FirstElement = 0;
    sb.Buffer.StructureByteStride = 4;
    sb.Buffer.NumElements = 3 * GPUISP_TONE_LUT;
    sb.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
    c->device->CreateShaderResourceView(c->tone.res, &sb, c->cpuSlot[SLOT_SRV_TONE]);

    return true;
}

// ---------------------------------------------------------------- public API

GpuIspCore* GpuIspCreate(INT32 adapterIndex, bool debugLayer)
{
    GpuIspCore* c = new GpuIspCore();
    HRESULT hr = S_OK;

    if (debugLayer)
    {
        ID3D12Debug* dbg = nullptr;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dbg))))
        {
            dbg->EnableDebugLayer();
            dbg->Release();
        }
    }

    IDXGIFactory4* factory = nullptr;
    hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    if (FAILED(hr)) { c->setError("CreateDXGIFactory1", hr); return c; }

    IDXGIAdapter1* adapter = nullptr;
    UINT found = 0;
    for (UINT i = 0; ; i++)
    {
        HRESULT e = factory->EnumAdapters1(i, &adapter);
        if (e == DXGI_ERROR_NOT_FOUND) { adapter = nullptr; break; }
        if (FAILED(e)) { adapter = nullptr; break; }
        DXGI_ADAPTER_DESC1 d;
        adapter->GetDesc1(&d);
        if ((d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) { adapter->Release(); adapter = nullptr; continue; }
        if ((INT32)found == adapterIndex) break;
        found++;
        adapter->Release();
        adapter = nullptr;
    }
    if (!adapter)
    {
        UINT i = 0;
        while (SUCCEEDED(factory->EnumAdapters1(i, &adapter)))
        {
            DXGI_ADAPTER_DESC1 d;
            adapter->GetDesc1(&d);
            if ((d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0) break;
            adapter->Release();
            adapter = nullptr;
            i++;
        }
    }
    if (!adapter) { snprintf(c->error, sizeof(c->error), "no hardware D3D12 adapter"); factory->Release(); return c; }

    hr = D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&c->device));
    if (FAILED(hr)) { c->setError("D3D12CreateDevice", hr); adapter->Release(); factory->Release(); return c; }
    {
        DXGI_ADAPTER_DESC1 ad;
        adapter->GetDesc1(&ad);
        printf("  adapter: %S  VRAM %llu MB\n", ad.Description, (unsigned long long)(ad.DedicatedVideoMemory / (1024ull * 1024ull)));
    }

    D3D12_COMMAND_QUEUE_DESC qd;
    memset(&qd, 0, sizeof(qd));
    qd.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
    qd.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    qd.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    qd.NodeMask = 0;
    hr = c->device->CreateCommandQueue(&qd, IID_PPV_ARGS(&c->queue));
    if (FAILED(hr)) { c->setError("CreateCommandQueue(COMPUTE)", hr); adapter->Release(); factory->Release(); return c; }

    hr = c->device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(&c->alloc));
    if (FAILED(hr)) { c->setError("CreateCommandAllocator", hr); adapter->Release(); factory->Release(); return c; }
    hr = c->device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COMPUTE, c->alloc, nullptr, IID_PPV_ARGS(&c->list));
    if (FAILED(hr)) { c->setError("CreateCommandList", hr); adapter->Release(); factory->Release(); return c; }
    hr = c->device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&c->fence));
    if (FAILED(hr)) { c->setError("CreateFence", hr); adapter->Release(); factory->Release(); return c; }
    c->ev = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (debugLayer)
    {
        c->device->QueryInterface(IID_PPV_ARGS(&c->infoQueue));
        printf("  d3d12 debug layer: %s\n", c->infoQueue ? "active" : "NOT AVAILABLE (install the Graphics Tools optional feature)");
    }

    // Root signature: b0 = params CBV, t0..t4 = SRV table, u0..u7 = UAV table.
    // The EWDK SDK d3d12.h has no CD3D12_* helper constructors, so every descriptor struct is
    // memset-then-filled - same style as the tablet-verified GpuProbe.
    D3D12_DESCRIPTOR_RANGE srvRange, uavRange;
    memset(&srvRange, 0, sizeof(srvRange));
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 5;
    srvRange.BaseShaderRegister = 0;
    srvRange.RegisterSpace = 0;
    srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    memset(&uavRange, 0, sizeof(uavRange));
    uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uavRange.NumDescriptors = 8;
    uavRange.BaseShaderRegister = 0;
    uavRange.RegisterSpace = 0;
    uavRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    D3D12_ROOT_PARAMETER rp[3];
    memset(rp, 0, sizeof(rp));
    rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rp[1].DescriptorTable.NumDescriptorRanges = 1;
    rp[1].DescriptorTable.pDescriptorRanges = &srvRange;
    rp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rp[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rp[2].DescriptorTable.NumDescriptorRanges = 1;
    rp[2].DescriptorTable.pDescriptorRanges = &uavRange;
    rp[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rs;
    memset(&rs, 0, sizeof(rs));
    rs.NumParameters = 3;
    rs.pParameters = rp;
    rs.NumStaticSamplers = 0;
    rs.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ID3DBlob* sig = nullptr;
    ID3DBlob* err = nullptr;
    hr = D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err);
    if (FAILED(hr)) { c->setError("D3D12SerializeRootSignature", hr); adapter->Release(); factory->Release(); return c; }
    hr = c->device->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&c->rootSig));
    sig->Release();
    if (FAILED(hr)) { c->setError("CreateRootSignature", hr); adapter->Release(); factory->Release(); return c; }

    struct { const void* bc; SIZE_T size; ID3D12PipelineState** pso; const char* name; } stages[] = {
        { g_csFrontEnd, sizeof(g_csFrontEnd), &c->psoFront, "FrontEndCS" },
        { g_csColorDown, sizeof(g_csColorDown), &c->psoColor, "ColorDownCS" },
        { g_csNv12, sizeof(g_csNv12), &c->psoNv12, "Nv12CS" },
        { g_csSharp, sizeof(g_csSharp), &c->psoSharp, "SharpYCS" },
        { g_csSharpSep, sizeof(g_csSharpSep), &c->psoSharpSep, "SharpYCSep" },
        { g_csChroma, sizeof(g_csChroma), &c->psoChroma, "ChromaSmoothCS" },
        { g_csGuide1, sizeof(g_csGuide1), &c->psoGuide1, "ChromaGuide1CS" },
        { g_csGuide2, sizeof(g_csGuide2), &c->psoGuide2, "ChromaGuide2CS" },
    };
    for (auto& s : stages)
    {
        D3D12_COMPUTE_PIPELINE_STATE_DESC ps;
        memset(&ps, 0, sizeof(ps));
        ps.pRootSignature = c->rootSig;
        ps.CS.pShaderBytecode = s.bc;
        ps.CS.BytecodeLength = s.size;
        ps.NodeMask = 0;
        hr = c->device->CreateComputePipelineState(&ps, IID_PPV_ARGS(s.pso));
        if (FAILED(hr)) { c->setError(s.name, hr); break; }
    }

    adapter->Release();
    factory->Release();
    if (c->error[0]) return c;
    return c;
}

void GpuIspDestroy(GpuIspCore* c)
{
    if (!c) return;
    ReleaseResources(c);
    if (c->psoFront) c->psoFront->Release();
    if (c->psoColor) c->psoColor->Release();
    if (c->psoNv12) c->psoNv12->Release();
    if (c->psoSharp) c->psoSharp->Release();
    if (c->psoSharpSep) c->psoSharpSep->Release();
    if (c->psoChroma) c->psoChroma->Release();
    if (c->psoGuide1) c->psoGuide1->Release();
    if (c->psoGuide2) c->psoGuide2->Release();
    if (c->heap) c->heap->Release();
    if (c->rootSig) c->rootSig->Release();
    if (c->ev) CloseHandle(c->ev);
    if (c->infoQueue) c->infoQueue->Release();
    if (c->fence) c->fence->Release();
    if (c->list) c->list->Release();
    if (c->alloc) c->alloc->Release();
    if (c->queue) c->queue->Release();
    if (c->device) c->device->Release();
    delete c;
}

const char* GpuIspLastError(GpuIspCore* c) { return c ? c->error : "no core"; }
void GpuIspStageTimesMs(GpuIspCore* c, double* a, double* b, double* d, double* e, double* f)
{
    if (!c) return;
    if (a) *a = c->msFront;
    if (b) *b = c->msColor;
    if (d) *d = c->msNv12;
    if (e) *e = c->msSharp;
    if (f) *f = c->msChroma;
}

void GpuIspSetYuvMatrix(GpuIspParams& p, int mode)
{
    // Rows are luma/chroma coefficient sums; yuvMul scales to the 8-bit range. Both chroma rows are
    // normalised to +-0.5 (0.5 is the pure-B / pure-R excursion), so limited range needs 224 to reach
    // the standard 16..240 codes: 112 would put pure blue at 184 and deliver every frame at half
    // saturation. YuvToRgb8, derive_tone.py and decode_android_yuv.py all already read 224.
    static const float k601[9] = { 0.29900f, 0.58700f, 0.11400f,
                                  -0.168736f, -0.331264f, 0.500000f,
                                   0.500000f, -0.418688f, -0.081312f };
    static const float k709[9] = { 0.21260f, 0.71520f, 0.07220f,
                                  -0.114572f, -0.385428f, 0.500000f,
                                   0.500000f, -0.468126f, -0.031874f };
    const float* m = (mode == 2) ? k709 : k601;
    for (int i = 0; i < 9; i++) GpuIspYuvM(p, i) = m[i];
    if (mode == 1) { p.yuvMul[0] = 255.0f; p.yuvMul[1] = 254.0f; p.yuvMul[2] = 254.0f; p.yuvAdd[0] = 0.0f; p.yuvAdd[1] = 128.0f; p.yuvAdd[2] = 128.0f; }
    else           { p.yuvMul[0] = 219.0f; p.yuvMul[1] = 224.0f; p.yuvMul[2] = 224.0f; p.yuvAdd[0] = 16.0f; p.yuvAdd[1] = 128.0f; p.yuvAdd[2] = 128.0f; }
}

// The finished frame lives in up to three places depending on which optional stages ran: the Y plane
// in the sharpen scratch or in nv12, the UV plane in the chroma scratch or in nv12. Both staged and
// fused paths use this so the readback can never drift between them.
static void EmitNv12Readback(GpuIspCore* c, const GpuIspParams& p)
{
    ResourceBox* ySrc = (p.flags & GPUISP_FLAG_SHARP) ? &c->nv12Sharp : &c->nv12;
    Transition(c, c->readback, D3D12_RESOURCE_STATE_COPY_DEST);
    Transition(c, *ySrc, D3D12_RESOURCE_STATE_COPY_SOURCE);
    c->list->CopyBufferRegion(c->readback.res, 0, ySrc->res, 0, (UINT)c->yPlaneBytes);
    if (p.flags & GPUISP_FLAG_CHROMA_SMOOTH)
    {
        Transition(c, c->nv12Uv, D3D12_RESOURCE_STATE_COPY_SOURCE);
        c->list->CopyBufferRegion(c->readback.res, (UINT)c->yPlaneBytes, c->nv12Uv.res, 0,
                                  (UINT)c->uvPlaneBytes);
    }
    else
    {
        Transition(c, c->nv12, D3D12_RESOURCE_STATE_COPY_SOURCE);
        c->list->CopyBufferRegion(c->readback.res, (UINT)c->yPlaneBytes, c->nv12.res,
                                  (UINT)c->yPlaneBytes, (UINT)c->uvPlaneBytes);
    }
}

// Stage 4 has two byte-identical forms (see IspCs.hlsl): the direct (2R+1)^2 read and a groupshared
// vertical collapse that only pays from radius 3 up. The radius picks the pipeline, never the caller,
// so a preset can silently regress; the rounding matches the shader's clamp of SharpenRadius.
static ID3D12PipelineState* PickSharp(GpuIspCore* c, const GpuIspParams& p)
{
    return ((int)(p.sharpenRadius + 0.5f) >= 3) ? c->psoSharpSep : c->psoSharp;
}

bool GpuIspRun(GpuIspCore* c, const GpuIspParams& p, const GpuIspTone& tone, const float* lscCells,
               const void* raw, UINT64 rawSizeBytes, void* nv12)
{
    if (!c || !c->device) { if (c) snprintf(c->error, sizeof(c->error), "core not initialised"); return false; }
    // A PSO that failed to compile must not turn into a silent no-op dispatch.
    if (!c->psoFront || !c->psoColor || !c->psoNv12 || !c->psoSharp || !c->psoSharpSep || !c->psoChroma ||
        !c->psoGuide1 || !c->psoGuide2)
    { snprintf(c->error, sizeof(c->error), "compute PSOs missing (%s)", c->error[0] ? c->error : "no recorded error"); return false; }
    if (!CheckGeometry(c, p)) return false;
    if (!AllocateFrame(c, p)) return false;

    // ---- map the CPU-side inputs
    {
        void* ptr = nullptr;
        D3D12_RANGE rW = {0, sizeof(GpuIspParams)}, rR = {0, 0};
        if (FAILED(c->params.res->Map(0, &rW, &ptr))) { c->setError("params Map", E_FAIL); return false; }
        memcpy(ptr, &p, sizeof(p));
        D3D12_RANGE empty = {0, 0};
        c->params.res->Unmap(0, &empty);
        (void)rW; (void)rR;
    }
    {
        UINT32 cells = (p.lscCols * p.lscRows) ? (p.lscCols * p.lscRows) : 1u;
        void* ptr = nullptr;
        D3D12_RANGE rW = {0, cells * 16u};
        if (FAILED(c->lsc.res->Map(0, &rW, &ptr))) { c->setError("LSC Map", E_FAIL); return false; }
        memcpy(ptr, lscCells, (size_t)cells * 16u);
        D3D12_RANGE empty = {0, 0};
        c->lsc.res->Unmap(0, &empty);
    }
    {
        void* ptr = nullptr;
        D3D12_RANGE rW = {0, (UINT)sizeof(GpuIspTone)};
        if (FAILED(c->tone.res->Map(0, &rW, &ptr))) { c->setError("tone Map", E_FAIL); return false; }
        memcpy(ptr, tone.lut, sizeof(GpuIspTone));
        D3D12_RANGE empty = {0, 0};
        c->tone.res->Unmap(0, &empty);
    }
    if (rawSizeBytes > (UINT64)c->rawUploadPitch * c->rawH) { snprintf(c->error, sizeof(c->error), "RAW larger than the upload buffer"); return false; }
    {
        void* ptr = nullptr;
        D3D12_RANGE rW = {0, (UINT)((UINT64)c->rawUploadPitch * c->rawH)};
        if (FAILED(c->uploadRaw.res->Map(0, &rW, &ptr))) { c->setError("RAW upload Map", E_FAIL); return false; }
        UINT8* dst = (UINT8*)ptr;
        const UINT8* src = (const UINT8*)raw;
        UINT32 srcPitch = c->rawW * 2u;
        // The caller hands over a tight packed_raw buffer; clamp the last row so a short file
        // can never read past the caller's allocation.
        for (UINT32 y = 0; y < c->rawH; y++)
        {
            UINT64 remaining = rawSizeBytes - (UINT64)y * srcPitch;
            if (remaining <= 0) break;
            size_t n = (remaining < srcPitch) ? (size_t)remaining : (size_t)srcPitch;
            memcpy(dst + (UINT64)y * c->rawUploadPitch, src + (UINT64)y * srcPitch, n);
        }
        D3D12_RANGE empty = {0, 0};
        c->uploadRaw.res->Unmap(0, &empty);
    }

    ID3D12DescriptorHeap* heaps[] = { c->heap };
    UINT32 groupsW, groupsH;

    // ---- stage 0: upload the sensor RAW into the R16_UINT texture
    ResetList(c);
    D3D12_TEXTURE_COPY_LOCATION rawDst, rawSrc;
    memset(&rawDst, 0, sizeof(rawDst));
    rawDst.pResource = c->raw.res;
    rawDst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    rawDst.SubresourceIndex = 0;
    memset(&rawSrc, 0, sizeof(rawSrc));
    rawSrc.pResource = c->uploadRaw.res;
    rawSrc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    rawSrc.PlacedFootprint.Offset = 0;
    rawSrc.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R16_UINT;
    rawSrc.PlacedFootprint.Footprint.Width = c->rawW;
    rawSrc.PlacedFootprint.Footprint.Height = c->rawH;
    rawSrc.PlacedFootprint.Footprint.Depth = 1;
    rawSrc.PlacedFootprint.Footprint.RowPitch = c->rawUploadPitch;
    // The previous frame left RAW in shader-read state. Make the new upload
    // visible before the front-end reads it, including when reusing this core.
    Transition(c, c->raw, D3D12_RESOURCE_STATE_COPY_DEST);
    c->list->CopyTextureRegion(&rawDst, 0, 0, 0, &rawSrc, nullptr);
    if (!Submit(c, nullptr)) return false;

    // ---- stage 1: black level + lens shading + demosaic at full resolution
    ResetList(c);
    c->list->SetDescriptorHeaps(1, heaps);
    c->list->SetComputeRootSignature(c->rootSig);
    c->list->SetPipelineState(c->psoFront);
    c->list->SetComputeRootConstantBufferView(0, c->params.res->GetGPUVirtualAddress());
    c->list->SetComputeRootDescriptorTable(1, c->gpuSrvTable);
    c->list->SetComputeRootDescriptorTable(2, c->gpuUavTable);
    Transition(c, c->raw, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(c, c->rgbFull, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if(p.flags & GPUISP_FLAG_DIRECT)Transition(c,c->rgbOut,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    groupsW = (c->rawW + GS - 1) / GS;
    groupsH = (c->rawH + GS - 1) / GS;
    if (groupsW > 65535u || groupsH > 65535u) { snprintf(c->error, sizeof(c->error), "dispatch grid over 65535"); return false; }
    c->list->Dispatch(groupsW, groupsH, 1);
    if (!Submit(c, &c->msFront)) return false;

    // ---- stage 2: box downscale + AWB + CCM + tone + gamma
    if((p.flags & GPUISP_FLAG_DIRECT)==0u){
    ResetList(c);
    // The heap must be bound before the root descriptor tables that index into it.
    c->list->SetDescriptorHeaps(1, heaps);
    c->list->SetComputeRootSignature(c->rootSig);
    c->list->SetPipelineState(c->psoColor);
    c->list->SetComputeRootConstantBufferView(0, c->params.res->GetGPUVirtualAddress());
    c->list->SetComputeRootDescriptorTable(1, c->gpuSrvTable);
    c->list->SetComputeRootDescriptorTable(2, c->gpuUavTable);
    Transition(c, c->rgbFull, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(c, c->rgbOut, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    groupsW = (c->outW + GS - 1) / GS;
    groupsH = (c->outH + GS - 1) / GS;
    c->list->Dispatch(groupsW, groupsH, 1);
    if (!Submit(c, &c->msColor)) return false;
    }else{c->msColor=0;}

    // ---- stage 3: RGB -> NV12
    ResetList(c);
    c->list->SetDescriptorHeaps(1, heaps);
    c->list->SetComputeRootSignature(c->rootSig);
    c->list->SetPipelineState(c->psoNv12);
    c->list->SetComputeRootConstantBufferView(0, c->params.res->GetGPUVirtualAddress());
    c->list->SetComputeRootDescriptorTable(1, c->gpuSrvTable);
    c->list->SetComputeRootDescriptorTable(2, c->gpuUavTable);
    Transition(c, c->rgbOut, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(c, c->nv12, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    groupsW = (c->outW / 4u + GS - 1) / GS;
    groupsH = (c->outH / 2u + GS2 - 1) / GS2;   // one thread covers a 4x2 block
    c->list->Dispatch(groupsW, groupsH, 1);
    if (!Submit(c, &c->msNv12)) return false;

    // ---- stage 4: luma-only unsharp over the finished NV12 Y plane (optional)
    if (p.flags & GPUISP_FLAG_SHARP)
    {
        ResetList(c);
        c->list->SetDescriptorHeaps(1, heaps);
        c->list->SetComputeRootSignature(c->rootSig);
        c->list->SetPipelineState(PickSharp(c, p));
        c->list->SetComputeRootConstantBufferView(0, c->params.res->GetGPUVirtualAddress());
        c->list->SetComputeRootDescriptorTable(1, c->gpuSrvTable);
        c->list->SetComputeRootDescriptorTable(2, c->gpuUavTable);
        // The readback leaves this scratch in COPY_SOURCE, so every frame from the second on would
        // UAV-write a resource that is not in UNORDERED_ACCESS. nv12 gets its transition above; the
        // two scratches must get theirs here or the live service runs on driver luck forever.
        Transition(c, c->nv12Sharp, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        groupsW = (c->outW / 4u + GS - 1) / GS;
        groupsH = (c->outH + GS - 1) / GS;
        c->list->Dispatch(groupsW, groupsH, 1);
        if (!Submit(c, &c->msSharp)) return false;
    }

    // ---- stage 5: chroma denoise + gain on the finished NV12 UV plane (optional)
    if (p.flags & GPUISP_FLAG_CHROMA_SMOOTH)
    {
        ResetList(c);
        c->list->SetDescriptorHeaps(1, heaps);
        c->list->SetComputeRootSignature(c->rootSig);
        c->list->SetComputeRootConstantBufferView(0, c->params.res->GetGPUVirtualAddress());
        c->list->SetComputeRootDescriptorTable(1, c->gpuSrvTable);
        c->list->SetComputeRootDescriptorTable(2, c->gpuUavTable);
        Transition(c, c->nv12Uv, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        groupsW = (c->outW / 4u + GS - 1) / GS;
        groupsH = (c->outH / 2u + GS - 1) / GS;
        // Three dispatches, one Submit, so the stage keeps its single timing slot and the host's stage
        // report does not change shape. The two guided passes run for a gain or for re-centring: with
        // neither, the binomial already wrote the finished plane, and dispatching them would both cost
        // time and read fields this frame never filled.
        const bool cascade = p.chromaGain > 0.0f || (p.flags & GPUISP_FLAG_CHROMA_RECENTRE) != 0u;
        c->list->SetPipelineState(c->psoChroma);
        c->list->Dispatch(groupsW, groupsH, 1);
        if (cascade)
        {
            UavBarrier(c, c->chromaA.res);
            UavBarrierStat(c, p);
            c->list->SetPipelineState(c->psoGuide1);
            c->list->Dispatch(groupsW, groupsH, 1);
            UavBarrier(c, c->chromaB.res);
            UavBarrierStat(c, p);
            c->list->SetPipelineState(c->psoGuide2);
            c->list->Dispatch(groupsW, groupsH, 1);
        }
        if (!Submit(c, &c->msChroma)) return false;
    }

    // ---- read the NV12 buffer back
    ResetList(c);
    EmitNv12Readback(c, p);
    if (!Submit(c, nullptr)) return false;
    {
        void* ptr = nullptr;
        D3D12_RANGE rR = {0, (UINT)c->nv12Bytes};
        if (FAILED(c->readback.res->Map(0, &rR, &ptr))) { c->setError("readback Map", E_FAIL); return false; }
        memcpy(nv12, ptr, (size_t)c->nv12Bytes);
        D3D12_RANGE empty = {0, 0};
        c->readback.res->Unmap(0, &empty);
    }
    return true;
}

// Perf P1: the whole frame (upload -> 3 dispatches -> readback) in a SINGLE command list with a
// SINGLE fence. GpuIspRun above closes+fences per stage, which forces a cache flush/resolve and a
// DVFS round-trip on every dispatch on the tile-based Adreno, inflating true per-frame latency.
// This path issues identical GPU work (same resources, same transitions, same dispatches); only
// the submission granularity changes, so the GPU==CPU gate output is unchanged.
bool GpuIspRunFused(GpuIspCore* c, const GpuIspParams& p, const GpuIspTone& tone, const float* lscCells,
                    const void* raw, UINT64 rawSizeBytes, void* nv12, double* totalMs)
{
    if (!c || !c->device) { if (c) snprintf(c->error, sizeof(c->error), "core not initialised"); return false; }
    if (!c->psoFront || !c->psoColor || !c->psoNv12 || !c->psoSharp || !c->psoSharpSep || !c->psoChroma ||
        !c->psoGuide1 || !c->psoGuide2)
    { snprintf(c->error, sizeof(c->error), "compute PSOs missing"); return false; }
    if (!CheckGeometry(c, p)) return false;
    if (!AllocateFrame(c, p)) return false;

    // ---- map CPU-side inputs (identical to GpuIspRun)
    {
        void* ptr = nullptr; D3D12_RANGE rW = {0, sizeof(GpuIspParams)}, empty = {0, 0};
        if (FAILED(c->params.res->Map(0, &rW, &ptr))) { c->setError("params Map", E_FAIL); return false; }
        memcpy(ptr, &p, sizeof(p)); c->params.res->Unmap(0, &empty);
    }
    {
        UINT32 cells = (p.lscCols * p.lscRows) ? (p.lscCols * p.lscRows) : 1u;
        void* ptr = nullptr; D3D12_RANGE rW = {0, cells * 16u}, empty = {0, 0};
        if (FAILED(c->lsc.res->Map(0, &rW, &ptr))) { c->setError("LSC Map", E_FAIL); return false; }
        memcpy(ptr, lscCells, (size_t)cells * 16u); c->lsc.res->Unmap(0, &empty);
    }
    {
        void* ptr = nullptr; D3D12_RANGE rW = {0, (UINT)sizeof(GpuIspTone)}, empty = {0, 0};
        if (FAILED(c->tone.res->Map(0, &rW, &ptr))) { c->setError("tone Map", E_FAIL); return false; }
        memcpy(ptr, tone.lut, sizeof(GpuIspTone)); c->tone.res->Unmap(0, &empty);
    }
    if (rawSizeBytes > (UINT64)c->rawUploadPitch * c->rawH) { snprintf(c->error, sizeof(c->error), "RAW larger than the upload buffer"); return false; }
    {
        void* ptr = nullptr; D3D12_RANGE rW = {0, (UINT)((UINT64)c->rawUploadPitch * c->rawH)}, empty = {0, 0};
        if (FAILED(c->uploadRaw.res->Map(0, &rW, &ptr))) { c->setError("RAW upload Map", E_FAIL); return false; }
        UINT8* dst = (UINT8*)ptr; const UINT8* src = (const UINT8*)raw; UINT32 srcPitch = c->rawW * 2u;
        for (UINT32 y = 0; y < c->rawH; y++)
        {
            UINT64 remaining = rawSizeBytes - (UINT64)y * srcPitch;
            if (remaining <= 0) break;
            size_t n = (remaining < srcPitch) ? (size_t)remaining : (size_t)srcPitch;
            memcpy(dst + (UINT64)y * c->rawUploadPitch, src + (UINT64)y * srcPitch, n);
        }
        c->uploadRaw.res->Unmap(0, &empty);
    }

    ID3D12DescriptorHeap* heaps[] = { c->heap };
    UINT32 groupsW, groupsH;

    ResetList(c);
    c->list->SetDescriptorHeaps(1, heaps);

    // stage 0: upload copy
    D3D12_TEXTURE_COPY_LOCATION rawDst, rawSrc;
    memset(&rawDst, 0, sizeof(rawDst)); memset(&rawSrc, 0, sizeof(rawSrc));
    rawDst.pResource = c->raw.res; rawDst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; rawDst.SubresourceIndex = 0;
    rawSrc.pResource = c->uploadRaw.res; rawSrc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    rawSrc.PlacedFootprint.Offset = 0;
    rawSrc.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R16_UINT;
    rawSrc.PlacedFootprint.Footprint.Width = c->rawW; rawSrc.PlacedFootprint.Footprint.Height = c->rawH;
    rawSrc.PlacedFootprint.Footprint.Depth = 1; rawSrc.PlacedFootprint.Footprint.RowPitch = c->rawUploadPitch;
    // A changing sensor frame must replace the complete texture before any
    // shader reads it; the previous dispatch left this in shader-read state.
    Transition(c, c->raw, D3D12_RESOURCE_STATE_COPY_DEST);
    c->list->CopyTextureRegion(&rawDst, 0, 0, 0, &rawSrc, nullptr);

    // stage 1: front-end (black + LSC + demosaic, full res)
    c->list->SetComputeRootSignature(c->rootSig);
    c->list->SetPipelineState(c->psoFront);
    c->list->SetComputeRootConstantBufferView(0, c->params.res->GetGPUVirtualAddress());
    c->list->SetComputeRootDescriptorTable(1, c->gpuSrvTable);
    c->list->SetComputeRootDescriptorTable(2, c->gpuUavTable);
    Transition(c, c->raw, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(c, c->rgbFull, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if(p.flags & GPUISP_FLAG_DIRECT)Transition(c,c->rgbOut,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    groupsW = (c->rawW + GS - 1) / GS; groupsH = (c->rawH + GS - 1) / GS;
    if (groupsW > 65535u || groupsH > 65535u) { snprintf(c->error, sizeof(c->error), "dispatch grid over 65535"); return false; }
    c->list->Dispatch(groupsW, groupsH, 1);

    // stage 2: colour (AWB + CCM + tone + gamma) + downscale
    if((p.flags & GPUISP_FLAG_DIRECT)==0u){
    c->list->SetPipelineState(c->psoColor);
    Transition(c, c->rgbFull, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(c, c->rgbOut, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    groupsW = (c->outW + GS - 1) / GS; groupsH = (c->outH + GS - 1) / GS;
    c->list->Dispatch(groupsW, groupsH, 1);

    // stage 3: RGB -> NV12 (+ bilateral denoise inside)
    }
    c->list->SetPipelineState(c->psoNv12);
    Transition(c, c->rgbOut, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(c, c->nv12, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    groupsW = (c->outW / 4u + GS - 1) / GS; groupsH = (c->outH / 2u + GS2 - 1) / GS2;
    c->list->Dispatch(groupsW, groupsH, 1);

    // stage 4: luma-only unsharp over the finished NV12 Y plane (optional)
    if (p.flags & GPUISP_FLAG_SHARP)
    {
        // nv12 stays UNORDERED_ACCESS across stage 3 -> stage 4, so the implicit UAV flush
        // that every other stage boundary gets from its state transition does not happen here.
        D3D12_RESOURCE_BARRIER uv;
        memset(&uv, 0, sizeof(uv));
        uv.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        uv.UAV.pResource = c->nv12.res;
        c->list->ResourceBarrier(1, &uv);

        c->list->SetPipelineState(PickSharp(c, p));
        Transition(c, c->nv12Sharp, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        groupsW = (c->outW / 4u + GS - 1) / GS; groupsH = (c->outH + GS - 1) / GS;
        c->list->Dispatch(groupsW, groupsH, 1);
    }

    // stage 5: chroma denoise + gain on the finished NV12 UV plane (optional)
    if (p.flags & GPUISP_FLAG_CHROMA_SMOOTH)
    {
        // Same reason as stage 4's barrier: nv12 was written by stage 3 inside this list and stays
        // UNORDERED_ACCESS, so the implicit flush a state transition would give does not happen.
        D3D12_RESOURCE_BARRIER cw;
        memset(&cw, 0, sizeof(cw));
        cw.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        cw.UAV.pResource = c->nv12.res;
        c->list->ResourceBarrier(1, &cw);

        // Stage 5's luma guide is the Y plane that ships, so when sharpen ran it reads the scratch stage
        // 4 wrote in this same list - which needs its own UAV barrier, not just nv12's.
        if (p.flags & GPUISP_FLAG_SHARP)
        {
            D3D12_RESOURCE_BARRIER sy;
            memset(&sy, 0, sizeof(sy));
            sy.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            sy.UAV.pResource = c->nv12Sharp.res;
            c->list->ResourceBarrier(1, &sy);
        }

        c->list->SetPipelineState(c->psoChroma);
        Transition(c, c->nv12Uv, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        groupsW = (c->outW / 4u + GS - 1) / GS;
        groupsH = (c->outH / 2u + GS - 1) / GS;
        c->list->Dispatch(groupsW, groupsH, 1);
        const bool cascade = p.chromaGain > 0.0f || (p.flags & GPUISP_FLAG_CHROMA_RECENTRE) != 0u;
        if (cascade)
        {
            UavBarrier(c, c->chromaA.res);
            UavBarrierStat(c, p);
            c->list->SetPipelineState(c->psoGuide1);
            c->list->Dispatch(groupsW, groupsH, 1);
            UavBarrier(c, c->chromaB.res);
            UavBarrierStat(c, p);
            c->list->SetPipelineState(c->psoGuide2);
            c->list->Dispatch(groupsW, groupsH, 1);
        }
    }

    // readback copy
    EmitNv12Readback(c, p);

    // ONE submit, ONE fence for the entire frame
    if (!Submit(c, totalMs)) return false;

    {
        void* ptr = nullptr; D3D12_RANGE rR = {0, (UINT)c->nv12Bytes}, empty = {0, 0};
        if (FAILED(c->readback.res->Map(0, &rR, &ptr))) { c->setError("readback Map", E_FAIL); return false; }
        memcpy(nv12, ptr, (size_t)c->nv12Bytes);
        c->readback.res->Unmap(0, &empty);
    }
    return true;
}

// 16-bit half -> 32-bit float, used only for the full-resolution RGB debug readback.
static float HalfToFloat(UINT16 h)
{
    UINT32 sign = (h & 0x8000u) ? 0x80000000u : 0u;
    UINT32 exp = (h >> 10) & 0x1Fu;
    UINT32 man = h & 0x3FFu;
    UINT32 out;
    if (exp == 0)
    {
        if (man == 0) { out = sign; }
        else
        {
            exp = 127 - 15 + 1;
            while (!(man & 0x400u)) { man <<= 1; exp--; }
            man &= 0x3FFu;
            out = sign | (exp << 23) | (man << 13);
        }
    }
    else if (exp == 0x1F)
    {
        out = sign | 0x7F800000u | (man << 13);
    }
    else
    {
        out = sign | ((exp + 127 - 15) << 23) | (man << 13);
    }
    union { UINT32 u; float f; } cv;
    cv.u = out;
    return cv.f;
}

static bool ReadRgba16f(GpuIspCore* c, ResourceBox& box, UINT w, UINT h, float* rgbaOut, UINT64 outSizeBytes)
{
    if (!box.res) return false;
    UINT64 need = (UINT64)w * h * 8ull;
    if (outSizeBytes < need) { snprintf(c->error, sizeof(c->error), "RGB readback buffer too small"); return false; }

    D3D12_RESOURCE_DESC d = box.res->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp;
    memset(&fp, 0, sizeof(fp));
    UINT numRows = 0;
    UINT64 rowSizeBytes = 0, totalBytes = 0;
    // GetCopyableFootprints returns void; the out params are the answer.
    c->device->GetCopyableFootprints(&d, 0, 1, 0, &fp, &numRows, &rowSizeBytes, &totalBytes);
    if (fp.Footprint.Format != DXGI_FORMAT_R16G16B16A16_FLOAT)
    { snprintf(c->error, sizeof(c->error), "unexpected readback footprint format"); return false; }

    D3D12_RESOURCE_DESC bd = BufDesc((totalBytes + 255u) & ~255ull, D3D12_RESOURCE_FLAG_NONE);
    ID3D12Resource* rb = nullptr;
    HRESULT hr = CreateCommitted(c, bd, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST, &rb);
    if (FAILED(hr)) { c->setError("alloc RGBA16F readback", hr); return false; }

    ResetList(c);
    Transition(c, box, D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION dst, src;
    memset(&dst, 0, sizeof(dst));
    dst.pResource = rb;
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint = fp;
    memset(&src, 0, sizeof(src));
    src.pResource = box.res;
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.SubresourceIndex = 0;
    c->list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    if (!Submit(c, nullptr)) { rb->Release(); return false; }

    void* ptr = nullptr;
    D3D12_RANGE rR = { 0, (UINT)totalBytes };
    hr = rb->Map(0, &rR, &ptr);
    if (FAILED(hr)) { c->setError("RGBA16F readback Map", hr); rb->Release(); return false; }
    // The footprint row pitch is 256-byte aligned, so walk it row by row rather than linearly.
    const BYTE* base = (const BYTE*)ptr + fp.Offset;
    for (UINT y = 0; y < numRows; y++)
    {
        const UINT16* row16 = (const UINT16*)(base + (size_t)y * fp.Footprint.RowPitch);
        for (UINT x = 0; x < w; x++)
            for (int ch = 0; ch < 4; ch++)
                rgbaOut[((size_t)y * w + x) * 4 + ch] = HalfToFloat(row16[x * 4 + ch]);
    }
    D3D12_RANGE empty = {0, 0};
    rb->Unmap(0, &empty);
    rb->Release();
    return true;
}

bool GpuIspReadFullRgb(GpuIspCore* c, float* rgbaOut, UINT64 outSizeBytes)
{
    if (!c) return false;
    return ReadRgba16f(c, c->rgbFull, c->rawW, c->rawH, rgbaOut, outSizeBytes);
}

bool GpuIspReadPreviewRgb(GpuIspCore* c, float* rgbaOut, UINT64 outSizeBytes)
{
    if (!c) return false;
    return ReadRgba16f(c, c->rgbOut, c->outW, c->outH, rgbaOut, outSizeBytes);
}
