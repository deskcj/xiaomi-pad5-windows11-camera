//
// BackendBridgeRear.c
//
// The AVStream miniport owns no camera hardware.  It asks the physical rear
// KMDF backend for one proven Stage39 RAW frame, then passes that private copy
// to the existing Bayer-to-RGB conversion callback.
//

#include <ntddk.h>
#include <wdf.h>
#include "BackendBridgeRear.h"
#include "..\NabuCamHwRear\NabuCamRearIoctl.h"

#define NABU_REAR_BRIDGE_POOL_TAG 'bRBN'

typedef struct _NABU_REAR_BRIDGE_CONTEXT {
    volatile LONG AcquireCount;
    volatile LONG CaptureCount;
    volatile LONG SessionActive;
    volatile LONG SessionStartCount;
    volatile LONG SessionStopCount;
    volatile LONG SessionRestartCount;
    volatile LONG SessionStartStatus;
    volatile LONG SessionStopStatus;
    volatile LONG PersistentGetCount;
    volatile LONG FallbackCount;
    volatile LONG OpenStatus;
    volatile LONG IoctlStatus;
    volatile LONG OutputBytes;
    volatile LONG RawCompleted;
    volatile LONG RawSignature;
    volatile LONG RawSignatureChanged;
    volatile LONG RawChangedCount;
    volatile LONG RawStaticCount;
    PVOID RawBuffer;
} NABU_REAR_BRIDGE_CONTEXT, *PNABU_REAR_BRIDGE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(
    NABU_REAR_BRIDGE_CONTEXT,
    NabuRearGetBridgeContext);

static NTSTATUS
NabuRearOpenHardware(_Out_ PFILE_OBJECT *FileObject,
                     _Out_ PDEVICE_OBJECT *DeviceObject)
{
    DECLARE_CONST_UNICODE_STRING(
        deviceName,
        L"\\DosDevices\\Global\\NabuCamHwRearControl");

    *FileObject = NULL;
    *DeviceObject = NULL;
    return IoGetDeviceObjectPointer(
        (PUNICODE_STRING)&deviceName,
        FILE_READ_DATA | FILE_WRITE_DATA,
        FileObject,
        DeviceObject);
}

static NTSTATUS
NabuRearSendSimpleIoctl(
    _Inout_ PNABU_REAR_BRIDGE_CONTEXT Context,
    _In_ ULONG IoControlCode)
{
    PFILE_OBJECT fileObject = NULL;
    PDEVICE_OBJECT deviceObject = NULL;
    PIRP irp;
    KEVENT event;
    IO_STATUS_BLOCK ioStatus;
    NTSTATUS status;

    status = NabuRearOpenHardware(&fileObject, &deviceObject);
    InterlockedExchange(&Context->OpenStatus, (LONG)status);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    KeInitializeEvent(&event, NotificationEvent, FALSE);
    RtlZeroMemory(&ioStatus, sizeof(ioStatus));
    irp = IoBuildDeviceIoControlRequest(
        IoControlCode,
        deviceObject,
        NULL,
        0,
        NULL,
        0,
        FALSE,
        &event,
        &ioStatus);
    if (irp == NULL) {
        status = STATUS_INSUFFICIENT_RESOURCES;
    } else {
        status = IoCallDriver(deviceObject, irp);
        if (status == STATUS_PENDING) {
            (VOID)KeWaitForSingleObject(
                &event, Executive, KernelMode, FALSE, NULL);
            status = ioStatus.Status;
        } else if (NT_SUCCESS(status)) {
            status = ioStatus.Status;
        }
    }
    ObDereferenceObject(fileObject);
    return status;
}

static ULONG
NabuRearRawSignature(
    _In_reads_(PixelCount) const USHORT *Raw,
    _In_ ULONG PixelCount)
{
    ULONG signature = 2166136261UL;
    ULONG i;

    // A deterministic sparse FNV-1a signature is sufficient to distinguish
    // a newly exposed frame from a replayed DMA buffer without hashing all
    // 6.5 MB on every refresh.
    for (i = 0; i < PixelCount; i += 257UL) {
        signature ^= Raw[i];
        signature *= 16777619UL;
    }
    signature ^= Raw[PixelCount - 1UL];
    signature *= 16777619UL;
    return signature;
}

