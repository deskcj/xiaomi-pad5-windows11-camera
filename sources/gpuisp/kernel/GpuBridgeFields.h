    EX_RUNDOWN_REF GpuRundown;
    FAST_MUTEX GpuIoLock;
    KSPIN_LOCK GpuRgbMetadataLock;
    PUSHORT GpuRawBuffer;
    volatile LONG GpuMode,GpuEpoch,GpuRawGeneration,GpuSubmittedGeneration,GpuPublishedCount;
    volatile LONG GpuRawSignature,GpuRgbReaders;
    volatile LONG64 GpuHeartbeat;
