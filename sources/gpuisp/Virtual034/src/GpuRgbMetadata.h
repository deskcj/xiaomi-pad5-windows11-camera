#pragma once
// Both CPU/GPU publishers commit pointer+generation together. A pin retains
// that exact generation while copying; later publications cannot mark an
// unseen frame delivered. Only the short metadata commit holds a spin lock.
typedef struct _AV_GPU_RGB_LEASE {PULONG Pixels;LONG Generation;} AV_GPU_RGB_LEASE;
static BOOLEAN AvGpuAcquireRgb(PAV_DEVICE_CONTEXT d,LONG after,AV_GPU_RGB_LEASE* lease){
    KIRQL irql;LONG generation;PULONG pixels;
    KeAcquireSpinLock(&d->GpuRgbMetadataLock,&irql);
    generation=InterlockedCompareExchange(&d->PublishedGeneration,0,0);
    pixels=d->RgbBuffer;
    if(!pixels||generation<=after||InterlockedCompareExchange(&d->PublishedReal,0,0)==0){
        KeReleaseSpinLock(&d->GpuRgbMetadataLock,irql);return FALSE;
    }
    InterlockedIncrement(&d->GpuRgbReaders);
    lease->Pixels=pixels;lease->Generation=generation;
    KeReleaseSpinLock(&d->GpuRgbMetadataLock,irql);return TRUE;
}
static VOID AvGpuReleaseRgb(PAV_DEVICE_CONTEXT d){InterlockedDecrement(&d->GpuRgbReaders);}
static VOID AvGpuPublishRgb(PAV_DEVICE_CONTEXT d){
    KIRQL irql;PULONG old;
    KeAcquireSpinLock(&d->GpuRgbMetadataLock,&irql);
    old=(PULONG)InterlockedExchangePointer((PVOID volatile*)&d->RgbBuffer,d->CaptureBuffer);
    d->CaptureBuffer=old;
    InterlockedIncrement(&d->PublishedGeneration);
    InterlockedExchange(&d->PublishedReal,1);
    KeReleaseSpinLock(&d->GpuRgbMetadataLock,irql);
}
// PASSIVE/APC worker only, outside the metadata spin lock. The back buffer
// was the previous front: wait for its old readers before reusing its bytes.
static BOOLEAN AvGpuWaitRgbReaders(PAV_DEVICE_CONTEXT d){
    LARGE_INTEGER delay;ULONG waits=0;delay.QuadPart=-10000;
    while(InterlockedCompareExchange(&d->GpuRgbReaders,0,0)){
        if(++waits>100)return FALSE;
        KeDelayExecutionThread(KernelMode,FALSE,&delay);
    }
    return TRUE;
}
