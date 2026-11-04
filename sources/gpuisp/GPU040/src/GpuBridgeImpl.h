// Included after AV_DEVICE_CONTEXT and the normal conversion worker definitions.
// No GPU API is ever called in kernel mode. The stable hardware backend is unchanged.
static BOOLEAN
AvGpuConsumeRaw(PAV_DEVICE_CONTEXT d,const USHORT* raw,ULONG w,ULONG h)
{
    ULONG i,sig=2166136261UL;
    if(InterlockedCompareExchange(&d->GpuMode,0,0)==0)return FALSE;
    if(w!=CODEX_GPU_RAW_WIDTH || h!=CODEX_GPU_RAW_HEIGHT)return FALSE;
    if(KeQueryInterruptTime()-(ULONGLONG)InterlockedCompareExchange64(&d->GpuHeartbeat,0,0)>50000000ULL){
        InterlockedExchange(&d->GpuMode,0);return FALSE; // dead client -> existing CPU path next frame
    }
    for(i=0;i<w*h;i+=257){sig^=raw[i];sig*=16777619UL;}
    ExAcquireFastMutex(&d->RawQueueLock);
    if(d->GpuMode && d->GpuRawBuffer && (ULONG)d->GpuRawSignature!=sig){
        RtlCopyMemory(d->GpuRawBuffer,raw,CODEX_GPU_RAW_BYTES);
        InterlockedExchange(&d->GpuRawSignature,(LONG)sig);
        InterlockedIncrement(&d->GpuRawGeneration);
    }
    InterlockedExchange(&d->RawCopyAccepted,1);
    ExReleaseFastMutex(&d->RawQueueLock);
    return TRUE;
}

static NTSTATUS
AvGpuPropertyBody(PAV_DEVICE_CONTEXT d,PIRP irp,CODEX_GPU_REQUEST* req,PVOID data)
{
    ULONG id=req->Property.Id;
    if(id==CodexGpuInfo){
        CODEX_GPU_INFO out={CODEX_GPU_VERSION};
        out.Mode=d->GpuMode;out.Epoch=d->GpuEpoch;out.RawGeneration=d->GpuRawGeneration;
        out.SubmittedGeneration=d->GpuSubmittedGeneration;out.PublishedCount=d->GpuPublishedCount;
        out.Width=AV_OUTPUT_WIDTH;out.Height=AV_OUTPUT_HEIGHT;out.Pattern=CODEX_GPU_PATTERN;out.Rotate180=CODEX_GPU_ROTATE;
#if CODEX_GPU_VERSION == 2
        out.RawWidth=CODEX_GPU_RAW_WIDTH;out.RawHeight=CODEX_GPU_RAW_HEIGHT;
#endif
        RtlCopyMemory(data,&out,sizeof(out));irp->IoStatus.Information=sizeof(out);return STATUS_SUCCESS;
    }
    if(id==CodexGpuMode){
        ULONG mode=*(ULONG*)data;
        if(mode>1)return STATUS_INVALID_PARAMETER;
        if(mode && (d->ActivePinCount || d->RawConversionBusy))return STATUS_DEVICE_BUSY;
        if(mode && !d->GpuRawBuffer){
            d->GpuRawBuffer=ExAllocatePool2(POOL_FLAG_NON_PAGED,CODEX_GPU_RAW_BYTES,AV_POOL_TAG);
            if(!d->GpuRawBuffer)return STATUS_INSUFFICIENT_RESOURCES;
        }
        ExAcquireFastMutex(&d->RawQueueLock);
        InterlockedExchange(&d->GpuMode,0);
        InterlockedIncrement(&d->GpuEpoch);
        InterlockedExchange(&d->GpuRawGeneration,0);InterlockedExchange(&d->GpuSubmittedGeneration,0);
        InterlockedExchange(&d->GpuRawSignature,0);InterlockedExchange(&d->GpuPublishedCount,0);
        InterlockedExchange64(&d->GpuHeartbeat,(LONG64)KeQueryInterruptTime());
        InterlockedExchange(&d->GpuMode,(LONG)mode);
        ExReleaseFastMutex(&d->RawQueueLock);
        irp->IoStatus.Information=0;return STATUS_SUCCESS;
    }
    if(!d->GpuMode || (ULONG)d->GpuEpoch!=req->Epoch)return STATUS_INVALID_DEVICE_STATE;
    InterlockedExchange64(&d->GpuHeartbeat,(LONG64)KeQueryInterruptTime());
    if(id==CodexGpuRaw){
        CODEX_GPU_FRAME header={CODEX_GPU_VERSION};
        NTSTATUS status=STATUS_DEVICE_NOT_READY;
        ExAcquireFastMutex(&d->RawQueueLock);
        __try {
            if(d->GpuRawBuffer && d->GpuRawGeneration && (ULONG)d->GpuRawGeneration!=req->AfterGeneration){
                header.Epoch=d->GpuEpoch;header.Generation=d->GpuRawGeneration;
                header.Width=CODEX_GPU_RAW_WIDTH;header.Height=CODEX_GPU_RAW_HEIGHT;header.Stride=CODEX_GPU_RAW_WIDTH*2;
                header.Pattern=CODEX_GPU_PATTERN;header.Bytes=CODEX_GPU_RAW_BYTES;
                RtlCopyMemory(data,&header,sizeof(header));
                RtlCopyMemory((PUCHAR)data+sizeof(header),d->GpuRawBuffer,CODEX_GPU_RAW_BYTES);
                irp->IoStatus.Information=sizeof(header)+CODEX_GPU_RAW_BYTES;status=STATUS_SUCCESS;
            }
        } __finally {ExReleaseFastMutex(&d->RawQueueLock);}
        return status;
    }
    if(id==CodexGpuSubmit){
        CODEX_GPU_FRAME header;PULONG old;LARGE_INTEGER delay;ULONG waits=0;
        RtlCopyMemory(&header,data,sizeof(header));
        if(header.Version!=CODEX_GPU_VERSION || header.Epoch!=(ULONG)d->GpuEpoch ||
           header.Width!=AV_OUTPUT_WIDTH || header.Height!=AV_OUTPUT_HEIGHT ||
           header.Stride!=AV_OUTPUT_WIDTH*4 || header.Bytes!=CODEX_GPU_RGB_BYTES ||
           !header.Generation || header.Generation>(ULONG)d->GpuRawGeneration ||
           header.Generation<=(ULONG)d->GpuSubmittedGeneration)return STATUS_INVALID_PARAMETER;
        // CaptureBuffer was the previous published buffer: let all old pin readers retire
        // BEFORE reusing it. New readers only see RgbBuffer, never this scratch.
        delay.QuadPart=-10000;
        while(InterlockedCompareExchange(&d->GpuRgbReaders,0,0)){
            if(++waits>100)return STATUS_DEVICE_BUSY;
            KeDelayExecutionThread(KernelMode,FALSE,&delay);
        }
        RtlCopyMemory(d->CaptureBuffer,(PUCHAR)data+sizeof(header),CODEX_GPU_RGB_BYTES);
        old=InterlockedExchangePointer((PVOID volatile*)&d->RgbBuffer,d->CaptureBuffer);
        d->CaptureBuffer=old;
        InterlockedExchange(&d->GpuSubmittedGeneration,(LONG)header.Generation);
        InterlockedIncrement(&d->GpuPublishedCount);
        InterlockedIncrement(&d->PublishedGeneration);
        InterlockedExchange(&d->PublishedReal,1);
        irp->IoStatus.Information=0;return STATUS_SUCCESS;
    }
    return STATUS_NOT_SUPPORTED;
}

