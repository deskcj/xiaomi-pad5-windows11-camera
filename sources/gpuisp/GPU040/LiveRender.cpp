// BuildC003 - render a SAVED live RAW snapshot through the exact live pipeline, offline.
// The live service (GpuCameraPump in serve mode) derives AWB / exposure gain / CCM per frame from the
// RAW itself (UpdateFactoryIq) and then runs the fused GPU path. The bench does none of that: it uses the
// static awb/exgain written in the params file. So a bench render of a live snapshot is NOT the picture the
// service shows - on the saved front frame the static preset lands 21 codes of V off the derived value,
// which reads as a pink cast. This tool reproduces the service's parameter assembly line for line so the
// delivered frames are honest, and --static-iq turns the derivation off to measure the difference.
#define main ArchivedLiveRenderBenchMain
#include "bench/src/GpuIspBench.cpp"
#undef main
#include <algorithm>
#include <mmreg.h>
#include <unknwn.h>
#include <ks.h>
#include <ksmedia.h>
#define CODEX_GPU_CLIENT 1
#include "src/GpuBridgeProtocol.h"
#include "FactoryLiveIq.h"

static std::string Utf8(const char* p) { return std::string(p); }

#ifndef LIVE_RENDER_NO_MAIN
int main(int argc, char** argv)
{
    if (argc < 5) {
        printf("LiveRender front|rear RAW PARAMS OUT [--out-w N] [--out-h N] [--raw-w N] [--raw-h N] [--pattern P] [--rotate 0|1] [--nv12 P] [--static-iq] [--cpu] [--repeat N] [--crop X Y W H]\n");
        return 2;
    }
    const bool front = strcmp(argv[1], "front") == 0;
    if (!front && strcmp(argv[1], "rear")) return 2;
    AndroidParams ap;
    if (!LoadParams(Utf8(argv[3]), ap)) return 7;
    std::vector<UINT8> raw;
    if (!ReadFile(Utf8(argv[2]), raw)) return 3;
    UINT32 inW = front ? 1632 : 1280, inH = front ? 1224 : 960;
    UINT32 outW = 1280, outH = front ? 960 : 948;
    int pattern = front ? 0 : 1;
    bool rotate = front, staticIq = false, useCpu = false;
    int repeat = 1;
    UINT32 cropX = 0, cropY = 0, cropW = 0, cropH = 0;   // zoom window in RAW pixels; 0 means the whole frame
    std::string outNv12;
    for (int i = 5; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&](const char* d) { return i + 1 < argc ? argv[++i] : (char*)d; };
        if (a == "--out-w") outW = (UINT32)atoi(next("1280"));
        else if (a == "--out-h") outH = (UINT32)atoi(next("960"));
        else if (a == "--raw-w") inW = (UINT32)atoi(next("1280"));
        else if (a == "--raw-h") inH = (UINT32)atoi(next("960"));
        else if (a == "--pattern") pattern = atoi(next("0"));
        else if (a == "--rotate") rotate = atoi(next("0")) != 0;
        else if (a == "--nv12") outNv12 = next("");
        else if (a == "--static-iq") staticIq = true;
        else if (a == "--cpu") useCpu = true;
        else if (a == "--repeat") repeat = atoi(next("1"));
        else if (a == "--crop") { cropX = (UINT32)atoi(next("0")); cropY = (UINT32)atoi(next("0"));
                                  cropW = (UINT32)atoi(next("0")); cropH = (UINT32)atoi(next("0")); }
    }
    if (raw.size() != (size_t)inW * inH * 2) {
        printf("ERROR: raw size %zu != %ux%u x 2 bytes\n", raw.size(), inW, inH);
        return 4;
    }

    // Exactly the pump's parameter assembly (GpuCameraPump.cpp lines 100-116).
    GpuIspParams p = {};
    p.rawW = inW; p.rawH = inH; p.outW = outW; p.outH = outH;
    p.cropX = cropX; p.cropY = cropY;
    p.cropW = cropW ? cropW : p.rawW;
    p.cropH = cropH ? cropH : p.rawH;
    p.lscCols = ap.lscCols; p.lscRows = ap.lscRows;
    p.blackLevel = ap.black; p.whiteLevel = ap.white; p.exGain = ap.exGain; p.gamma = 2.2f;
    memcpy(p.awb, ap.awb, sizeof(p.awb));
    for (int i = 0; i < 9; i++) GpuIspCcm(p, i) = ap.ccm[i];
    p.flags = GPUISP_FLAG_AWB | GPUISP_FLAG_CCM | GPUISP_FLAG_TONE;
    if (p.rawW == p.outW && p.rawH == p.outH) p.flags |= GPUISP_FLAG_DIRECT;
    if (p.lscCols > 1 && p.lscRows > 1) p.flags |= GPUISP_FLAG_LSC;
    p.denoise = ap.denoise; if (p.denoise > 0) p.flags |= GPUISP_FLAG_DENOISE;
    p.sharpen = ap.sharpen;
    p.sharpenRadius = ap.sharpenRadius > 0 ? ap.sharpenRadius : 1;
    p.sharpenLimit = ap.sharpenLimit >= 0 ? ap.sharpenLimit : 24;
    p.sharpenThreshold = ap.sharpenThr >= 0 ? ap.sharpenThr : 0;
    GpuIspSetEdgeProtect(p, ap.sharpenEdgeLo >= 0 ? ap.sharpenEdgeLo : 0, ap.sharpenEdgeHi >= 0 ? ap.sharpenEdgeHi : 0);
    // Signal-dependent bilateral luma width: without this call nr_range_* in a params file is inert
    // on the live and replay paths, and the stage keeps the uniform 1/Denoise reciprocal.
    if (ap.nrRangeA >= 0.0f && ap.nrRangeB > 0.0f) GpuIspSetNrRange(p, ap.nrRangeA, ap.nrRangeB);
    printf("nr_range a=%.4f b=%.4f (%s)\n", p.nrRangeA, p.nrRangeB,
           p.nrRangeB > 0.0f ? "signal-dependent" : "uniform 1/denoise");
    if (p.sharpen > 0) p.flags |= GPUISP_FLAG_SHARP;
    if (ap.chromaSmooth > 0) p.flags |= GPUISP_FLAG_CHROMA_SMOOTH;
    if (ap.chromaRecentre > 0) p.flags |= GPUISP_FLAG_CHROMA_RECENTRE;
    p.chromaGain = ap.chromaGain; p.chromaBiasU = ap.chromaBiasU; p.chromaBiasV = ap.chromaBiasV;
    // Without this call a chroma_sigma in a params file is inert on the live and replay paths, exactly
    // the mistake nr_range made until BuildC006 fixed the parser.
    GpuIspSetChromaSigma(p, ap.chromaSigma);
    p.cfaOff = ((pattern == 2 || pattern == 3) ? 1u : 0u) | ((pattern == 1 || pattern == 3) ? 2u : 0u);
    GpuIspSetYuvMatrix(p, 0);

    CODEX_GPU_INFO info = {};
    info.RawWidth = inW; info.RawHeight = inH; info.Pattern = (ULONG)pattern; info.Rotate180 = rotate ? 1 : 0;

    LiveIqState iq;
    if (!staticIq) UpdateFactoryIq(front, info, (const UINT16*)raw.data(), ap, p, iq);
    else printf("iq STATIC (preset awb %.4f/%.4f exgain %.4f)\n", p.awb[0], p.awb[3], p.exGain);

    GpuIspTone tone;
    BuildToneLut(ap, tone);

    MEMORYSTATUSEX mem = { sizeof(mem) };
    GlobalMemoryStatusEx(&mem);
    printf("free_ram_mb=%llu\n", (unsigned long long)(mem.ullAvailPhys / 1048576));
    std::vector<UINT8> nv12((size_t)GpuIspNv12SizeBytes(p.outW, p.outH));
    double ms = 0;
    // --cpu runs the C++ mirror of the same arithmetic (GpuIspRef), which the GPU==CPU parity gate holds to
    // +-1 code. It exists because this pad often has under 600 MB free while agents are working, and the
    // shading-table A/B is a relative measurement that does not need the adapter.
    if (useCpu) {
        GpuIspRefOutput ro;
        if (!GpuIspRefRun(p, ap.lsc.data(), tone, (const UINT16*)raw.data(), ro, nv12.data())) {
            printf("ERROR: CPU reference pipeline failed\n");
            return 14;
        }
        printf("cpu mirror ms=%.3f  out=%ux%u pattern=%d rotate=%d\n", 0.0, p.outW, p.outH, pattern, rotate ? 1 : 0);
    } else {
        if (mem.ullAvailPhys < 600ull * 1048576) { printf("ERROR: RAM gate 600 MB\n"); return 6; }
        GpuIspCore* core = GpuIspCreate(0, false);
        if (!core) { printf("ERROR: GPU create failed\n"); return 8; }
        std::vector<double> times;
        bool ok = true;
        for (int r = 0; r < (repeat < 1 ? 1 : repeat) && ok; ++r) {
            ok = GpuIspRunFused(core, p, tone, ap.lsc.data(), raw.data(), (UINT32)raw.size(), nv12.data(), &ms);
            if (ok) times.push_back(ms);
        }
        if (!ok) {
            printf("ERROR: GPU pipeline: %s\n", GpuIspLastError(core));
            GpuIspDestroy(core);
            return 13;
        }
        // The first fused call uploads the shaders, so it is not a frame the service ever shows; report the
        // warm median separately and only use that for fps.
        printf("fused gpu_ms=%.3f", times.front());
        if (times.size() > 1) {
            std::vector<double> warm(times.begin() + 1, times.end());
            std::sort(warm.begin(), warm.end());
            double med = warm[warm.size() / 2];
            printf(" warm_med_ms=%.3f warm_fps=%.3f n=%d", med, 1000.0 / med, (int)warm.size());
        }
        printf("  out=%ux%u pattern=%d rotate=%d\n", p.outW, p.outH, pattern, rotate ? 1 : 0);
        GpuIspDestroy(core);
    }
    if (!WritePngFromNv12(Utf8(argv[4]), 0, nv12.data(), p.outW, p.outH, GpuIspUVRowPitch(p.outW), GpuIspUVRowPitch(p.outW), rotate))
        printf("ERROR: png write failed\n");
    else printf("wrote %s\n", argv[4]);
    if (!outNv12.empty() && !WriteFile(outNv12, nv12.data(), nv12.size())) printf("ERROR: nv12 write failed\n");
    return 0;
}
#endif
