// GpuIspBench.cpp - GPUISP2 offline harness.
//
// Runs the ISP on real Android ground truth that already sits on D: (no tablet needed):
//   paired-*-raw-plane0.bin  -> the sensor RAW of one frame
//   paired-*-yuv-i420.yuv    -> what Android's own ISP produced from that same frame
//   paired-*-result.json     -> the IQ values Android applied to that same frame
//
// It can drive the GPU pipeline, the CPU reference, or both, and reports GPU==CPU agreement plus
// the usual quality metrics against Android (MAE, PSNR, luma, LapVar sharpness, clipping).
// Output is NV12 plus a 24-bit BMP so the owner can judge the picture himself.
#include "GpuIsp.h"
#include "GpuIspRef.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <string>
#include <vector>

namespace
{

struct Options
{
    std::string rawPath, paramsPath, refYuvPath, outNv12, outCpuNv12, outBmp, outPng;
    UINT32 rawW = 0, rawH = 0, rawPitch = 0;
    UINT32 outW = 0, outH = 0;
    UINT32 cropX = 0, cropY = 0, cropW = 0, cropH = 0;
    UINT32 refW = 0, refH = 0;
    int pattern = GPUISP_BGGR;
    bool patternGiven = false;
    int yuvMode = 0;
    int adapter = 0;
    int repeat = 5;
    bool useGpu = true;
    bool useCpu = false;
    bool debug = false;
    bool checkFront = false;
    bool rotate180 = false; // Presentation only; RAW/CFA and NV12 parity remain in sensor space.
    bool direct = false;
    bool noLsc = false, noTone = false, noCcm = false, noAwb = false, gammaOn = false;
    float gamma = 2.2f, exGain = -1.0f;   // < 0: take the value from the params file
    float denoise = -1.0f;           // < 0: take the value from the params file
    float sharpen = -1.0f;           // < 0: take the value from the params file
    float sharpenRadius = -1.0f;     // < 0: take the value from the params file
    float sharpenThr = -1.0f;        // < 0: take the value from the params file
    float sharpenLimit = -1.0f;      // < 0: take the value from the params file
    float sharpenEdgeLo = -1.0f;     // edge protection start, codes; < 0: params file
    float sharpenEdgeHi = -1.0f;     // edge protection zero point, codes; < 0: params file
    float nrRangeA = -1.0f;          // bilateral 1/range at gamma luma 0; < 0: params file
    float nrRangeB = -1.0f;          // its slope per unit gamma luma; <= 0 keeps the uniform width
    int chromaSmooth = -1;           // 1 = the finished UV plane gets stage 5; < 0: params file
    float chromaGain = -1.0f;        // stage 5's saturation gain about grey; <= 0 keeps it off; < 0: params file
    float chromaSigma = -1.0f;       // stage 5's guided luma width in codes; < 0: params file, 0: the shipped 20
    int chromaRecentre = -1;         // 1 = stage 5 also re-centres the chroma neutral point; < 0: params file
    float chromaBiasU = 0.0f;        // grey target: a static U bias, in codes, added after the re-centring
    int chromaBiasUSet = 0;          // 1 = the command line gave it, so it beats the params file
    float chromaBiasV = 0.0f;        // grey target: the same term on V (the rear's own gap lives here)
    int chromaBiasVSet = 0;
    std::string lscOrder = "RGG B";
};

struct AndroidParams
{
    float black = 0, white = 1023;
    float awb[4] = { 1, 1, 1, 1 };
    float ccm[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
    UINT32 lscCols = 0, lscRows = 0;
    std::vector<float> lsc;          // cols*rows*4 in (R,Gr,Gb,B)
    std::vector<float> toneX;        // normalised x
    std::vector<float> toneY[3];     // r,g,b
    float neutral[3] = { 0, 0, 0 };
    float exposureNs = 0, iso = 0;
    float exGain = 1.0f;             // whole-frame exposure multiplier applied after black level
    float denoise = 0;               // bilateral luma width, 0 = stage off
    float sharpen = 0;               // unsharp amount, 0 = stage off
    float sharpenRadius = -1.0f;     // 1..4 -> binomial row 2r; -1 = take the params file value
    float sharpenThr = -1.0f;        // soft deadzone, in 8-bit luma codes; -1 = not in the file
    float sharpenLimit = -1.0f;      // halo clamp, in codes; -1 = not in the file
    float sharpenEdgeLo = -1.0f;     // edge protection start, codes; -1 = not in the file
    float sharpenEdgeHi = -1.0f;     // edge protection zero point, codes; -1 = not in the file
    float nrRangeA = -1.0f;          // bilateral 1/range at gamma luma 0; -1 = not in the file
    float nrRangeB = -1.0f;          // its slope per unit gamma luma; -1 = not in the file
    float chromaSmooth = 0;          // 1 = smooth the finished UV plane; absent = off
    float chromaGain = 0;            // UV saturation gain about grey, needs chroma_smooth; absent = off
    float chromaSigma = 0;           // guided chroma luma width, codes; absent = 0 = the shipped 20
    float chromaRecentre = 0;        // 1 = re-centre the UV neutral point too, needs chroma_smooth
    float chromaBiasU = 0;           // grey target on U, codes; absent = 0, which is the shipped behaviour
    float chromaBiasV = 0;           // grey target on V, codes; the front ships 0, the rear's gap is here
    int pattern = -1;                // mosaic as recorded with the IQ (-1: take --pattern)
};

bool ReadFile(const std::string& path, std::vector<UINT8>& out)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) { printf("ERROR: cannot open %s\n", path.c_str()); return false; }
    fseek(f, 0, SEEK_END);
    long long sz = _ftelli64(f);
    fseek(f, 0, SEEK_SET);
    out.resize((size_t)sz);
    size_t got = fread(out.data(), 1, (size_t)sz, f);
    fclose(f);
    if ((long long)got != sz) { printf("ERROR: short read on %s\n", path.c_str()); return false; }
    return true;
}

bool WriteFile(const std::string& path, const void* data, size_t size)
{
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) { printf("ERROR: cannot write %s\n", path.c_str()); return false; }
    size_t w = fwrite(data, 1, size, f);
    fclose(f);
    if (w != size) { printf("ERROR: short write on %s\n", path.c_str()); return false; }
    return true;
}

// ---------------------------------------------------------------- params file

