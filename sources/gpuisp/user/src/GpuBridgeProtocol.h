#pragma once
// Shared, pointer-free KS property ABI. Includes windows/ntddk + ks before this header.
static const GUID CodexGpuPropertySet={0x2898a4c1,0x667d,0x4675,{0x91,0xc4,0xa2,0x65,0xee,0x2c,0x19,0x03}};
#ifndef CODEX_GPU_VERSION
#define CODEX_GPU_VERSION 1UL
#endif
#ifndef CODEX_GPU_RAW_WIDTH
#define CODEX_GPU_RAW_WIDTH 1280UL
#define CODEX_GPU_RAW_HEIGHT 960UL
#endif
#define CODEX_GPU_RAW_BYTES (CODEX_GPU_RAW_WIDTH*CODEX_GPU_RAW_HEIGHT*2UL)
#define CODEX_GPU_RGB_BYTES (1280UL*960UL*4UL)
enum { CodexGpuInfo=0,CodexGpuMode=1,CodexGpuRaw=2,CodexGpuSubmit=3 };
typedef struct _CODEX_GPU_REQUEST { KSPROPERTY Property;ULONG AfterGeneration;ULONG Epoch; } CODEX_GPU_REQUEST;
typedef struct _CODEX_GPU_FRAME { ULONG Version,Epoch,Generation,Width,Height,Stride,Pattern,Bytes; } CODEX_GPU_FRAME;
typedef struct _CODEX_GPU_INFO {
    ULONG Version,Mode,Epoch,RawGeneration,SubmittedGeneration,PublishedCount,Width,Height,Pattern,Rotate180;
#if CODEX_GPU_VERSION == 2 || defined(CODEX_GPU_CLIENT)
    ULONG RawWidth,RawHeight;
#endif
} CODEX_GPU_INFO;