static NTSTATUS
AvGpuProperty(PIRP irp,PKSIDENTIFIER request,PVOID data)
{
    PKSFILTER filter;PKSDEVICE ks;PAV_DEVICE_CONTEXT d;NTSTATUS status;
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL)return STATUS_INVALID_DEVICE_STATE;
    filter=KsGetFilterFromIrp(irp);if(!filter)return STATUS_INVALID_DEVICE_REQUEST;
    ks=KsFilterGetDevice(filter);if(!ks || !ks->Context)return STATUS_DEVICE_NOT_READY;
    d=(PAV_DEVICE_CONTEXT)ks->Context;
    if(!ExAcquireRundownProtection(&d->GpuRundown))return STATUS_DELETE_PENDING;
    ExAcquireFastMutex(&d->GpuIoLock);
    __try {
        if(d->Removing)status=STATUS_DELETE_PENDING;
        else status=AvGpuPropertyBody(d,irp,(CODEX_GPU_REQUEST*)request,data);
    } __except(EXCEPTION_EXECUTE_HANDLER){status=GetExceptionCode();}
    ExReleaseFastMutex(&d->GpuIoLock);ExReleaseRundownProtection(&d->GpuRundown);
    return status;
}

static const KSPROPERTY_ITEM AvGpuPropertyItems[]={
 DEFINE_KSPROPERTY_ITEM(CodexGpuInfo,AvGpuProperty,sizeof(CODEX_GPU_REQUEST),sizeof(CODEX_GPU_INFO),NULL,NULL,0,NULL,NULL,0),
 DEFINE_KSPROPERTY_ITEM(CodexGpuMode,NULL,sizeof(CODEX_GPU_REQUEST),sizeof(ULONG),AvGpuProperty,NULL,0,NULL,NULL,0),
 DEFINE_KSPROPERTY_ITEM(CodexGpuRaw,AvGpuProperty,sizeof(CODEX_GPU_REQUEST),sizeof(CODEX_GPU_FRAME)+CODEX_GPU_RAW_BYTES,NULL,NULL,0,NULL,NULL,0),
 DEFINE_KSPROPERTY_ITEM(CodexGpuSubmit,NULL,sizeof(CODEX_GPU_REQUEST),sizeof(CODEX_GPU_FRAME)+CODEX_GPU_RGB_BYTES,AvGpuProperty,NULL,0,NULL,NULL,0)
};
static const KSPROPERTY_SET AvGpuPropertySets[]={
 {&CodexGpuPropertySet,RTL_NUMBER_OF(AvGpuPropertyItems),AvGpuPropertyItems,0,NULL}
};
static const KSAUTOMATION_TABLE AvGpuAutomation={
 RTL_NUMBER_OF(AvGpuPropertySets),sizeof(KSPROPERTY_ITEM),AvGpuPropertySets,
 0,0,NULL,0,0,NULL
};
