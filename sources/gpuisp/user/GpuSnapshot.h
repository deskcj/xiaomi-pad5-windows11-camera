#pragma once

// Optional, bounded diagnostic. Keep three matching pipeline frames in RAM,
// then save only once the producer has been idle for at least 500 ms.
struct GpuSnapshot {
    struct Frame {
        CODEX_GPU_FRAME header;
        GpuIspParams params;
        GpuIspTone tone;
        std::vector<BYTE> raw, nv12, rgb;
        std::vector<float> lsc;
    };
    std::vector<Frame> saved;
    std::wstring request;
    std::string folder;
    ULONGLONG nextCheck = 0;
    bool armed = false;
    GpuSnapshot(const wchar_t* path) : request(std::wstring(path)+L"\\SNAPSHOT.REQUEST"), folder(Utf8(path)) {}
    bool Requested(){
        if(armed)return true;
        if(!saved.empty() || GetTickCount64()<nextCheck)return false;
        if(GetFileAttributesW(request.c_str())!=INVALID_FILE_ATTRIBUTES)return true;
        nextCheck=GetTickCount64()+1000;return false;
    }
    void Capture(const CODEX_GPU_FRAME& header, const GpuIspParams& params,
                 const GpuIspTone& tone, const std::vector<float>& lsc,
                 const BYTE* raw, const std::vector<BYTE>& nv12, const BYTE* rgb) {
        if (!armed && saved.empty() && GetTickCount64() >= nextCheck) {
            nextCheck = GetTickCount64()+1000;
            if (GetFileAttributesW(request.c_str()) != INVALID_FILE_ATTRIBUTES && DeleteFileW(request.c_str()))
                armed = true;
        }
        if (!armed || saved.size() >= 3) return;
        Frame frame;
        frame.header = header; frame.params = params; frame.tone = tone; frame.lsc = lsc;
        frame.raw.assign(raw, raw+header.Bytes);
        frame.nv12 = nv12;
        frame.rgb.assign(rgb, rgb+CODEX_GPU_RGB_BYTES);
        saved.push_back(std::move(frame));
        if (saved.size() == 3) armed = false;
    }
    void FlushIdle(ULONGLONG idleMs) {
        if (saved.empty() || idleMs < 500) return;
        bool ok = true;
        for (size_t i=0; i<saved.size(); ++i) {
            const Frame& f = saved[i];
            std::string stem=folder+"\\snapshot-"+std::to_string(i);
            ok = WriteFile(stem+".raw16", f.raw.data(), f.raw.size()) && ok;
            ok = WriteFile(stem+".nv12", f.nv12.data(), f.nv12.size()) && ok;
            ok = WriteFile(stem+".rgb32", f.rgb.data(), f.rgb.size()) && ok;
            ok = WriteFile(stem+".header", &f.header, sizeof(f.header)) && ok;
            ok = WriteFile(stem+".params", &f.params, sizeof(f.params)) && ok;
            ok = WriteFile(stem+".tone", &f.tone, sizeof(f.tone)) && ok;
            ok = WriteFile(stem+".lsc", f.lsc.data(), f.lsc.size()*sizeof(float)) && ok;
        }
        char result[160];
        sprintf_s(result,"MATCHED_SNAPSHOT complete=%s count=%zu files_ok=%s idle_write=true\n",
                  ok && saved.size()==3 ? "true" : "false",saved.size(),ok ? "true" : "false");
        WriteFile(folder+"\\snapshot.done", result, strlen(result));
        printf("%s", result); fflush(stdout);
        std::vector<Frame>().swap(saved);
        armed = false;
    }
};