bool LoadParams(const std::string& path, AndroidParams& a)
{
    FILE* f = fopen(path.c_str(), "rt");
    if (!f) { printf("ERROR: cannot open params %s\n", path.c_str()); return false; }
    char line[4096];
    int section = 0;      // 0 = key/value, 1 = lsc rows, 2 = tone rows
    while (fgets(line, sizeof(line), f))
    {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
        char key[64];
        if (sscanf(line, "%63s", key) != 1) continue;
        if (!strcmp(key, "lsc_data")) { section = 1; continue; }
        if (!strcmp(key, "tone_data")) { section = 2; continue; }
        // Table rows always start with a number, so a non-numeric token ends the block. Without
        // this a key appended after lsc_data is consumed as a malformed row and never applied.
        if (section != 0)
        {
            char c = key[0];
            if (!((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.')) section = 0;
        }
        if (section == 1)
        {
            float v0, v1, v2, v3;
            if (sscanf(line, "%f %f %f %f", &v0, &v1, &v2, &v3) == 4)
            { a.lsc.push_back(v0); a.lsc.push_back(v1); a.lsc.push_back(v2); a.lsc.push_back(v3); }
            continue;
        }
        if (section == 2)
        {
            float x, r, g, b;
            if (sscanf(line, "%f %f %f %f", &x, &r, &g, &b) == 4)
            { a.toneX.push_back(x); a.toneY[0].push_back(r); a.toneY[1].push_back(g); a.toneY[2].push_back(b); }
            continue;
        }
        if (!strcmp(key, "black")) sscanf(line, "%*s %f", &a.black);
        else if (!strcmp(key, "white")) sscanf(line, "%*s %f", &a.white);
        else if (!strcmp(key, "awb")) sscanf(line, "%*s %f %f %f %f", &a.awb[0], &a.awb[1], &a.awb[2], &a.awb[3]);
        else if (!strcmp(key, "ccm")) sscanf(line, "%*s %f %f %f %f %f %f %f %f %f",
                                             &a.ccm[0], &a.ccm[1], &a.ccm[2], &a.ccm[3], &a.ccm[4],
                                             &a.ccm[5], &a.ccm[6], &a.ccm[7], &a.ccm[8]);
        else if (!strcmp(key, "lsc")) sscanf(line, "%*s %u %u", &a.lscCols, &a.lscRows);
        else if (!strcmp(key, "neutral")) sscanf(line, "%*s %f %f %f", &a.neutral[0], &a.neutral[1], &a.neutral[2]);
        else if (!strcmp(key, "exposure")) sscanf(line, "%*s %f %f", &a.exposureNs, &a.iso);
        else if (!strcmp(key, "exgain")) sscanf(line, "%*s %f", &a.exGain);
        else if (!strcmp(key, "denoise")) sscanf(line, "%*s %f", &a.denoise);
        else if (!strcmp(key, "sharpen")) sscanf(line, "%*s %f", &a.sharpen);
        else if (!strcmp(key, "sharpen_radius")) sscanf(line, "%*s %f", &a.sharpenRadius);
        else if (!strcmp(key, "sharpen_thr")) sscanf(line, "%*s %f", &a.sharpenThr);
        else if (!strcmp(key, "sharpen_limit")) sscanf(line, "%*s %f", &a.sharpenLimit);
        else if (!strcmp(key, "sharpen_edge_lo")) sscanf(line, "%*s %f", &a.sharpenEdgeLo);
        else if (!strcmp(key, "sharpen_edge_hi")) sscanf(line, "%*s %f", &a.sharpenEdgeHi);
        else if (!strcmp(key, "nr_range_a")) sscanf(line, "%*s %f", &a.nrRangeA);
        else if (!strcmp(key, "nr_range_b")) sscanf(line, "%*s %f", &a.nrRangeB);
        else if (!strcmp(key, "chroma_smooth")) sscanf(line, "%*s %f", &a.chromaSmooth);
        else if (!strcmp(key, "chroma_gain")) sscanf(line, "%*s %f", &a.chromaGain);
        else if (!strcmp(key, "chroma_sigma")) sscanf(line, "%*s %f", &a.chromaSigma);
        else if (!strcmp(key, "chroma_recentre")) sscanf(line, "%*s %f", &a.chromaRecentre);
        else if (!strcmp(key, "chroma_bias_u")) sscanf(line, "%*s %f", &a.chromaBiasU);
        else if (!strcmp(key, "chroma_bias_v")) sscanf(line, "%*s %f", &a.chromaBiasV);
        else if (!strcmp(key, "pattern")) sscanf(line, "%*s %d", &a.pattern);
    }
    fclose(f);
    if (a.lscCols * a.lscRows * 4 != a.lsc.size())
    {
        printf("ERROR: LSC cell count mismatch (header %u x %u, rows %zu)\n", a.lscCols, a.lscRows, a.lsc.size() / 4);
        return false;
    }
    return true;
}

void BuildToneLut(const AndroidParams& a, GpuIspTone& t)
{
    for (int i = 0; i < GPUISP_TONE_LUT; i++)
        for (int ch = 0; ch < 3; ch++) t.lut[ch * GPUISP_TONE_LUT + i] = (float)i / (float)(GPUISP_TONE_LUT - 1);
    if (a.toneX.size() < 2) return;
    for (int ch = 0; ch < 3; ch++)
    {
        for (int i = 0; i < GPUISP_TONE_LUT; i++)
        {
            float x = (float)i / (float)(GPUISP_TONE_LUT - 1);
            size_t k = 0;
            while (k + 2 < a.toneX.size() && a.toneX[k + 1] < x) k++;
            float x0 = a.toneX[k], x1 = a.toneX[k + 1];
            float y0 = a.toneY[ch][k], y1 = a.toneY[ch][k + 1];
            float f = (x1 > x0) ? (x - x0) / (x1 - x0) : 0.0f;
            f = f < 0 ? 0 : (f > 1 ? 1 : f);
            t.lut[ch * GPUISP_TONE_LUT + i] = y0 + (y1 - y0) * f;
        }
    }
}

// ---------------------------------------------------------------- raw loading

bool LoadRaw(const Options& o, std::vector<UINT16>& tight)
{
    std::vector<UINT8> buf;
    if (!ReadFile(o.rawPath, buf)) return false;
    UINT32 pitch = o.rawPitch ? o.rawPitch : o.rawW * 2u;
    size_t need = (size_t)pitch * o.rawH;
    if (buf.size() < need)
    {
        printf("ERROR: RAW file %zu bytes, expected at least %zu for %ux%u pitch %u\n",
               buf.size(), need, o.rawW, o.rawH, pitch);
        return false;
    }
    tight.assign((size_t)o.rawW * o.rawH, 0);
    const UINT16* src = (const UINT16*)buf.data();
    for (UINT32 y = 0; y < o.rawH; y++)
        memcpy(&tight[(size_t)y * o.rawW], (const UINT8*)src + (size_t)y * pitch, (size_t)o.rawW * 2);
    return true;
}

// ---------------------------------------------------------------- i420 reference

bool LoadI420(const std::string& path, UINT32 w, UINT32 h, std::vector<UINT8>& y,
              std::vector<UINT8>& u, std::vector<UINT8>& v)
{
    std::vector<UINT8> buf;
    if (!ReadFile(path, buf)) return false;
    size_t yPlane = (size_t)w * h, cPlane = (size_t)(w / 2) * (h / 2);
    if (buf.size() < yPlane + 2 * cPlane)
    {
        printf("ERROR: reference YUV %zu bytes, needs %zu for %ux%u i420\n", buf.size(),
               yPlane + 2 * cPlane, w, h);
        return false;
    }
    y.assign(buf.begin(), buf.begin() + yPlane);
    u.assign(buf.begin() + yPlane, buf.begin() + yPlane + cPlane);
    v.assign(buf.begin() + yPlane + cPlane, buf.begin() + yPlane + 2 * cPlane);
    return true;
}

// Box-downsample a planar 8-bit image to the target size (used only to align the reference).
void BoxResizeU8(const std::vector<UINT8>& src, UINT32 sw, UINT32 sh, std::vector<UINT8>& dst,
                 UINT32 dw, UINT32 dh)
{
    dst.assign((size_t)dw * dh, 0);
    for (UINT32 oy = 0; oy < dh; oy++)
        for (UINT32 ox = 0; ox < dw; ox++)
        {
            UINT32 x0 = (ox * sw) / dw, x1 = ((ox + 1) * sw) / dw;
            UINT32 y0 = (oy * sh) / dh, y1 = ((oy + 1) * sh) / dh;
            if (x1 <= x0) x1 = x0 + 1;
            if (y1 <= y0) y1 = y0 + 1;
            UINT32 acc = 0, n = 0;
            for (UINT32 y = y0; y < y1 && y < sh; y++)
                for (UINT32 x = x0; x < x1 && x < sw; x++) { acc += src[(size_t)y * sw + x]; n++; }
            dst[(size_t)oy * dw + ox] = n ? (UINT8)(acc / n) : 0;
        }
}

// ---------------------------------------------------------------- metrics

struct PlaneMetrics
{
    double mae = 0, psnr = 0, mean = 0, refMean = 0, lapVar = 0;
    int haveRef = 0;
};

double LapVar(const std::vector<UINT8>& img, UINT32 w, UINT32 h, UINT32 pitch)
{
    // Laplacian variance on the 4-neighbour kernel, sampled over the whole plane.
    double sum = 0, sum2 = 0;
    long long n = 0;
    for (UINT32 y = 1; y + 1 < h; y++)
        for (UINT32 x = 1; x + 1 < w; x++)
        {
            int c = img[(size_t)y * pitch + x];
            int l = img[(size_t)y * pitch + x - 1];
            int r = img[(size_t)y * pitch + x + 1];
            int u = img[(size_t)(y - 1) * pitch + x];
            int d = img[(size_t)(y + 1) * pitch + x];
            double v = (l + r + u + d - 4 * c);
            sum += v; sum2 += v * v; n++;
        }
    if (!n) return 0;
    double mean = sum / (double)n;
    return sum2 / (double)n - mean * mean;
}

PlaneMetrics Compare(const std::vector<UINT8>& got, const std::vector<UINT8>& ref, UINT32 w, UINT32 h,
                    UINT32 pitch)
{
    PlaneMetrics m;
    double acc = 0, sq = 0, mean = 0, cmean = 0;
    long long cvar = 0;
    for (UINT32 y = 0; y < h; y++)
        for (UINT32 x = 0; x < w; x++)
        {
            int g = got[(size_t)y * pitch + x];
            int r = ref[(size_t)y * w + x];
            double d = (double)g - (double)r;
            acc += fabs(d); sq += d * d; mean += g; cmean += r; cvar++;
        }
    m.haveRef = 1;
    m.mae = acc / (double)cvar;
    double mse = sq / (double)cvar;
    m.psnr = (mse > 0) ? 10.0 * log10(255.0 * 255.0 / mse) : 99.0;
    m.mean = mean / (double)cvar;
    m.refMean = cmean / (double)cvar;
    m.lapVar = LapVar(got, w, h, pitch);
    return m;
}

// ---------------------------------------------------------------- YUV -> RGB + image output
//
// The picture the owner judges has to be the SAME picture render_nv12_png.py produces, so the
// host-side encoder here uses the exact BT.601-limited coefficients that script uses, applied with
// the same truncation (.astype(np.uint8) == C-style float->UINT8 cast, NOT round-half-up). The old
// code instead recovered RGB from the pipeline's own matrix with a 3x3 inverse but then wrote those
// normalised 0..1 values straight into the 8-bit BMP without scaling by 255, so every channel
// truncated to 0 and the exported frame was black. It also depended on the inverse agreeing with
// the shader forward, which it does not for chroma. We now hardcode the three standard inverses.
//
// Exact coefficients (8-bit in/out, chroma centred at 128), BT.601 LIMITED (mode 0, the default):
//   y = Y - 16 ; u = U - 128 ; v = V - 128
//   R = 1.164383*y + 1.596027*v
//   G = 1.164383*y - 0.391762*u - 0.811237*v
//   B = 1.164383*y + 2.017232*u
//   rgb = (UINT8) clip(rgb, 0, 255)                       // truncation, matches render_nv12_png.py
// These equal 255/219 for luma and 255/224 * {1.402,0.344136,0.714136,1.772} for chroma, i.e. the
// textbook limited-range BT.601 inverse. Mode 1 = BT.601 full range, mode 2 = BT.709 limited.

static void YuvToRgb8(int mode, UINT8 Y, UINT8 U, UINT8 V, UINT8 rgb[3])
{
    float y, u = (float)U - 128.0f, v = (float)V - 128.0f, r, g, b;
    if (mode == 1)            // BT.601 full range
    {
        y = (float)Y;
        r = y + 1.402f * v;
        g = y - 0.344136f * u - 0.714136f * v;
        b = y + 1.772f * u;
    }
    else if (mode == 2)       // BT.709 limited
    {
        y = (float)Y - 16.0f;
        r = 1.164383f * y + 1.792741f * v;
        g = 1.164383f * y - 0.213249f * u - 1.539004f * v;
        b = 1.164383f * y + 2.112402f * u;
    }
    else                      // mode 0: BT.601 limited, == render_nv12_png.py
    {
        y = (float)Y - 16.0f;
        r = 1.164383f * y + 1.596027f * v;
        g = 1.164383f * y - 0.391762f * u - 0.811237f * v;
        b = 1.164383f * y + 2.017232f * u;
    }
    // Truncate, do not round: this is what PIL's .astype(np.uint8) does.
    rgb[0] = (UINT8)(r < 0 ? 0 : (r > 255 ? 255 : r));
    rgb[1] = (UINT8)(g < 0 ? 0 : (g > 255 ? 255 : g));
    rgb[2] = (UINT8)(b < 0 ? 0 : (b > 255 ? 255 : b));
}

// Renders the padded in-memory NV12 buffer to a top-down packed RGB24 scan (w*h*3). Reads the GPU
// layout: Y at row y*yPitch, UV plane at the GpuIspUVOffsetBytes offset (== yPitch*h here), one
// interleaved UV byte pair per two luma columns, chroma nearest-sampled by (x & ~1)/2 rows. This
// mirrors the tight-vs-padded rule: the exported .nv12 FILE is tightly packed (outW per row) but we
// render from the padded GPU buffer in memory, so yPitch/uvPitch (256-aligned) are always correct.
static bool Nv12ToRgb24(int mode, const UINT8* nv12, UINT32 w, UINT32 h, UINT32 yPitch,
                        UINT32 uvPitch, std::vector<UINT8>& rgb, bool rotate180 = false)
{
    if (!nv12) return false;
    rgb.assign((size_t)w * h * 3, 0);
    const UINT8* yPlane  = nv12;
    const UINT8* uvPlane = nv12 + (size_t)yPitch * h;   // == GpuIspUVOffsetBytes(w, h)
    for (UINT32 y = 0; y < h; y++)
    {
        UINT32 sy = rotate180 ? h - 1u - y : y;
        const UINT8* uvRow = uvPlane + (size_t)(sy / 2) * uvPitch;
        UINT8* dst = rgb.data() + (size_t)y * w * 3;
        for (UINT32 x = 0; x < w; x++)
        {
            UINT32 sx = rotate180 ? w - 1u - x : x;
            UINT8 U = uvRow[(sx & ~1u)];
            UINT8 V = uvRow[(sx & ~1u) + 1];
            YuvToRgb8(mode, yPlane[(size_t)sy * yPitch + sx], U, V, dst + (size_t)x * 3);
        }
    }
    return true;
}

static void PutBmpHeader(std::vector<UINT8>& out, UINT32 w, UINT32 h)
{
    UINT32 lineBytes = ((w * 3) + 3) & ~3u;
    UINT32 size = 54 + lineBytes * h;
    out.assign(size, 0);
    UINT8* hdr = out.data();
    hdr[0] = 'B'; hdr[1] = 'M';
    memcpy(hdr + 2, &size, 4);
    UINT32 dataOff = 54;
    memcpy(hdr + 10, &dataOff, 4);
    UINT32 bih = 40;
    memcpy(hdr + 14, &bih, 4);
    INT32 iw = (INT32)w, ih = -(INT32)h;      // negative height = top-down rows
    memcpy(hdr + 18, &iw, 4);
    memcpy(hdr + 22, &ih, 4);
    UINT16 planes = 1, bpp = 24;
    memcpy(hdr + 26, &planes, 2);
    memcpy(hdr + 28, &bpp, 2);
    memcpy(hdr + 34, &size, 4);
}

bool WriteBmpFromNv12(const std::string& path, int mode, const UINT8* nv12, UINT32 w, UINT32 h,
                      UINT32 yPitch, UINT32 uvPitch, bool rotate180 = false)
{
    std::vector<UINT8> rgb;
    if (!Nv12ToRgb24(mode, nv12, w, h, yPitch, uvPitch, rgb, rotate180)) { printf("ERROR: NV12 render failed\n"); return false; }
    std::vector<UINT8> out;
    PutBmpHeader(out, w, h);
    UINT32 lineBytes = ((w * 3) + 3) & ~3u;
    for (UINT32 y = 0; y < h; y++)
    {
        UINT8* dst = out.data() + 54 + (size_t)y * lineBytes;
        const UINT8* src = rgb.data() + (size_t)y * w * 3;
        for (UINT32 x = 0; x < w; x++)
        {
            dst[x * 3 + 0] = src[x * 3 + 2];   // BMP stores BGR, our buffer is RGB
            dst[x * 3 + 1] = src[x * 3 + 1];
            dst[x * 3 + 2] = src[x * 3 + 0];
        }
    }
    return WriteFile(path, out.data(), out.size());
}

// ---- minimal PNG writer, no zlib/WIC (this EWDK build only links d3d12/dxgi). The IDAT stream is
// a valid zlib container whose DEFLATE uses only STORED (uncompressed) blocks, so it is a real PNG
// readable by any decoder at ~BMP file size. Only needs CRC32 (chunk) + Adler32 (zlib tail).

static UINT32 PngCrc32(const UINT8* d, size_t n)
{
    static UINT32 tab[256]; static bool init = false;
    if (!init) { for (UINT32 i = 0; i < 256; i++) { UINT32 c = i; for (int k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1); tab[i] = c; } init = true; }
    UINT32 c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) c = tab[(c ^ d[i]) & 0xFFu] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

static UINT32 PngAdler32(const UINT8* d, size_t n)
{
    UINT32 a = 1, b = 0;
    for (size_t i = 0; i < n; i++) { a = (a + d[i]) % 65521u; b = (b + a) % 65521u; }
    return (b << 16) | a;
}

static void PngPutBE32(std::vector<UINT8>& v, UINT32 x)
{
    v.push_back((UINT8)(x >> 24)); v.push_back((UINT8)((x >> 16) & 0xFF));
    v.push_back((UINT8)((x >> 8) & 0xFF)); v.push_back((UINT8)(x & 0xFF));
}

static void PngAddChunk(std::vector<UINT8>& v, const char* type, const UINT8* data, size_t n)
{
    PngPutBE32(v, (UINT32)n);
    size_t cs = v.size();
    for (int i = 0; i < 4; i++) v.push_back((UINT8)type[i]);
    for (size_t i = 0; i < n; i++) v.push_back(data[i]);
    PngPutBE32(v, PngCrc32(&v[cs], v.size() - cs));
}

bool WritePngFromNv12(const std::string& path, int mode, const UINT8* nv12, UINT32 w, UINT32 h,
                      UINT32 yPitch, UINT32 uvPitch, bool rotate180 = false)
{
    std::vector<UINT8> rgb;
    if (!Nv12ToRgb24(mode, nv12, w, h, yPitch, uvPitch, rgb, rotate180)) { printf("ERROR: NV12 render failed\n"); return false; }

    // Filtered scanlines (filter byte 0 = None, then RGB for the row).
    std::vector<UINT8> raw;
    raw.reserve((size_t)h * (1 + (size_t)w * 3));
    for (UINT32 y = 0; y < h; y++)
    {
        raw.push_back(0);
        const UINT8* row = rgb.data() + (size_t)y * w * 3;
        for (size_t i = 0; i < (size_t)w * 3; i++) raw.push_back(row[i]);
    }

    // zlib stream: 2-byte header + DEFLATE stored blocks + Adler32 of the raw scanlines.
    std::vector<UINT8> idat;
    idat.push_back(0x78); idat.push_back(0x01);
    size_t pos = 0, n = raw.size();
    if (n == 0) { idat.push_back(1); idat.push_back(0); idat.push_back(0); idat.push_back(0xFF); idat.push_back(0xFF); }
    while (pos < n)
    {
        size_t blk = n - pos; if (blk > 65535u) blk = 65535u;
        bool last = (pos + blk == n);
        UINT16 L = (UINT16)blk, NL = (UINT16)(L ^ 0xFFFFu);
        idat.push_back(last ? 1 : 0);
        idat.push_back((UINT8)(L & 0xFF)); idat.push_back((UINT8)((L >> 8) & 0xFF));   // LEN  (LE)
        idat.push_back((UINT8)(NL & 0xFF)); idat.push_back((UINT8)((NL >> 8) & 0xFF));  // NLEN (LE)
        for (size_t i = 0; i < blk; i++) idat.push_back(raw[pos + i]);
        pos += blk;
    }
    PngPutBE32(idat, PngAdler32(raw.data(), raw.size()));

    std::vector<UINT8> out;
    const UINT8 sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    for (int i = 0; i < 8; i++) out.push_back(sig[i]);
    std::vector<UINT8> ihdr;
    PngPutBE32(ihdr, w); PngPutBE32(ihdr, h);
    ihdr.push_back(8); ihdr.push_back(2); ihdr.push_back(0); ihdr.push_back(0); ihdr.push_back(0); // 8-bit, colour type 2 = RGB
    PngAddChunk(out, "IHDR", ihdr.data(), ihdr.size());
    PngAddChunk(out, "IDAT", idat.data(), idat.size());
    PngAddChunk(out, "IEND", nullptr, 0);
    return WriteFile(path, out.data(), out.size());
}

// Also writes the Android reference i420 as a BMP so the two can be compared side by side.
bool WriteBmpFromI420(const std::string& path, const std::vector<UINT8>& y, const std::vector<UINT8>& u,
                      const std::vector<UINT8>& v, UINT32 w, UINT32 h)
{
    UINT32 lineBytes = ((w * 3) + 3) & ~3u;
    UINT32 size = 54 + lineBytes * h;
    std::vector<UINT8> out(size, 0);
    UINT8* hdr = out.data();
    hdr[0] = 'B'; hdr[1] = 'M';
    memcpy(hdr + 2, &size, 4);
    UINT32 dataOff = 54;
    memcpy(hdr + 10, &dataOff, 4);
    UINT32 bih = 40;
    memcpy(hdr + 14, &bih, 4);
    INT32 iw = (INT32)w, ih = -(INT32)h;
    memcpy(hdr + 18, &iw, 4);
    memcpy(hdr + 22, &ih, 4);
    UINT16 planes = 1, bpp = 24;
    memcpy(hdr + 26, &planes, 2);
    memcpy(hdr + 28, &bpp, 2);
    for (UINT32 j = 0; j < h; j++)
    {
        UINT8* dst = out.data() + 54 + (size_t)j * lineBytes;
        for (UINT32 i = 0; i < w; i++)
        {
            float Y = ((float)y[(size_t)j * w + i] - 16.0f) / 219.0f;
            float Cb = ((float)u[(size_t)(j / 2) * (w / 2) + i / 2] - 128.0f) / 224.0f;
            float Cr = ((float)v[(size_t)(j / 2) * (w / 2) + i / 2] - 128.0f) / 224.0f;
            float R = Y + 1.402f * Cr;
            float G = Y - 0.344136f * Cb - 0.714136f * Cr;
            float B = Y + 1.772f * Cb;
            dst[i * 3 + 0] = (UINT8)(B < 0 ? 0 : (B > 1 ? 255 : B * 255 + 0.5f));
            dst[i * 3 + 1] = (UINT8)(G < 0 ? 0 : (G > 1 ? 255 : G * 255 + 0.5f));
            dst[i * 3 + 2] = (UINT8)(R < 0 ? 0 : (R > 1 ? 255 : R * 255 + 0.5f));
        }
    }
    return WriteFile(path, out.data(), out.size());
}

std::string fmtFps(double msPerFrame)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "%.1f fps", 1000.0 / msPerFrame);
    return std::string(buf);
}

}   // namespace