NTSTATUS
NabuRearBackendInitialize(_In_ WDFDEVICE Device)
{
    WDF_OBJECT_ATTRIBUTES attributes;
    PNABU_REAR_BRIDGE_CONTEXT context;
    NTSTATUS status;

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(
        &attributes,
        NABU_REAR_BRIDGE_CONTEXT);
    status = WdfObjectAllocateContext(
        Device,
        &attributes,
        (PVOID *)&context);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    RtlZeroMemory(context, sizeof(*context));
    InterlockedExchange(
        &context->OpenStatus,
        (LONG)STATUS_NOT_SUPPORTED);
    InterlockedExchange(
        &context->IoctlStatus,
        (LONG)STATUS_NOT_SUPPORTED);
    InterlockedExchange(
        &context->SessionStartStatus,
        (LONG)STATUS_NOT_SUPPORTED);
    InterlockedExchange(
        &context->SessionStopStatus,
        (LONG)STATUS_NOT_SUPPORTED);
    context->RawBuffer = ExAllocatePool2(
        POOL_FLAG_NON_PAGED,
        NABU_REAR_FRAME_BYTES,
        NABU_REAR_BRIDGE_POOL_TAG);
    if (context->RawBuffer == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
NabuRearBackendAcquire(_In_ WDFDEVICE Device)
{
    PNABU_REAR_BRIDGE_CONTEXT context =
        NabuRearGetBridgeContext(Device);
    NTSTATUS status;

    if (KeGetCurrentIrql() != PASSIVE_LEVEL) {
        return STATUS_INVALID_DEVICE_STATE;
    }
    if (InterlockedCompareExchange(
            &context->SessionActive, 0, 0) != 0) {
        return STATUS_SUCCESS;
    }

    InterlockedIncrement(&context->AcquireCount);
    InterlockedIncrement(&context->SessionStartCount);
    status = NabuRearSendSimpleIoctl(
        context, IOCTL_NABU_CAM_HW_REAR_SESSION_START);
    InterlockedExchange(&context->SessionStartStatus, (LONG)status);
    InterlockedExchange(&context->IoctlStatus, (LONG)status);
    if (NT_SUCCESS(status)) {
        InterlockedExchange(&context->SessionActive, 1);
    }
    return status;
}

VOID
NabuRearBackendRelease(_In_ WDFDEVICE Device)
{
    PNABU_REAR_BRIDGE_CONTEXT context =
        NabuRearGetBridgeContext(Device);
    NTSTATUS status;

    if (KeGetCurrentIrql() != PASSIVE_LEVEL ||
        InterlockedCompareExchange(
            &context->SessionActive, 0, 0) == 0) {
        return;
    }
    InterlockedIncrement(&context->SessionStopCount);
    status = NabuRearSendSimpleIoctl(
        context, IOCTL_NABU_CAM_HW_REAR_SESSION_STOP);
    InterlockedExchange(&context->SessionStopStatus, (LONG)status);
    InterlockedExchange(&context->IoctlStatus, (LONG)status);
    if (NT_SUCCESS(status) || status == STATUS_DEVICE_NOT_READY) {
        InterlockedExchange(&context->SessionActive, 0);
    }
}

NTSTATUS
NabuRearBackendCapture(
    _In_ WDFDEVICE Device,
    _In_ NABU_REAR_RAW_CONSUMER Consumer,
    _In_opt_ PVOID ConsumerContext)
{
    PNABU_REAR_BRIDGE_CONTEXT context =
        NabuRearGetBridgeContext(Device);
    PFILE_OBJECT fileObject = NULL;
    PDEVICE_OBJECT deviceObject = NULL;
    PIRP irp;
    KEVENT event;
    IO_STATUS_BLOCK ioStatus;
    NTSTATUS status;
    ULONG signature;
    ULONG captureIoctl = 0;
    ULONG outputBytes;
    ULONG rawWidth;
    ULONG rawHeight;
    LONG previousSignature;

    if (Consumer == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (KeGetCurrentIrql() != PASSIVE_LEVEL) {
        return STATUS_INVALID_DEVICE_STATE;
    }

    InterlockedIncrement(&context->CaptureCount);
    InterlockedExchange(&context->OutputBytes, 0);
    InterlockedExchange(&context->RawCompleted, 0);
    if (context->RawBuffer == NULL) {
        status = STATUS_DEVICE_NOT_READY;
        goto Finished;
    }

    status = NabuRearBackendAcquire(Device);
    if (NT_SUCCESS(status)) {
        captureIoctl = IOCTL_NABU_CAM_HW_REAR_SESSION_GET_FRAME;
        outputBytes = NABU_REAR_PREVIEW_RAW_BYTES;
        rawWidth = NABU_REAR_PREVIEW_RAW_WIDTH;
        rawHeight = NABU_REAR_PREVIEW_RAW_HEIGHT;
        InterlockedIncrement(&context->PersistentGetCount);
    } else {
        captureIoctl = IOCTL_NABU_CAM_HW_CAPTURE_REAR_RAW_BUFFER;
        outputBytes = NABU_REAR_FRAME_BYTES;
        rawWidth = NABU_REAR_WIDTH;
        rawHeight = NABU_REAR_HEIGHT;
        InterlockedIncrement(&context->FallbackCount);
    }

    status = NabuRearOpenHardware(&fileObject, &deviceObject);
    InterlockedExchange(&context->OpenStatus, (LONG)status);
    if (!NT_SUCCESS(status)) {
        goto Finished;
    }

    KeInitializeEvent(&event, NotificationEvent, FALSE);
    RtlZeroMemory(&ioStatus, sizeof(ioStatus));
    irp = IoBuildDeviceIoControlRequest(
        captureIoctl,
        deviceObject,
        NULL,
        0,
        context->RawBuffer,
        outputBytes,
        FALSE,
        &event,
        &ioStatus);
    if (irp == NULL) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto Finished;
    }

    status = IoCallDriver(deviceObject, irp);
    if (status == STATUS_PENDING) {
        (VOID)KeWaitForSingleObject(
            &event,
            Executive,
            KernelMode,
            FALSE,
            NULL);
        status = ioStatus.Status;
    } else if (NT_SUCCESS(status)) {
        status = ioStatus.Status;
    }
    InterlockedExchange(&context->IoctlStatus, (LONG)status);
    InterlockedExchange(
        &context->OutputBytes,
        (LONG)ioStatus.Information);
    if (!NT_SUCCESS(status)) {
        goto Finished;
    }
    if (ioStatus.Information != outputBytes) {
        status = STATUS_INFO_LENGTH_MISMATCH;
        goto Finished;
    }

    signature = NabuRearRawSignature(
        (const USHORT *)context->RawBuffer,
        rawWidth * rawHeight);
    previousSignature = InterlockedExchange(
        &context->RawSignature, (LONG)signature);
    if (previousSignature == 0 ||
        (ULONG)previousSignature != signature) {
        InterlockedExchange(&context->RawSignatureChanged, 1);
        InterlockedIncrement(&context->RawChangedCount);
    } else {
        InterlockedExchange(&context->RawSignatureChanged, 0);
        InterlockedIncrement(&context->RawStaticCount);
    }

    Consumer(
        ConsumerContext,
        (const USHORT *)context->RawBuffer,
        rawWidth,
        rawHeight);
    InterlockedExchange(&context->RawCompleted, 1);

Finished:
    if (captureIoctl == IOCTL_NABU_CAM_HW_REAR_SESSION_GET_FRAME &&
        !NT_SUCCESS(status) &&
        (status == STATUS_DEVICE_NOT_READY ||
         status == STATUS_IO_DEVICE_ERROR ||
         status == STATUS_IO_TIMEOUT)) {
        /*
         * The physical worker has ended or has not produced its first frame.
         * Do not replay its last buffer forever.  The next AVStream refresh
         * issues SESSION_START again; the physical owner serializes that
         * request with any worker that is still starting.
         */
        if (InterlockedExchange(&context->SessionActive, 0) != 0) {
            InterlockedIncrement(&context->SessionRestartCount);
        }
    }
    if (fileObject != NULL) {
        ObDereferenceObject(fileObject);
    }
    InterlockedExchange(&context->IoctlStatus, (LONG)status);
    return status;
}

VOID
NabuRearBackendWriteDiagnostics(
    _In_ WDFDEVICE Device,
    _In_ NABU_REAR_DIAGNOSTIC_WRITER Writer,
    _In_opt_ PVOID WriterContext)
{
    PNABU_REAR_BRIDGE_CONTEXT context;

    if (Device == NULL || Writer == NULL) {
        return;
    }
    context = NabuRearGetBridgeContext(Device);
    Writer(
        WriterContext,
        L"NabuAvRear_BridgeAcquireCount",
        (ULONG)InterlockedCompareExchange(
            &context->AcquireCount, 0, 0));
    Writer(
        WriterContext,
        L"NabuAvRear_BridgeCaptureCount",
        (ULONG)InterlockedCompareExchange(
            &context->CaptureCount, 0, 0));
    Writer(
        WriterContext,
        L"NabuAvRear_BridgeSessionActive",
        (ULONG)InterlockedCompareExchange(
            &context->SessionActive, 0, 0));
    Writer(
        WriterContext,
        L"NabuAvRear_BridgeSessionStartCount",
        (ULONG)InterlockedCompareExchange(
            &context->SessionStartCount, 0, 0));
    Writer(
        WriterContext,
        L"NabuAvRear_BridgeSessionStopCount",
        (ULONG)InterlockedCompareExchange(
            &context->SessionStopCount, 0, 0));
    Writer(
        WriterContext,
        L"NabuAvRear_BridgeSessionRestartCount",
        (ULONG)InterlockedCompareExchange(
            &context->SessionRestartCount, 0, 0));
    Writer(
        WriterContext,
        L"NabuAvRear_BridgeSessionStartStatus",
        (ULONG)InterlockedCompareExchange(
            &context->SessionStartStatus, 0, 0));
    Writer(
        WriterContext,
        L"NabuAvRear_BridgeSessionStopStatus",
        (ULONG)InterlockedCompareExchange(
            &context->SessionStopStatus, 0, 0));
    Writer(
        WriterContext,
        L"NabuAvRear_BridgePersistentGetCount",
        (ULONG)InterlockedCompareExchange(
            &context->PersistentGetCount, 0, 0));
    Writer(
        WriterContext,
        L"NabuAvRear_BridgeFallbackCount",
        (ULONG)InterlockedCompareExchange(
            &context->FallbackCount, 0, 0));
    Writer(
        WriterContext,
        L"NabuAvRear_BridgeOpenStatus",
        (ULONG)InterlockedCompareExchange(
            &context->OpenStatus, 0, 0));
    Writer(
        WriterContext,
        L"NabuAvRear_BridgeIoctlStatus",
        (ULONG)InterlockedCompareExchange(
            &context->IoctlStatus, 0, 0));
    Writer(
        WriterContext,
        L"NabuAvRear_BridgeOutputBytes",
        (ULONG)InterlockedCompareExchange(
            &context->OutputBytes, 0, 0));
    Writer(
        WriterContext,
        L"NabuAvRear_BridgeRawCompleted",
        (ULONG)InterlockedCompareExchange(
            &context->RawCompleted, 0, 0));
    Writer(
        WriterContext,
        L"NabuAvRear_BridgeRawSignature",
        (ULONG)InterlockedCompareExchange(
            &context->RawSignature, 0, 0));
    Writer(
        WriterContext,
        L"NabuAvRear_BridgeRawSignatureChanged",
        (ULONG)InterlockedCompareExchange(
            &context->RawSignatureChanged, 0, 0));
    Writer(
        WriterContext,
        L"NabuAvRear_BridgeRawChangedCount",
        (ULONG)InterlockedCompareExchange(
            &context->RawChangedCount, 0, 0));
    Writer(
        WriterContext,
        L"NabuAvRear_BridgeRawStaticCount",
        (ULONG)InterlockedCompareExchange(
            &context->RawStaticCount, 0, 0));
}

VOID
NabuRearBackendCleanup(_In_ WDFDEVICE Device)
{
    PNABU_REAR_BRIDGE_CONTEXT context =
        NabuRearGetBridgeContext(Device);

    NabuRearBackendRelease(Device);

    if (context->RawBuffer != NULL) {
        ExFreePoolWithTag(
            context->RawBuffer,
            NABU_REAR_BRIDGE_POOL_TAG);
        context->RawBuffer = NULL;
    }
}
