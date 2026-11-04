//
// BackendBridgeFront.c
//
// The AVStream miniport deliberately owns no camera hardware.  It requests
// one proven Stage70 RAW frame from the full KMDF hardware backend and then
// hands that frame to the existing Bayer/RGB conversion callback.
//
#include <ntddk.h>
#include <wdf.h>
#include <initguid.h>
#include <devpkey.h>
#include <acpitabl.h>
#include "HwCaptureFront.h"
#include "NabuCamOwnIoctl.h"

#define NABU_BRIDGE_POOL_TAG 'BRBN'

typedef struct _NABU_FRONT_BRIDGE_CONTEXT {
    volatile LONG AcquireCount;
    volatile LONG CaptureCount;
    volatile LONG SessionActive;
    volatile LONG SessionStartCount;
    volatile LONG SessionStopCount;
    volatile LONG SessionStartStatus;
    volatile LONG SessionStopStatus;
    volatile LONG PersistentGetCount;
    volatile LONG FallbackCount;
    volatile LONG OpenStatus;
    volatile LONG IoctlStatus;
    volatile LONG OutputBytes;
    volatile LONG RawCompleted;
    volatile LONG IdentityIoctlStatus;
    volatile LONG PhysicalLocationQueryStatus;
    volatile LONG PhysicalLocationSetStatus;
    volatile LONG PhysicalLocationBytes;
    volatile LONG PhysicalLocationSynthesized;
    volatile LONG PhysicalLocationPanel;
    volatile LONG ContainerIdQueryStatus;
    volatile LONG ContainerIdSetStatus;
    volatile LONG ContainerIdBytes;
    PVOID RawBuffer;
} NABU_FRONT_BRIDGE_CONTEXT, *PNABU_FRONT_BRIDGE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(
    NABU_FRONT_BRIDGE_CONTEXT,
    NabuFrontGetBridgeContext);

static NTSTATUS
NabuFrontOpenHardware(
    _Out_ PFILE_OBJECT *FileObject,
    _Out_ PDEVICE_OBJECT *DeviceObject)
{
    DECLARE_CONST_UNICODE_STRING(
        deviceName,
        L"\\DosDevices\\Global\\NabuCamHwFrontControl");

    *FileObject = NULL;
    *DeviceObject = NULL;
    return IoGetDeviceObjectPointer(
        (PUNICODE_STRING)&deviceName,
        FILE_READ_DATA | FILE_WRITE_DATA,
        FileObject,
        DeviceObject);
}

static NTSTATUS
NabuFrontSendSimpleIoctl(
    _Inout_ PNABU_FRONT_BRIDGE_CONTEXT Context,
    _In_ ULONG IoControlCode)
{
    PFILE_OBJECT fileObject = NULL;
    PDEVICE_OBJECT deviceObject = NULL;
    PIRP irp;
    KEVENT event;
    IO_STATUS_BLOCK ioStatus;
    NTSTATUS status;

    status = NabuFrontOpenHardware(&fileObject, &deviceObject);
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
                &event,
                Executive,
                KernelMode,
                FALSE,
                NULL);
            status = ioStatus.Status;
        } else if (NT_SUCCESS(status)) {
            status = ioStatus.Status;
        }
    }
    ObDereferenceObject(fileObject);
    return status;
}