// ---------------------------------------------------------------- main

int main(int argc, char** argv)
{
    Options o;
    std::string refBmp;
    for (int i = 1; i < argc; i++)
    {
        std::string a = argv[i];
        auto next = [&](UINT32& dst) { if (i + 1 < argc) dst = (UINT32)atoi(argv[++i]); };
        auto nextf = [&](float& dst) { if (i + 1 < argc) dst = (float)atof(argv[++i]); };
        auto nexti = [&](int& dst) { if (i + 1 < argc) dst = atoi(argv[++i]); };
        auto nexts = [&](std::string& dst) { if (i + 1 < argc) dst = argv[++i]; };
        if (a == "--raw") nexts(o.rawPath);
        else if (a == "--params") nexts(o.paramsPath);
        else if (a == "--ref-yuv") nexts(o.refYuvPath);
        else if (a == "--ref-bmp") nexts(refBmp);
        else if (a == "--out-nv12") nexts(o.outNv12);
        else if (a == "--out-cpu-nv12") nexts(o.outCpuNv12);
        else if (a == "--out-bmp") nexts(o.outBmp);
        else if (a == "--out-png") nexts(o.outPng);
        else if (a == "--rotate180") o.rotate180 = true;
        else if (a == "--direct") o.direct = true;
        else if (a == "--width") next(o.rawW);
        else if (a == "--height") next(o.rawH);
        else if (a == "--raw-pitch") next(o.rawPitch);
        else if (a == "--out-w") next(o.outW);
        else if (a == "--out-h") next(o.outH);
        else if (a == "--ref-w") next(o.refW);
        else if (a == "--ref-h") next(o.refH);
        else if (a == "--crop") { if (i + 4 < argc) { o.cropX = (UINT32)atoi(argv[++i]); o.cropY = (UINT32)atoi(argv[++i]); o.cropW = (UINT32)atoi(argv[++i]); o.cropH = (UINT32)atoi(argv[++i]); } }
        else if (a == "--pattern") { nexti(o.pattern); o.patternGiven = true; }
        else if (a == "--yuv") nexti(o.yuvMode);
        else if (a == "--adapter") nexti(o.adapter);
        else if (a == "--repeat") nexti(o.repeat);
        else if (a == "--gamma") { o.gammaOn = true; nextf(o.gamma); }
        else if (a == "--exposure") nextf(o.exGain);
        else if (a == "--denoise") nextf(o.denoise);
        else if (a == "--nr-range") { nextf(o.nrRangeA); nextf(o.nrRangeB); }
        else if (a == "--sharpen") nextf(o.sharpen);
        else if (a == "--sharpen-radius") nextf(o.sharpenRadius);
        else if (a == "--sharpen-thr") nextf(o.sharpenThr);
        else if (a == "--sharpen-limit") nextf(o.sharpenLimit);
        else if (a == "--sharpen-edge") { nextf(o.sharpenEdgeLo); nextf(o.sharpenEdgeHi); }
        else if (a == "--chroma-smooth") o.chromaSmooth = 1;
        else if (a == "--no-chroma-smooth") o.chromaSmooth = 0;
        else if (a == "--chroma-gain") nextf(o.chromaGain);
        else if (a == "--chroma-recentre") o.chromaRecentre = 1;
        else if (a == "--no-chroma-recentre") o.chromaRecentre = 0;
        else if (a == "--chroma-bias-u") { nextf(o.chromaBiasU); o.chromaBiasUSet = 1; }
        else if (a == "--chroma-bias-v") { nextf(o.chromaBiasV); o.chromaBiasVSet = 1; }
        else if (a == "--cpu") { o.useCpu = true; o.useGpu = false; }
        else if (a == "--both") o.useCpu = true;
        else if (a == "--no-lsc") o.noLsc = true;
        else if (a == "--no-tone") o.noTone = true;
        else if (a == "--no-ccm") o.noCcm = true;
        else if (a == "--no-awb") o.noAwb = true;
        else if (a == "--debug") o.debug = true;
        else if (a == "--check-front") o.checkFront = true;
        else if (a == "--help" || a == "-h")
        {
            printf("GpuIspBench - GPUISP2 offline ISP harness\n"
                   "  --raw <bin> --width <n> --height <n> [--raw-pitch <bytes>]   sensor RAW16 plane\n"
                   "  --params <txt>                IQ values (see bench/tools/result2params.py)\n"
                   "  [--out-w <n> --out-h <n>] [--crop x y w h]                    default: full crop, 4:3 preview size\n"
                   "  --pattern 0..3              0=BGGR 1=GRBG 2=GBRG 3=RGGB (params key 'pattern' if not given)\n"
                   "  --yuv 0|1|2                 0=BT.601 limited 1=601 full 2=BT.709 limited\n"
                   "  --ref-yuv <i420> --ref-w --ref-h [--ref-bmp <path>]           Android output of the same frame\n"
                   "  --out-nv12 <path> --out-bmp <path> --out-png <path>\n"
                   "  --out-cpu-nv12 <path>     the CPU reference frame in the same tight layout, to diff\n"
                   "                            against the GPU file when the parity line reports a gap\n"
                   "  --cpu | --both | --gpu      --cpu runs the reference only, --both cross-checks\n"
                   "  --no-lsc --no-tone --no-ccm --no-awb --gamma [n] --exposure [f]\n"
                   "  --denoise <f>               5x5 bilateral luma width in 0..1 (0 = off)\n"
                   "  --nr-range <a> <b>          make that width signal-dependent: 1/range = a + b*luma,\n"
                   "                            luma being the gamma-encoded Y the range test already keys on.\n"
                   "                            b <= 0 keeps the uniform 1/denoise width exactly as before.\n"
                   "                            params keys nr_range_a / nr_range_b, command line still wins\n"
                   "  --sharpen <f>               luma unsharp amount on the NV12 Y plane (0 = off)\n"
                   "  --sharpen-radius <1|2|3|4>  unsharp binomial blur radius (4 is the last radius whose\n"
                   "                            fp32 accumulator stays exactly integral)\n"
                   "  --sharpen-thr <codes>       unsharp deadzone, 8-bit luma codes\n"
                   "  --sharpen-limit <codes>     unsharp halo clamp, 8-bit luma codes\n"
                   "  --sharpen-edge <lo> <hi>    fade the injection out as the local contrast of the\n"
                   "                            blur window rises from lo to hi codes (0 0 = off)\n"
                   "  --chroma-smooth | --no-chroma-smooth\n"
                   "                            stage 5 on the finished NV12 UV plane only (luma is\n"
                   "                            byte-identical either way); params key chroma_smooth 0|1\n"
                   "  --chroma-gain <x>         stage 5's UV saturation gain about grey, needs --chroma-smooth;\n"
                   "                            1 4 6 4 1 binomial, then two 5x5 luma-guided passes, then x.\n"
                   "                            0 = the binomial alone, byte-identical to Build009.\n"
                   "                            params key chroma_gain <x>\n"
                   "  --chroma-recentre | --no-chroma-recentre\n"
                   "                            stage 5 also measures this frame's own chroma neutral point\n"
                   "                            (exact integer sums over the sites within 8 codes of grey,\n"
                   "                            taken by the first guided pass) and subtracts it before the\n"
                   "                            gain. Needs --chroma-smooth; with gain 0 it re-centres at 1.0.\n"
                   "                            params key chroma_recentre 0|1\n"
                   "  --chroma-bias-u <codes>   stage 5's grey target: this much added to the finished U\n"
                   "                            AFTER the re-centring, so the frame's grey lands on\n"
                   "                            128+bias instead of 128. 0 (absent) is byte-identical to\n"
                   "                            Build012. Needs the cascade. params key chroma_bias_u <x>\n"
                   "  --chroma-bias-v <codes>   the same term on V. The front ships U only (its V gap\n"
                   "                            measured -0.010); the rear's own measured grey gap lives on\n"
                   "                            V, so 0 (absent) keeps every front preset byte-identical.\n"
                   "                            params key chroma_bias_v <x>\n"
                   "                            --exposure/--denoise override the params keys exgain/denoise\n"
                   "                            sharpen keys in the params file: sharpen, sharpen_radius,\n"
                   "                            sharpen_thr, sharpen_limit (command line still wins)\n"
                   "  --adapter <n> --repeat <n> --debug\n"
                   "  --check-front               read back the full-res RGBA16F and diff it vs the CPU reference\n");
            return 0;
        }
        else { printf("ERROR: unknown argument '%s' (try --help)\n", a.c_str()); return 1; }
    }

    if (o.rawPath.empty() || o.rawW == 0 || o.rawH == 0)
    {
        printf("ERROR: --raw --width --height are required\n");
        return 1;
    }
    if (o.outW == 0) o.outW = (o.refW ? o.refW : 1280);
    if (o.outH == 0) o.outH = (o.refH ? o.refH : 960);
    if (o.cropW == 0) { o.cropW = o.rawW; o.cropH = o.rawH; }
    if ((o.outW % 4) || (o.outH % 2)) { printf("ERROR: --out-w must be a multiple of 4 and --out-h even\n"); return 1; }

    AndroidParams ap;
    if (!o.paramsPath.empty() && !LoadParams(o.paramsPath, ap)) return 1;

    GpuIspParams p;
    memset(&p, 0, sizeof(p));
    p.rawW = o.rawW; p.rawH = o.rawH; p.outW = o.outW; p.outH = o.outH;
    p.cropX = o.cropX; p.cropY = o.cropY; p.cropW = o.cropW; p.cropH = o.cropH;
    p.lscCols = ap.lscCols; p.lscRows = ap.lscRows;
    p.blackLevel = ap.black ? ap.black : 0.0f;
    p.whiteLevel = ap.white ? ap.white : 1023.0f;
    p.exGain = (o.exGain >= 0.0f) ? o.exGain : ap.exGain;
    for (int i = 0; i < 4; i++) p.awb[i] = ap.awb[i];
    for (int i = 0; i < 9; i++) GpuIspCcm(p, i) = ap.ccm[i];
    p.gamma = o.gamma;
    p.denoise = (o.denoise >= 0.0f) ? o.denoise : ap.denoise;
    p.sharpen = (o.sharpen >= 0.0f) ? o.sharpen : ap.sharpen;
    // Same precedence as denoise/sharpen: command line, then the params file, then the shipped
    // default (radius 1, no deadzone, halo clamp 24 = the front driver's value).
    p.sharpenRadius = (o.sharpenRadius >= 0.0f) ? o.sharpenRadius
                        : (ap.sharpenRadius >= 0.0f ? ap.sharpenRadius : 1.0f);
    p.sharpenThreshold = (o.sharpenThr >= 0.0f) ? o.sharpenThr
                        : (ap.sharpenThr >= 0.0f ? ap.sharpenThr : 0.0f);
    p.sharpenLimit = (o.sharpenLimit >= 0.0f) ? o.sharpenLimit
                    : (ap.sharpenLimit >= 0.0f ? ap.sharpenLimit : 24.0f);
    // Edge protection defaults to off (0/0), so a params file or live caller that never mentions it
    // keeps the pre-protection behaviour byte for byte.
    {
        float edgeLo = (o.sharpenEdgeLo >= 0.0f) ? o.sharpenEdgeLo
                         : (ap.sharpenEdgeLo >= 0.0f ? ap.sharpenEdgeLo : 0.0f);
        float edgeHi = (o.sharpenEdgeHi >= 0.0f) ? o.sharpenEdgeHi
                         : (ap.sharpenEdgeHi >= 0.0f ? ap.sharpenEdgeHi : 0.0f);
        GpuIspSetEdgeProtect(p, edgeLo, edgeHi);
    }
    // Signal-dependent NR width. Off unless a preset supplies a positive slope, so every params file
    // that does not mention nr_range_* keeps the shipped uniform 1/Denoise reciprocal byte for byte.
    {
        float na = (o.nrRangeA >= 0.0f) ? o.nrRangeA : ap.nrRangeA;
        float nb = (o.nrRangeB >= 0.0f) ? o.nrRangeB : ap.nrRangeB;
        if (na >= 0.0f && nb > 0.0f) GpuIspSetNrRange(p, na, nb);
    }
    // Parity offsets that turn each mosaic into the shader's BGGR indexing. The params file wins over
    // the default when it carries a pattern key, so a curve fitted at one mosaic cannot be replayed at
    // another; --pattern still overrides both.
    int pat = o.patternGiven ? o.pattern : (ap.pattern >= 0 ? ap.pattern : GPUISP_BGGR);
    p.cfaOff = ((pat == 2 || pat == 3) ? 1u : 0u) | ((pat == 1 || pat == 3) ? 2u : 0u);
    p.flags = 0;
    if(o.direct)p.flags|=GPUISP_FLAG_DIRECT;
    if (!o.noLsc && ap.lscCols * ap.lscRows > 1) p.flags |= 0x0001u;
    if (!o.noAwb) p.flags |= 0x0002u;
    if (!o.noCcm) p.flags |= 0x0004u;
    if (!o.noTone && ap.toneX.size() >= 2) p.flags |= 0x0008u;
    if (o.gammaOn) p.flags |= 0x0010u;
    if (p.denoise > 0.0f) p.flags |= 0x0040u;
    if (p.sharpen > 0.0f) p.flags |= GPUISP_FLAG_SHARP;
    // The stage-5 saturation gain follows the same precedence as denoise/sharpen - command line, then
    // the params file, then off. It is only read while stage 5 runs, so chroma_gain without
    // chroma_smooth is inert, and chroma_smooth without chroma_gain stays the binomial Build009 shipped.
    p.chromaGain = (o.chromaGain >= 0.0f) ? o.chromaGain : ap.chromaGain;
    // Chroma plane smoothing is opt-in per preset, so a params file that never carries the key and a
    // caller that never sets the bit keep the shipped UV plane byte for byte.
    if ((o.chromaSmooth >= 0 ? o.chromaSmooth : (ap.chromaSmooth > 0.0f ? 1 : 0)) != 0)
        p.flags |= GPUISP_FLAG_CHROMA_SMOOTH;
    // Re-centring rides on stage 5: without the smooth bit the cascade never runs, so this bit is
    // deliberately ANDed rather than left for the shader to ignore.
    int recentreOn = (o.chromaRecentre >= 0 ? o.chromaRecentre : (ap.chromaRecentre > 0.0f ? 1 : 0)) != 0 ? 1 : 0;
    if (recentreOn && (p.flags & GPUISP_FLAG_CHROMA_SMOOTH) != 0u)
        p.flags |= GPUISP_FLAG_CHROMA_RECENTRE;
    else if (recentreOn)
        printf("  note: chroma_recentre ignored without chroma_smooth\n");
    // The grey target is read by stage 5's last pass only, so it lives and dies with the cascade exactly
    // like the gain does; 0.0 for a preset that never carries the key leaves that pass's arithmetic alone.
    p.chromaBiasU = o.chromaBiasUSet ? o.chromaBiasU : ap.chromaBiasU;
    p.chromaBiasV = o.chromaBiasVSet ? o.chromaBiasV : ap.chromaBiasV;
    // The guided passes' luma width: a command-line value wins over the file, and neither being set
    // leaves 0, which the shader reads as the width every preset before BuildC007 used.
    GpuIspSetChromaSigma(p, o.chromaSigma >= 0.0f ? o.chromaSigma : ap.chromaSigma);
    if ((p.chromaBiasU != 0.0f || p.chromaBiasV != 0.0f) && (p.flags & GPUISP_FLAG_CHROMA_SMOOTH) == 0u)
        printf("  note: chroma_bias_u/v ignored without chroma_smooth\n");
    GpuIspSetYuvMatrix(p, o.yuvMode);

    GpuIspTone tone;
    BuildToneLut(ap, tone);
    std::vector<float> lscCells(ap.lsc.size() ? ap.lsc : std::vector<float>(16, 1.0f));

    std::vector<UINT16> raw;
    if (!LoadRaw(o, raw)) return 1;

    UINT32 yPitch = (o.outW + 255u) & ~255u;
    UINT32 uvPitch = (o.outW + 255u) & ~255u;
    UINT64 nv12Size = GpuIspNv12SizeBytes(o.outW, o.outH);
    std::vector<UINT8> nv12Gpu((size_t)nv12Size, 0), nv12Cpu((size_t)nv12Size, 0);

    printf("GPUISP2 bench  raw %ux%u  crop %u,%u %ux%u  out %ux%u  flags 0x%x\n",
           o.rawW, o.rawH, o.cropX, o.cropY, o.cropW, o.cropH, o.outW, o.outH, p.flags);
    printf("  black %.1f white %.1f  awb %.4f/%.4f/%.4f/%.4f  lsc %ux%u  tone %zu pts  exgain %.3f  denoise %.4f\n",
           p.blackLevel, p.whiteLevel, p.awb[0], p.awb[1], p.awb[2], p.awb[3], p.lscCols, p.lscRows,
           ap.toneX.size(), p.exGain, p.denoise);
    printf("  sharpen %.4f radius %d thr %.1f limit %.1f  edge %.0f-%.0f (%s)\n",
           p.sharpen, (int)p.sharpenRadius, p.sharpenThreshold, p.sharpenLimit,
           p.sharpenEdgeLo, p.sharpenEdgeHi, p.sharpenEdgeScale > 0.0f ? "on" : "off");
    printf("  nr range a %.4f b %.4f (%s)\n", p.nrRangeA, p.nrRangeB,
           p.nrRangeB > 0.0f ? "signal-dependent" : "uniform 1/denoise");
    printf("  chroma %s gain %.4f recentre %s bias u %+0.4f v %+0.4f (%s)\n",
           (p.flags & GPUISP_FLAG_CHROMA_SMOOTH) ? "stage on" : "stage off", p.chromaGain,
           (p.flags & GPUISP_FLAG_CHROMA_RECENTRE) ? "on" : "off", p.chromaBiasU, p.chromaBiasV,
           (p.flags & GPUISP_FLAG_CHROMA_SMOOTH) == 0u ? "UV plane untouched"
             : ((p.chromaGain > 0.0f || (p.flags & GPUISP_FLAG_CHROMA_RECENTRE) != 0u)
                 ? "binomial + luma-guided x2 + gain" : "binomial only"));

    double tFront = 0, tColor = 0, tNv12 = 0, tSharp = 0, tChroma = 0;
    bool gpuOk = false;
    GpuIspCore* core = nullptr;
    GpuIspRefOutput ref;
    bool cpuRan = false;
    if (o.useGpu)
    {
        core = GpuIspCreate(o.adapter, o.debug);
        if (!core) { printf("ERROR: no core\n"); return 1; }
        const char* createErr = GpuIspLastError(core);
        if (createErr && createErr[0]) printf("  core problem after create: %s\n", createErr);
        gpuOk = true;
        for (int r = 0; r < (int)(o.repeat ? o.repeat : 1); r++)
        {
            double a, b, c, e, f;
            if (!GpuIspRun(core, p, tone, lscCells.data(), raw.data(), (UINT64)raw.size() * 2,
                           nv12Gpu.data()))
            {
                printf("ERROR: GPU pipeline: %s\n", GpuIspLastError(core));
                gpuOk = false;
                break;
            }
            GpuIspStageTimesMs(core, &a, &b, &c, &e, &f);
            tFront = a; tColor = b; tNv12 = c; tSharp = e; tChroma = f;
        }
        if (gpuOk)
        {
            double total = tFront + tColor + tNv12 + tSharp + tChroma;
            printf("  GPU stages: front-end %.2f ms  colour+downscale %.2f ms  NV12 %.2f ms  sharpen %.2f ms  chroma %.2f ms  total %.2f ms -> %s\n",
                   tFront, tColor, tNv12, tSharp, tChroma, total, total > 0.0 ? fmtFps(total).c_str() : "n/a");
        }
        // Perf P1: run the same frame as ONE command list / ONE fence, verify byte-identical output
        // vs the staged path, and report the true single-list frame latency.
        if (gpuOk)
        {
            std::vector<UINT8> nv12Fused((size_t)nv12Size, 0);
            double fusedMs = 0; bool fusedOk = true;
            for (int r = 0; r < (int)(o.repeat ? o.repeat : 1); r++)
            {
                if (!GpuIspRunFused(core, p, tone, lscCells.data(), raw.data(), (UINT64)raw.size() * 2,
                                    nv12Fused.data(), &fusedMs))
                {
                    printf("ERROR: fused pipeline: %s\n", GpuIspLastError(core));
                    fusedOk = false; break;
                }
            }
            if (fusedOk)
            {
                size_t diff = 0;
                for (size_t i = 0; i < (size_t)nv12Size; i++) if (nv12Gpu[i] != nv12Fused[i]) diff++;
                // The comparison is fused wall-clock vs the SUM of the five individually fenced stage
                // timers above - not a second "staged" pipeline, which this code does not have. The line
                // used to add only the first three timers and call that "staged(5 lists)", which made the
                // fused path look 28% slower than a run that never happened.
                double base = tFront + tColor + tNv12 + tSharp + tChroma;
                printf("  PERF fused(1 list/1 fence) %.2f ms  vs sum of the 5 fenced stage timers %.2f ms"
                       "  -> %.2fx; fused-vs-staged byte-diff=%zu %s\n",
                       fusedMs, base, base > 0.0 ? base / fusedMs : 0.0, diff,
                       diff == 0 ? "(gate-safe: identical output)" : "(OUTPUT DIFFERS - NOT safe)");
            }
        }
    }

    if (o.useCpu || !gpuOk || o.checkFront)
    {
        if (!GpuIspRefRun(p, lscCells.data(), tone, raw.data(), ref, nv12Cpu.data()))
        {
            printf("ERROR: CPU reference failed\n");
            if (core) GpuIspDestroy(core);
            return 1;
        }
        cpuRan = true;
        if (!gpuOk) { nv12Gpu = nv12Cpu; }
    }

    // Stage-1 gate: the full-resolution RGBA16F read straight off the GPU must equal the
    // reference's. Isolates a broken RAW upload / SRV-UAV table from a colour-math difference.
    if (o.checkFront && gpuOk && core && cpuRan)
    {
        std::vector<float> gpuFull((size_t)p.rawW * p.rawH * 4, 0.0f);
        if (!GpuIspReadFullRgb(core, gpuFull.data(), gpuFull.size() * sizeof(float)))
            printf("  check-front: readback failed: %s\n", GpuIspLastError(core));
        else if (ref.rgbFull.size() != gpuFull.size())
            printf("  check-front: reference did not produce rgbFull (%zu vs %zu floats)\n",
                   ref.rgbFull.size(), gpuFull.size());
        else
        {
            double worst = 0.0; size_t at = 0; size_t over = 0;
            for (size_t i = 0; i < gpuFull.size(); i++)
            {
                double d = fabs((double)gpuFull[i] - (double)ref.rgbFull[i]);
                if (d > worst) { worst = d; at = i; }
                if (i % 4 != 3 && d > 1.0 / 255.0) over++;   // excluding the alpha lane
            }
            printf("  check-front: max RGBA16F diff %.6f at pixel %zu (gpu %.5f/%.5f/%.5f vs cpu %.5f/%.5f/%.5f)\n",
                   worst, at / 4, gpuFull[at], gpuFull[at + 1], gpuFull[at + 2],
                   ref.rgbFull[at], ref.rgbFull[at + 1], ref.rgbFull[at + 2]);
            printf("  check-front: channels over 1/255: %zu of %zu (%.5f%%)\n", over, gpuFull.size(),
                   100.0 * (double)over / (double)gpuFull.size());
            printf("  check-front: first 4 GPU pixels:");
            for (int i = 0; i < 4; i++)
                printf(" (%.4f,%.4f,%.4f)", gpuFull[i * 4], gpuFull[i * 4 + 1], gpuFull[i * 4 + 2]);
            printf("\n  check-front: first 4 CPU pixels:");
            for (int i = 0; i < 4; i++)
                printf(" (%.4f,%.4f,%.4f)", ref.rgbFull[i * 4], ref.rgbFull[i * 4 + 1], ref.rgbFull[i * 4 + 2]);
            printf("\n");
        }
    }
    // Stage-2 gate: same idea for the colour + downscale output.
    if (o.checkFront && gpuOk && core && cpuRan)
    {
        std::vector<float> gpuPrev((size_t)p.outW * p.outH * 4, 0.0f);
        if (!GpuIspReadPreviewRgb(core, gpuPrev.data(), gpuPrev.size() * sizeof(float)))
            printf("  check-colour: readback failed: %s\n", GpuIspLastError(core));
        else if (ref.rgbOut.size() == gpuPrev.size())
        {
            double worst = 0.0; size_t at = 0; double gpuSum = 0, cpuSum = 0;
            for (size_t i = 0; i < gpuPrev.size(); i++)
            {
                double d = fabs((double)gpuPrev[i] - (double)ref.rgbOut[i]);
                if (d > worst) { worst = d; at = i; }
                gpuSum += gpuPrev[i]; cpuSum += ref.rgbOut[i];
            }
            printf("  check-colour: max RGBA16F diff %.6f at pixel %zu  mean GPU %.6f / CPU %.6f\n",
                   worst, at / 4, gpuSum / gpuPrev.size(), cpuSum / ref.rgbOut.size());
            printf("  check-colour: GPU px0 (%.4f,%.4f,%.4f)  CPU px0 (%.4f,%.4f,%.4f)\n",
                   gpuPrev[0], gpuPrev[1], gpuPrev[2], ref.rgbOut[0], ref.rgbOut[1], ref.rgbOut[2]);
        }
    }

    if (core) { GpuIspDestroy(core); core = nullptr; }

    if (o.useGpu && o.useCpu && gpuOk && cpuRan)
    {
        long long diff = 0, maxd = 0;
        for (UINT32 y = 0; y < o.outH; y++)
            for (UINT32 x = 0; x < o.outW; x++)
            {
                UINT8 a = nv12Gpu[(size_t)y * yPitch + x], b = nv12Cpu[(size_t)y * yPitch + x];
                long long d = llabs((long long)a - (long long)b);
                diff += d; if (d > maxd) maxd = d;
            }
        printf("  GPU vs CPU luma: MAE %.4f  max %lld  (%s)\n", (double)diff / ((double)o.outW * o.outH),
               maxd, maxd <= 1 ? "MATCH" : "MISMATCH - shader and reference diverged");

        // Chroma is a separate write path (interleaved pairs, half the rows) and needs its own gate.
        long long cdiff = 0, cmax = 0;
        for (UINT32 y = 0; y < o.outH / 2; y++)
            for (UINT32 b = 0; b < o.outW; b++)
            {
                size_t off = (size_t)yPitch * o.outH + (size_t)y * uvPitch + b;
                long long d = llabs((long long)nv12Gpu[off] - (long long)nv12Cpu[off]);
                cdiff += d; if (d > cmax) cmax = d;
            }
        printf("  GPU vs CPU chroma: MAE %.4f  max %lld  (uvPitch %u >= outW %u: %s)\n",
               (double)cdiff / ((double)o.outW * (o.outH / 2)), cmax, uvPitch, o.outW,
               uvPitch >= o.outW ? "ok" : "BAD PITCH");
    }

    const UINT8* use = gpuOk ? nv12Gpu.data() : nv12Cpu.data();

    // Tight packing, shared by the delivered frame and its CPU mirror so --out-cpu-nv12 produces a file
    // that diffs against --out-nv12 pixel for pixel. The aggregate GPU-vs-CPU line says whether the two
    // sides agree; only two files say WHERE they disagree, and a cascade this wide needs that map.
    auto writeTight = [&](const std::string& path, const UINT8* src) -> bool {
        std::vector<UINT8> tight((size_t)o.outW * o.outH + (size_t)o.outW * (o.outH / 2), 0);
        for (UINT32 y = 0; y < o.outH; y++)
            memcpy(&tight[(size_t)y * o.outW], src + (size_t)y * yPitch, o.outW);
        UINT8* uvDst = tight.data() + (size_t)o.outW * o.outH;
        for (UINT32 y = 0; y < o.outH / 2; y++)
            memcpy(uvDst + (size_t)y * o.outW, src + (size_t)yPitch * o.outH + (size_t)y * uvPitch,
                   o.outW);
        return WriteFile(path, tight.data(), tight.size());
    };

    if (!o.outNv12.empty())
    {
        if (!writeTight(o.outNv12, use)) return 1;
        printf("  wrote NV12 %s (%zu bytes)\n", o.outNv12.c_str(),
               (size_t)o.outW * o.outH + (size_t)o.outW * (o.outH / 2));
    }
    if (!o.outCpuNv12.empty())
    {
        if (!cpuRan) { printf("ERROR: --out-cpu-nv12 needs --cpu or --both\n"); return 1; }
        if (!writeTight(o.outCpuNv12, nv12Cpu.data())) return 1;
        printf("  wrote CPU NV12 %s\n", o.outCpuNv12.c_str());
    }
    if (!o.outBmp.empty())
    {
        if (!WriteBmpFromNv12(o.outBmp, o.yuvMode, use, o.outW, o.outH, yPitch, uvPitch, o.rotate180)) return 1;
        printf("  wrote BMP %s\n", o.outBmp.c_str());
    }
    if (!o.outPng.empty())
    {
        if (!WritePngFromNv12(o.outPng, o.yuvMode, use, o.outW, o.outH, yPitch, uvPitch, o.rotate180)) return 1;
        printf("  wrote PNG %s\n", o.outPng.c_str());
    }

    if (!o.refYuvPath.empty())
    {
        if (o.refW == 0 || o.refH == 0) { printf("ERROR: --ref-w and --ref-h are required with --ref-yuv\n"); return 1; }
        std::vector<UINT8> ry, ru, rv;
        if (!LoadI420(o.refYuvPath, o.refW, o.refH, ry, ru, rv)) return 1;
        if (!refBmp.empty())
            printf("  wrote reference BMP %s (%s)\n", refBmp.c_str(),
                   WriteBmpFromI420(refBmp, ry, ru, rv, o.refW, o.refH) ? "ok" : "failed");

        std::vector<UINT8> gy = ry, gu = ru, gv = rv;
        if (o.refW != o.outW || o.refH != o.outH)
        {
            BoxResizeU8(ry, o.refW, o.refH, gy, o.outW, o.outH);
            BoxResizeU8(ru, o.refW / 2, o.refH / 2, gu, o.outW / 2, o.outH / 2);
            BoxResizeU8(rv, o.refW / 2, o.refH / 2, gv, o.outW / 2, o.outH / 2);
            printf("  reference resized %ux%u -> %ux%u for comparison\n", o.refW, o.refH, o.outW, o.outH);
        }
        std::vector<UINT8> ourY(o.outW * o.outH), ourU(o.outW / 2 * o.outH / 2), ourV(o.outW / 2 * o.outH / 2);
        for (UINT32 y = 0; y < o.outH; y++) memcpy(&ourY[(size_t)y * o.outW], use + (size_t)y * yPitch, o.outW);
        const UINT8* uvBase = use + (size_t)yPitch * o.outH;
        for (UINT32 y = 0; y < o.outH / 2; y++)
            for (UINT32 x = 0; x < o.outW / 2; x++)
            {
                ourU[(size_t)y * (o.outW / 2) + x] = uvBase[(size_t)y * uvPitch + x * 2];
                ourV[(size_t)y * (o.outW / 2) + x] = uvBase[(size_t)y * uvPitch + x * 2 + 1];
            }
        PlaneMetrics my = Compare(ourY, gy, o.outW, o.outH, o.outW);
        PlaneMetrics mu = Compare(ourU, gu, o.outW / 2, o.outH / 2, o.outW / 2);
        PlaneMetrics mv = Compare(ourV, gv, o.outW / 2, o.outH / 2, o.outW / 2);
        double refLap = LapVar(gy, o.outW, o.outH, o.outW);
        long long clip = 0;
        for (UINT32 y = 0; y < o.outH; y++)
            for (UINT32 x = 0; x < o.outW; x++)
                if (ourY[(size_t)y * o.outW + x] >= 250) clip++;
        printf("\n  vs Android reference (%ux%u i420)\n", o.refW, o.refH);
        printf("    Y  MAE %6.2f  PSNR %5.2f dB\n", my.mae, my.psnr);
        printf("    luma mean   ours %6.2f   Android %6.2f   delta %6.2f  (target < 3)  -> %s\n",
               my.mean, my.refMean, fabs(my.mean - my.refMean),
               fabs(my.mean - my.refMean) < 3.0 ? "PASS" : "FAIL");
        printf("    sharpness   ours %7.1f   Android %7.1f   delta %6.1f\n",
               my.lapVar, refLap, my.lapVar - refLap);
        printf("    U  MAE %6.2f   V  MAE %6.2f      highlights clipped %.2f%%\n", mu.mae, mv.mae,
               100.0 * (double)clip / ((double)o.outW * o.outH));
    }
    printf("\n");
    return gpuOk ? 0 : 2;
}
