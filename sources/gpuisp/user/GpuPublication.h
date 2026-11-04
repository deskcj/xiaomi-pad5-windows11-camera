#pragma once
#include "FastRgb.h"

// An owned copy of the completed GPU output. Only the publication worker reads
// this job; the producer can safely reuse its RAW and NV12 buffers immediately.
struct GpuPublicationJob {
    CODEX_GPU_FRAME header={};
    std::vector<BYTE> nv12;
};
static void PackGpuPublication(const GpuPublicationJob& job,UINT32 width,UINT32 height,
                               UINT32 pitch,bool rotate,std::vector<BYTE>& output){
    output.resize(sizeof(CODEX_GPU_FRAME)+CODEX_GPU_RGB_BYTES);
    CODEX_GPU_FRAME header=job.header;
    header.Width=1280;header.Height=960;header.Stride=1280*4;header.Bytes=CODEX_GPU_RGB_BYTES;
    memcpy(output.data(),&header,sizeof(header));
    FastRgb32(job.nv12.data(),width,height,pitch,rotate,
              output.data()+sizeof(header),960);
}