NTSTATUS
NabuFrontBackendInitialize(_In_ WDFDEVICE Device)
{
    WDF_OBJECT_ATTRIBUTES attributes;
    PNABU_FRONT_BRIDGE_CONTEXT context;
    NTSTATUS status;

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(
        &attributes,
        NABU_FRONT_BRIDGE_CONTEXT);
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
        NABU_FRONT_FRAME_BYTES,
        NABU_BRIDGE_POOL_TAG);
    if (context->RawBuffer == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
NabuFrontBackendAcquire(_In_ WDFDEVICE Device)
{
    PNABU_FRONT_BRIDGE_CONTEXT context =
        NabuFrontGetBridgeContext(Device);
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
    status = NabuFrontSendSimpleIoctl(
        context,
        IOCTL_NABU_CAM_HW_FRONT_SESSION_START);
    InterlockedExchange(
        &context->SessionStartStatus, (LONG)status);
    InterlockedExchange(&context->IoctlStatus, (LONG)status);
    if (NT_SUCCESS(status)) {
        InterlockedExchange(&context->SessionActive, 1);
    }
    return status;
}

NTSTATUS
NabuFrontBackendPublishPhysicalProperties(
    _In_ WDFDEVICE Device,
    _In_ PDEVICE_OBJECT VirtualPdo)
{
    PNABU_FRONT_BRIDGE_CONTEXT context =
        NabuFrontGetBridgeContext(Device);
    NABU_CAM_DEVICE_IDENTITY identity;
    PFILE_OBJECT fileObject = NULL;
    PDEVICE_OBJECT deviceObject = NULL;
    PIRP irp;
    KEVENT event;
    IO_STATUS_BLOCK ioStatus;
    NTSTATUS status;
    NTSTATUS setStatus;
    ACPI_PLD_V2_BUFFER synthesizedPld;
    PVOID physicalLocation;
    ULONG physicalLocationBytes;

    if (KeGetCurrentIrql() != PASSIVE_LEVEL) {
        return STATUS_INVALID_DEVICE_STATE;
    }
    if (VirtualPdo == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(&identity, sizeof(identity));
    status = NabuFrontOpenHardware(&fileObject, &deviceObject);
    InterlockedExchange(&context->OpenStatus, (LONG)status);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    KeInitializeEvent(&event, NotificationEvent, FALSE);
    RtlZeroMemory(&ioStatus, sizeof(ioStatus));
    irp = IoBuildDeviceIoControlRequest(
        IOCTL_NABU_CAM_HW_GET_DEVICE_IDENTITY,
        deviceObject,
        NULL,
        0,
        &identity,
        sizeof(identity),
        FALSE,
        &event,
        &ioStatus);
    if (irp == NULL) {
        status = STATUS_INSUFFICIENT_RESOURCES;
    } else {
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
    }
    InterlockedExchange(&context->IdentityIoctlStatus, (LONG)status);
    if (!NT_SUCCESS(status) ||
        ioStatus.Information < sizeof(identity) ||
        identity.Size != sizeof(identity)) {
        if (NT_SUCCESS(status)) {
            status = STATUS_INFO_LENGTH_MISMATCH;
        }
        goto Finished;
    }

    InterlockedExchange(
        &context->PhysicalLocationQueryStatus,
        (LONG)identity.PhysicalLocationStatus);
    physicalLocation = identity.PhysicalLocation;
    physicalLocationBytes = identity.PhysicalLocationBytes;
    if (NT_SUCCESS((NTSTATUS)identity.PhysicalLocationStatus) &&
        identity.PhysicalLocationType == DEVPROP_TYPE_BINARY &&
        identity.PhysicalLocationBytes != 0 &&
        identity.PhysicalLocationBytes <=
            sizeof(identity.PhysicalLocation)) {
        InterlockedExchange(
            &context->PhysicalLocationSynthesized, 0);
    } else {
        /*
         * Xiaomi's ACPI node has no _PLD.  Publish the standard ACPI
         * revision-2 descriptor expected by Windows camera discovery.
         * This describes a user-visible camera centered in the front panel;
         * dynamic display rotation remains the responsibility of Windows.
         */
        RtlZeroMemory(&synthesizedPld, sizeof(synthesizedPld));
        synthesizedPld.Revision = 2;
        synthesizedPld.IgnoreColor = 1;
        synthesizedPld.UserVisible = 1;
        synthesizedPld.Panel = AcpiPldPanelFront;
        synthesizedPld.VerticalPosition = AcpiPldVPosCenter;
        synthesizedPld.HorizontalPosition = AcpiPldHPosCenter;
        synthesizedPld.Shape = AcpiPldShapeUnknown;
        synthesizedPld.Rotation = AcpiPldRotation0;
        synthesizedPld.VerticalOffset = ACPI_PLD_OFFSET_NONE;
        synthesizedPld.HorizontalOffset = ACPI_PLD_OFFSET_NONE;
        physicalLocation = &synthesizedPld;
        physicalLocationBytes = sizeof(synthesizedPld);
        InterlockedExchange(
            &context->PhysicalLocationSynthesized, 1);
    }
    InterlockedExchange(
        &context->PhysicalLocationPanel, AcpiPldPanelFront);
    InterlockedExchange(
        &context->PhysicalLocationBytes,
        (LONG)physicalLocationBytes);
    setStatus = IoSetDevicePropertyData(
        VirtualPdo,
        &DEVPKEY_Device_PhysicalDeviceLocation,
        LOCALE_NEUTRAL,
        PLUGPLAY_PROPERTY_PERSISTENT,
        DEVPROP_TYPE_BINARY,
        physicalLocationBytes,
        physicalLocation);
    InterlockedExchange(
        &context->PhysicalLocationSetStatus, (LONG)setStatus);

    InterlockedExchange(
        &context->ContainerIdQueryStatus,
        (LONG)identity.ContainerIdStatus);
    InterlockedExchange(
        &context->ContainerIdBytes,
        (LONG)identity.ContainerIdBytes);
    if (NT_SUCCESS((NTSTATUS)identity.ContainerIdStatus) &&
        identity.ContainerIdType == DEVPROP_TYPE_GUID &&
        identity.ContainerIdBytes == sizeof(identity.ContainerId)) {
        setStatus = IoSetDevicePropertyData(
            VirtualPdo,
            &DEVPKEY_Device_ContainerId,
            LOCALE_NEUTRAL,
            PLUGPLAY_PROPERTY_PERSISTENT,
            DEVPROP_TYPE_GUID,
            sizeof(identity.ContainerId),
            &identity.ContainerId);
    } else {
        setStatus = (NTSTATUS)identity.ContainerIdStatus;
    }
    InterlockedExchange(
        &context->ContainerIdSetStatus, (LONG)setStatus);
    status = (NTSTATUS)InterlockedCompareExchange(
        &context->PhysicalLocationSetStatus, 0, 0);

Finished:
    ObDereferenceObject(fileObject);
    return status;
}

VOID
NabuFrontBackendRelease(_In_ WDFDEVICE Device)
{
    PNABU_FRONT_BRIDGE_CONTEXT context =
        NabuFrontGetBridgeContext(Device);
    NTSTATUS status;

    if (KeGetCurrentIrql() != PASSIVE_LEVEL ||
        InterlockedCompareExchange(
            &context->SessionActive, 0, 0) == 0) {
        return;
    }
    InterlockedIncrement(&context->SessionStopCount);
    status = NabuFrontSendSimpleIoctl(
        context,
        IOCTL_NABU_CAM_HW_FRONT_SESSION_STOP);
    InterlockedExchange(
        &context->SessionStopStatus, (LONG)status);
    InterlockedExchange(&context->IoctlStatus, (LONG)status);
    if (NT_SUCCESS(status) ||
        status == STATUS_DEVICE_NOT_READY) {
        InterlockedExchange(&context->SessionActive, 0);
    }
}

NTSTATUS
NabuFrontBackendCapture(
    _In_ WDFDEVICE Device,
    _In_ NABU_FRONT_RAW_CONSUMER Consumer,
    _In_opt_ PVOID ConsumerContext)
{
    PNABU_FRONT_BRIDGE_CONTEXT context =
        NabuFrontGetBridgeContext(Device);
    PFILE_OBJECT fileObject = NULL;
    PDEVICE_OBJECT deviceObject = NULL;
    PVOID rawBuffer = NULL;
    PIRP irp;
    KEVENT event;
    IO_STATUS_BLOCK ioStatus;
    NTSTATUS status;
    ULONG captureIoctl;
    ULONG outputBytes;
    ULONG rawWidth;
    ULONG rawHeight;

    if (Consumer == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (KeGetCurrentIrql() != PASSIVE_LEVEL) {
        return STATUS_INVALID_DEVICE_STATE;
    }

    InterlockedIncrement(&context->CaptureCount);
    InterlockedExchange(&context->OutputBytes, 0);
    InterlockedExchange(&context->RawCompleted, 0);

    status = NabuFrontBackendAcquire(Device);
    if (NT_SUCCESS(status)) {
        captureIoctl =
            IOCTL_NABU_CAM_HW_FRONT_SESSION_GET_FRAME;
        outputBytes = NABU_FRONT_PREVIEW_RAW_BYTES;
        rawWidth = NABU_FRONT_PREVIEW_RAW_WIDTH;
        rawHeight = NABU_FRONT_PREVIEW_RAW_HEIGHT;
        InterlockedIncrement(&context->PersistentGetCount);
    } else {
        /*
         * A failed START performs complete backend cleanup.  Preserve the
         * already-proven AV23 path as a safe fallback for this frame.
         */
        captureIoctl =
            IOCTL_NABU_CAM_HW_CAPTURE_FRONT_RAW_BUFFER;
        outputBytes = NABU_FRONT_FRAME_BYTES;
        rawWidth = NABU_FRONT_WIDTH;
        rawHeight = NABU_FRONT_HEIGHT;
        InterlockedIncrement(&context->FallbackCount);
    }

    rawBuffer = context->RawBuffer;
    if (rawBuffer == NULL) {
        status = STATUS_DEVICE_NOT_READY;
        goto Finished;
    }

    status = NabuFrontOpenHardware(&fileObject, &deviceObject);
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
        rawBuffer,
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

    Consumer(
        ConsumerContext,
        (const USHORT *)rawBuffer,
        rawWidth,
        rawHeight);
    InterlockedExchange(&context->RawCompleted, 1);

Finished:
    if (fileObject != NULL) {
        ObDereferenceObject(fileObject);
    }
    InterlockedExchange(&context->IoctlStatus, (LONG)status);
    return status;
}

VOID
NabuFrontBackendWriteDiagnostics(
    _In_ WDFDEVICE Device,
    _In_ NABU_FRONT_DIAGNOSTIC_WRITER Writer,
    _In_opt_ PVOID WriterContext)
{
    PNABU_FRONT_BRIDGE_CONTEXT context;

    if (Device == NULL || Writer == NULL) {
        return;
    }
    context = NabuFrontGetBridgeContext(Device);
    Writer(
        WriterContext,
        L"NabuAv_BridgeAcquireCount",
        (ULONG)InterlockedCompareExchange(
            &context->AcquireCount, 0, 0));
    Writer(
        WriterContext,
        L"NabuAv_BridgeCaptureCount",
        (ULONG)InterlockedCompareExchange(
            &context->CaptureCount, 0, 0));
    Writer(
        WriterContext,
        L"NabuAv_BridgeSessionActive",
        (ULONG)InterlockedCompareExchange(
            &context->SessionActive, 0, 0));
    Writer(
        WriterContext,
        L"NabuAv_BridgeSessionStartCount",
        (ULONG)InterlockedCompareExchange(
            &context->SessionStartCount, 0, 0));
    Writer(
        WriterContext,
        L"NabuAv_BridgeSessionStopCount",
        (ULONG)InterlockedCompareExchange(
            &context->SessionStopCount, 0, 0));
    Writer(
        WriterContext,
        L"NabuAv_BridgeSessionStartStatus",
        (ULONG)InterlockedCompareExchange(
            &context->SessionStartStatus, 0, 0));
    Writer(
        WriterContext,
        L"NabuAv_BridgeSessionStopStatus",
        (ULONG)InterlockedCompareExchange(
            &context->SessionStopStatus, 0, 0));
    Writer(
        WriterContext,
        L"NabuAv_BridgePersistentGetCount",
        (ULONG)InterlockedCompareExchange(
            &context->PersistentGetCount, 0, 0));
    Writer(
        WriterContext,
        L"NabuAv_BridgeFallbackCount",
        (ULONG)InterlockedCompareExchange(
            &context->FallbackCount, 0, 0));
    Writer(
        WriterContext,
        L"NabuAv_BridgeOpenStatus",
        (ULONG)InterlockedCompareExchange(
            &context->OpenStatus, 0, 0));
    Writer(
        WriterContext,
        L"NabuAv_BridgeIoctlStatus",
        (ULONG)InterlockedCompareExchange(
            &context->IoctlStatus, 0, 0));
    Writer(
        WriterContext,
        L"NabuAv_BridgeOutputBytes",
        (ULONG)InterlockedCompareExchange(
            &context->OutputBytes, 0, 0));
    Writer(
        WriterContext,
        L"NabuAv_BridgeRawCompleted",
        (ULONG)InterlockedCompareExchange(
            &context->RawCompleted, 0, 0));
    Writer(
        WriterContext,
        L"NabuAv_IdentityIoctlStatus",
        (ULONG)InterlockedCompareExchange(
            &context->IdentityIoctlStatus, 0, 0));
    Writer(
        WriterContext,
        L"NabuAv_PhysicalLocationQueryStatus",
        (ULONG)InterlockedCompareExchange(
            &context->PhysicalLocationQueryStatus, 0, 0));
    Writer(
        WriterContext,
        L"NabuAv_PhysicalLocationSetStatus",
        (ULONG)InterlockedCompareExchange(
            &context->PhysicalLocationSetStatus, 0, 0));
    Writer(
        WriterContext,
        L"NabuAv_PhysicalLocationBytes",
        (ULONG)InterlockedCompareExchange(
            &context->PhysicalLocationBytes, 0, 0));
    Writer(
        WriterContext,
        L"NabuAv_PhysicalLocationSynthesized",
        (ULONG)InterlockedCompareExchange(
            &context->PhysicalLocationSynthesized, 0, 0));
    Writer(
        WriterContext,
        L"NabuAv_PhysicalLocationPanel",
        (ULONG)InterlockedCompareExchange(
            &context->PhysicalLocationPanel, 0, 0));
    Writer(
        WriterContext,
        L"NabuAv_ContainerIdQueryStatus",
        (ULONG)InterlockedCompareExchange(
            &context->ContainerIdQueryStatus, 0, 0));
    Writer(
        WriterContext,
        L"NabuAv_ContainerIdSetStatus",
        (ULONG)InterlockedCompareExchange(
            &context->ContainerIdSetStatus, 0, 0));
    Writer(
        WriterContext,
        L"NabuAv_ContainerIdBytes",
        (ULONG)InterlockedCompareExchange(
            &context->ContainerIdBytes, 0, 0));
}

VOID
NabuFrontBackendCleanup(_In_ WDFDEVICE Device)
{
    PNABU_FRONT_BRIDGE_CONTEXT context =
        NabuFrontGetBridgeContext(Device);

    NabuFrontBackendRelease(Device);
    if (context->RawBuffer != NULL) {
        ExFreePoolWithTag(
            context->RawBuffer, NABU_BRIDGE_POOL_TAG);
        context->RawBuffer = NULL;
    }
}
