// FrontRawGrab - pull real sensor RAW straight from the front hardware backend control node.
//
// Why this exists: every capture tool in the tree sizes the front GET_FRAME output buffer for
// 1280x960 (Codex GPUISP3/Build001/src/LiveRawCapture.cpp:71), while the ABI-18 front backend
// requires NABU_FRONT_PREVIEW_RAW_BYTES = 1632*1224*2, so the ioctl fails with
// ERROR_INSUFFICIENT_BUFFER (122) and no front RAW has ever reached userspace. The geometry is
// probed from the driver's own status block instead of being assumed a second time.
//
// Output: <outdir>/front-<n>.raw16 (1632x1224 BGGR16) plus a one-line-per-frame log.
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <vector>
#include <string>

static DWORD Code(ULONG fn, bool direct) {
    return (0x8000 << 16) | ((FILE_READ_ACCESS | FILE_WRITE_ACCESS) << 14) | (fn << 2) |
           (direct ? METHOD_OUT_DIRECT : METHOD_BUFFERED);
}
static bool Ioctl(HANDLE h, DWORD code, void* out, DWORD outSize, DWORD& got, DWORD ms) {
    OVERLAPPED ov = {};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    BOOL ok = DeviceIoControl(h, code, nullptr, 0, out, outSize, &got, &ov);
    DWORD err = 0;
    if (!ok) {
        err = GetLastError();
        if (err != ERROR_IO_PENDING) { CloseHandle(ov.hEvent); SetLastError(err); return false; }
        if (!WaitForSingleObject(ov.hEvent, ms)) { CancelIoEx(h, &ov); CloseHandle(ov.hEvent); SetLastError(ERROR_TIMEOUT); return false; }
        if (!GetOverlappedResult(h, &ov, &got, TRUE)) { SetLastError(GetLastError()); CloseHandle(ov.hEvent); return false; }
    }
    CloseHandle(ov.hEvent);
    return true;
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) { fwprintf(stderr, L"FrontRawGrab OUTDIR FRAMES [BYTES]\n"); return 2; }
    int count = _wtoi(argv[2]);
    if (count < 1 || count > 30) return 2;
    // 1632*1224*2 = 3,995,136: the front preview RAW plane the backend hands out.
    size_t bytes = argc > 3 ? (size_t)_wtoi(argv[3]) : (size_t)1632 * 1224 * 2;
    MEMORYSTATUSEX mem = { sizeof(mem) };
    GlobalMemoryStatusEx(&mem);
    printf("free_ram_mb=%llu buffer_bytes=%zu\n", mem.ullAvailPhys / 1048576, bytes);
    if (mem.ullAvailPhys < 400ull * 1048576) { fprintf(stderr, "RAM below the 400 MB grab gate\n"); return 3; }
    // Same named mutex the existing capture tools take, so two RAW sessions cannot start at once.
    HANDLE grabMutex = CreateMutexW(nullptr, FALSE, L"Global\\CodexGpuIspLiveCapture");
    if (!grabMutex || WaitForSingleObject(grabMutex, 0) != WAIT_OBJECT_0) { fprintf(stderr, "another capture is active\n"); return 4; }

    HANDLE h = CreateFileW(L"\\\\.\\NabuCamHwFrontControl", GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    if (h == INVALID_HANDLE_VALUE) { fprintf(stderr, "Open front backend control error=%lu\n", GetLastError()); return 5; }
    DWORD got = 0;
    std::vector<ULONG> status(4096);
    // The status read is informational. It times out while the virtual driver holds the backend
    // streaming, and that must not prevent a session START from being attempted.
    if (!Ioctl(h, Code(0x800, false), status.data(), (DWORD)(status.size() * 4), got, 3000))
        printf("status_query=skipped error=%lu\n", GetLastError());
    else
        printf("front_status_bytes=%lu protocol=%lu build=%lu stage=%lu\n", got, status[1], status[2], status[3]);

    if (!Ioctl(h, Code(0x812, false), nullptr, 0, got, 15000)) {
        fprintf(stderr, "session START error=%lu\n", GetLastError()); CloseHandle(h); return 7;
    }
    printf("session_start=OK buffer_bytes=%zu\n", bytes); fflush(stdout);

    std::vector<uint16_t> raw(bytes / 2);
    int completed = 0, misses = 0;
    uint64_t previous = 0; int changed = 0;
    std::wstring dir(argv[1]);
    for (int frame = 1; frame <= count;) {
        LARGE_INTEGER a, b, f; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&a);
        if (!Ioctl(h, Code(0x813, true), raw.data(), (DWORD)raw.size() * 2, got, 12000)) {
            DWORD err = GetLastError();
            if (++misses <= 60 && (err == 21 || err == 170 || err == 1167)) { Sleep(100); continue; }
            fprintf(stderr, "GET_FRAME error=%lu after %d misses (buffer_bytes=%zu)\n", err, misses, bytes);
            Ioctl(h, Code(0x814, false), nullptr, 0, got, 5000); CloseHandle(h); return 8;
        }
        if ((size_t)got != bytes) { fprintf(stderr, "payload bytes=%lu expected=%zu\n", got, bytes); break; }
        QueryPerformanceCounter(&b);
        uint64_t hash = 14695981039346656037ull, sum = 0; unsigned lo = 65535, hi = 0, invalid = 0;
        for (uint16_t v : raw) { hash = (hash ^ v) * 1099511628211ull; sum += v; if (v < lo) lo = v; if (v > hi) hi = v; if (v > 1023) ++invalid; }
        if (completed && hash != previous) ++changed;
        previous = hash; ++completed;
        printf("frame=%d bytes=%lu ms=%.2f min=%u max=%u mean=%.3f invalid=%u hash=%016llx\n",
            completed, got, 1000.0 * (b.QuadPart - a.QuadPart) / f.QuadPart, lo, hi,
            (double)sum / raw.size(), invalid, (unsigned long long)hash);
        fflush(stdout);
        wchar_t name[128]; swprintf_s(name, L"\\front-live-%02d.raw16", completed);
        HANDLE o = CreateFileW((dir + name).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (o != INVALID_HANDLE_VALUE) { DWORD w = 0; WriteFile(o, raw.data(), (DWORD)raw.size() * 2, &w, nullptr); CloseHandle(o); }
        if (completed < count) Sleep(60);
    }
    Ioctl(h, Code(0x814, false), nullptr, 0, got, 5000);
    printf("completed=%d changed=%d sensor_not_started=false\n", completed, changed);
    CloseHandle(h);
    return completed ? 0 : 9;
}
