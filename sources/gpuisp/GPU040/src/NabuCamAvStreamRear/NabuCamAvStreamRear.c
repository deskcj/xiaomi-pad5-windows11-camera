//
// NabuCamAvStreamRear.c
//
// AVStream preview wrapper for the proven Stage39 rear-camera capture.
// The first KSSTATE_RUN performs one guarded optical capture at PASSIVE_LEVEL,
// converts it to RGB32 and then keeps publishing that accepted frame.  It does
// not repeatedly execute the older Stage39 VCM teardown path.
//

#define AVSTREAM_BUILD 1

#include <ntddk.h>
#include <wdf.h>
#include <wdfminiport.h>
#include <initguid.h>
#include <devpkey.h>
#include <acpitabl.h>
#include <windef.h>
#define NOBITMAP
#include <mmreg.h>
#undef NOBITMAP
#include <unknown.h>
// The ARM64 kernel C headers leave CDECL as a compiler calling-convention
// token, while ks.h also uses it on GUID data declarations.  AVStream's
// official samples are C++; make those declarations valid in this C unit.
#ifdef CDECL
#undef CDECL
#endif
#define CDECL
#include <ks.h>
#include <ksmedia.h>
#include "HwCapture.h"

#pragma warning(disable:4100)
#pragma warning(disable:4201)

#define AV_POOL_TAG           'RVAN'
#define AV_OUTPUT_WIDTH       640UL
#define AV_OUTPUT_HEIGHT      480UL
#define AV_RGB_STRIDE         (AV_OUTPUT_WIDTH * 4UL)
#define AV_RGB_FRAME_BYTES    (AV_RGB_STRIDE * AV_OUTPUT_HEIGHT)
#define AV_TARGET_FPS         30UL
#define AV_TIMER_PERIOD_MS    33UL
#define AV_FRAME_INTERVAL     (10000000ULL / AV_TARGET_FPS)
#define AV_REAR_INITIAL_DELAY_MS 500UL
#define AV_REAR_RETRY_DELAY_MS   350UL
#define AV_REAR_MAX_ATTEMPTS     8UL
#define AV_BLACK_LEVEL        64UL
#define AV_GAIN_ONE           1024UL
#define AV_STATIC_RGB32       \
    0xe436eb7e, 0x524f, 0x11ce, 0x9f, 0x53, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70

typedef struct _AV_DEVICE_CONTEXT {
    PKSDEVICE KsDevice;
    WDFDEVICE WdfDevice;
    PNABU_OWN_STAGE23_CONTEXT Hardware;
    HANDLE DiagnosticKey;
    PULONG volatile RgbBuffer;
    PULONG CaptureBuffer;
    WDFWORKITEM CaptureWorkItem;
    volatile LONG CaptureAttempted;
    volatile LONG CaptureQueued;
    volatile LONG CaptureStatus;
    volatile LONG CaptureRetryCount;
    volatile LONG CaptureSuccessAttempt;
    volatile LONG CaptureActivationStatus;
    volatile LONG PublishedReal;
    volatile LONG RawMinimum;
    volatile LONG RawMaximum;
    volatile LONG RawMean;
    volatile LONG RawNonZero;
    volatile LONG ConversionCompleted;
    volatile LONG ActivePinCount;
    volatile LONG PinCreateCount;
    volatile LONG PinCloseCount;
    volatile LONG RunCount;
    volatile LONG NonRunCount;
    volatile LONG TimerTickCount;
    volatile LONG ProcessCallCount;
    volatile LONG FrameDeliveredCount;
    volatile LONG NoStreamPointerCount;
    volatile LONG BufferTooSmallCount;
    volatile LONG PhysicalLocationSetStatus;
    volatile LONG PhysicalLocationBytes;
    volatile LONG PhysicalLocationPanel;
    volatile LONG InterfaceEnumerateStatus;
    volatile LONG InterfacePhysicalLocationStatus;
    volatile LONG InterfacePanelIdStatus;
    volatile LONG InterfacePropertyCount;
    volatile LONG Removing;
    volatile LONG Cleaned;
} AV_DEVICE_CONTEXT, *PAV_DEVICE_CONTEXT;

typedef struct _AV_PIN_CONTEXT {
    PKSPIN Pin;
    PAV_DEVICE_CONTEXT Device;
    WDFTIMER Timer;
    volatile LONG Running;
    volatile LONG FrameReady;
    ULONGLONG FrameNumber;
} AV_PIN_CONTEXT, *PAV_PIN_CONTEXT;

typedef struct _AV_TIMER_CONTEXT {
    PAV_PIN_CONTEXT PinContext;
} AV_TIMER_CONTEXT, *PAV_TIMER_CONTEXT;

typedef struct _AV_WORK_CONTEXT {
    PVOID DeviceContext;
} AV_WORK_CONTEXT, *PAV_WORK_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(AV_TIMER_CONTEXT, AvGetTimerContext);
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(AV_WORK_CONTEXT, AvGetWorkContext);

static WDFDRIVER g_AvWdfDriver;
static PDRIVER_UNLOAD g_AvKsUnload;

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_TIMER AvTimerCallback;
EVT_WDF_WORKITEM AvRearCaptureWorkItem;

static NTSTATUS AvDeviceAdd(PKSDEVICE Device);
static NTSTATUS AvDeviceStart(PKSDEVICE Device, PIRP Irp,
                              PCM_RESOURCE_LIST Translated,
                              PCM_RESOURCE_LIST Untranslated);
static VOID AvDeviceRemove(PKSDEVICE Device, PIRP Irp);
static VOID AvDeviceSurpriseRemoval(PKSDEVICE Device, PIRP Irp);
static NTSTATUS AvPinCreate(PKSPIN Pin, PIRP Irp);
static NTSTATUS AvPinClose(PKSPIN Pin, PIRP Irp);
static NTSTATUS AvPinProcess(PKSPIN Pin);
static NTSTATUS AvPinSetDataFormat(PKSPIN Pin, PKSDATAFORMAT OldFormat,
                                   PKSMULTIPLE_ITEM OldAttributes,
                                   const KSDATARANGE *DataRange,
                                   const KSATTRIBUTE_LIST *Attributes);
static NTSTATUS AvPinSetState(PKSPIN Pin, KSSTATE To, KSSTATE From);
static NTSTATUS AvIntersectHandler(PKSFILTER Filter, PIRP Irp,
                                   PKSP_PIN PinInstance,
                                   PKSDATARANGE CallerDataRange,
                                   PKSDATARANGE DescriptorDataRange,
                                   ULONG BufferSize, PVOID Data,
                                   PULONG DataSize);
static NTSTATUS AvRunRearCapture(PAV_DEVICE_CONTEXT Device);
static NTSTATUS AvPublishRearInterfaceProperties(
    PAV_DEVICE_CONTEXT Device);

static const DEVPROPKEY AvRearDevpkeyDevicePanelId = {
    {
        0x8dbc9c86, 0x97a9, 0x4bff,
        { 0x9b, 0xc6, 0xbf, 0xe9, 0x5d, 0x3e, 0x6d, 0xad }
    },
    2
};

static VOID
AvWriteRearValue(_In_opt_ HANDLE Key,
                 _In_ PCWSTR Name,
                 _In_ ULONG Value)
{
    UNICODE_STRING valueName;

    if (Key == NULL) {
        return;
    }
    RtlInitUnicodeString(&valueName, Name);
    (VOID)ZwSetValueKey(
        Key, &valueName, 0, REG_DWORD, &Value, sizeof(Value));
}

static VOID
AvWriteRearDiagnostics(_In_ PAV_DEVICE_CONTEXT Device)
{
#define AV_WRITE_REAR(Name, Field) \
    AvWriteRearValue( \
        Device->DiagnosticKey, L"NabuAvRear_" Name, \
        (ULONG)InterlockedCompareExchange(&Device->Field, 0, 0))

    AvWriteRearValue(Device->DiagnosticKey, L"NabuAvRear_Build", 13);
    AV_WRITE_REAR(L"CaptureAttempted", CaptureAttempted);
    AV_WRITE_REAR(L"CaptureQueued", CaptureQueued);
    AV_WRITE_REAR(L"CaptureStatus", CaptureStatus);
    AV_WRITE_REAR(L"CaptureRetryCount", CaptureRetryCount);
    AV_WRITE_REAR(L"CaptureSuccessAttempt", CaptureSuccessAttempt);
    AV_WRITE_REAR(L"CaptureActivationStatus", CaptureActivationStatus);
    AV_WRITE_REAR(L"PublishedReal", PublishedReal);
    AV_WRITE_REAR(L"RawMinimum", RawMinimum);
    AV_WRITE_REAR(L"RawMaximum", RawMaximum);
    AV_WRITE_REAR(L"RawMean", RawMean);
    AV_WRITE_REAR(L"RawNonZero", RawNonZero);
    AV_WRITE_REAR(L"ConversionCompleted", ConversionCompleted);
    AV_WRITE_REAR(L"ActivePinCount", ActivePinCount);
    AV_WRITE_REAR(L"PinCreateCount", PinCreateCount);
    AV_WRITE_REAR(L"PinCloseCount", PinCloseCount);
    AV_WRITE_REAR(L"RunCount", RunCount);
    AV_WRITE_REAR(L"NonRunCount", NonRunCount);
    AV_WRITE_REAR(L"TimerTickCount", TimerTickCount);
    AV_WRITE_REAR(L"ProcessCallCount", ProcessCallCount);
    AV_WRITE_REAR(L"FrameDeliveredCount", FrameDeliveredCount);
    AV_WRITE_REAR(L"NoStreamPointerCount", NoStreamPointerCount);
    AV_WRITE_REAR(L"BufferTooSmallCount", BufferTooSmallCount);
    AV_WRITE_REAR(L"PhysicalLocationSetStatus", PhysicalLocationSetStatus);
    AV_WRITE_REAR(L"PhysicalLocationBytes", PhysicalLocationBytes);
    AV_WRITE_REAR(L"PhysicalLocationPanel", PhysicalLocationPanel);
    AV_WRITE_REAR(L"InterfaceEnumerateStatus", InterfaceEnumerateStatus);
    AV_WRITE_REAR(
        L"InterfacePhysicalLocationStatus",
        InterfacePhysicalLocationStatus);
    AV_WRITE_REAR(L"InterfacePanelIdStatus", InterfacePanelIdStatus);
    AV_WRITE_REAR(L"InterfacePropertyCount", InterfacePropertyCount);

    if (Device->Hardware != NULL) {
        PNABU_OWN_STAGE23_CONTEXT hardware = Device->Hardware;
#define AV_WRITE_HW(Name, Field) \
        AvWriteRearValue( \
            Device->DiagnosticKey, L"NabuAvRear_" Name, \
            hardware->Field)

        AV_WRITE_HW(L"DmaBufferCreateStatus", DmaBufferCreateStatus);
        AV_WRITE_HW(L"DmaCommonBufferCreateStatus", DmaCommonBufferCreateStatus);
        AV_WRITE_HW(L"DmaPoolFallbackUsed", DmaPoolFallbackUsed);
        AV_WRITE_HW(L"DmaBufferLength", DmaBufferLength);
        AV_WRITE_HW(L"VcmPhase", VcmPhase);
        AV_WRITE_HW(L"VcmCompleted", VcmCompleted);
        AV_WRITE_HW(L"SmmuEnumerateStatus", SmmuEnumerateStatus);
        AV_WRITE_HW(L"SmmuOpenStatus", SmmuOpenStatus);
        AV_WRITE_HW(L"SmmuQueryStatus", SmmuQueryStatus);
        AV_WRITE_HW(L"SmmuGetConfigStatus", SmmuGetConfigStatus);
        AV_WRITE_HW(L"SmmuContextCountRaw", SmmuContextCountRaw);
        AV_WRITE_HW(L"SmmuContextCountAccepted", SmmuContextCountAccepted);
        AV_WRITE_HW(L"VcmCreateStatus", VcmCreateStatus);
        AV_WRITE_HW(L"VcmAssociationCount", VcmAssociationCount);
        AV_WRITE_HW(L"VcmAssociationFailIndex", VcmAssociationFailIndex);
        AV_WRITE_HW(L"VcmSetStateStatus", VcmSetStateStatus);
        AV_WRITE_HW(L"VcmActivationCount", VcmActivationCount);
        AV_WRITE_HW(L"VcmActivationFailIndex", VcmActivationFailIndex);
        AV_WRITE_HW(L"VcmDeactivateStatus", VcmDeactivateStatus);
        AV_WRITE_HW(L"VcmReserveStatus", VcmReserveStatus);
        AV_WRITE_HW(L"VcmBackStatus", VcmBackStatus);
        AV_WRITE_HW(L"VcmSetStateOffStatus", VcmSetStateOffStatus);
        AV_WRITE_HW(L"VcmDeassocStatus", VcmDeassocStatus);
        AV_WRITE_HW(L"VcmFreeStatus", VcmFreeStatus);
        AV_WRITE_HW(L"VcmDeviceAddressLow", VcmDeviceAddressLow);
        AV_WRITE_HW(L"VcmDeviceAddressHigh", VcmDeviceAddressHigh);
        AV_WRITE_HW(L"RealPhase", RealPhase);
        AV_WRITE_HW(L"RealAttempted", RealAttempted);
        AV_WRITE_HW(L"RealCompleted", RealCompleted);
        AV_WRITE_HW(L"RealPlatformOpenStatus", RealPlatformOpenStatus);
        AV_WRITE_HW(L"RealAcquire1Status", RealAcquire1Status);
        AV_WRITE_HW(L"RealPlatformPowerStatus", RealPlatformPowerStatus);
        AV_WRITE_HW(L"RealPlatformPinStatus", RealPlatformPinStatus);
        AV_WRITE_HW(L"RealInitMasterStatus", RealInitMasterStatus);
        AV_WRITE_HW(L"RealAcquire2Status", RealAcquire2Status);
        AV_WRITE_HW(L"RealChipIdStatus", RealChipIdStatus);
        AV_WRITE_HW(L"RealChipId", RealChipId);
        AV_WRITE_HW(L"RealInitStatus", RealInitStatus);
        AV_WRITE_HW(L"RealModeStatus", RealModeStatus);
        AV_WRITE_HW(L"RealStreamWriteStatus", RealStreamWriteStatus);
        AV_WRITE_HW(L"RealStreamReadStatus", RealStreamReadStatus);
        AV_WRITE_HW(L"RealStreamReadback", RealStreamReadback);
        AV_WRITE_HW(L"RealCsidTotalPackets", RealCsidTotalPackets);
        AV_WRITE_HW(L"RealWaitPolls", RealWaitPolls);
        AV_WRITE_HW(L"RealWaitExitReason", RealWaitExitReason);
        AV_WRITE_HW(L"RealWaitPacketsFirst", RealWaitPacketsFirst);
        AV_WRITE_HW(L"RealWaitPacketsLast", RealWaitPacketsLast);
        AV_WRITE_HW(L"RealWmImageBefore", RealWmImageBefore);
        AV_WRITE_HW(L"RealWmImageAfter", RealWmImageAfter);
        AV_WRITE_HW(L"RealBufferChanged", RealBufferChanged);
        AV_WRITE_HW(L"RealBufferTailChanged", RealBufferTailChanged);

#undef AV_WRITE_HW
    }

#undef AV_WRITE_REAR
}

static NTSTATUS
AvPublishRearPhysicalLocation(
    _In_ PDEVICE_OBJECT PhysicalDeviceObject,
    _Out_ PULONG Bytes)
{
    ACPI_PLD_V2_BUFFER pld;

    if (PhysicalDeviceObject == NULL || Bytes == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(&pld, sizeof(pld));
    pld.Revision = 2;
    pld.IgnoreColor = 1;
    pld.UserVisible = 1;
    pld.Panel = AcpiPldPanelBack;
    pld.VerticalPosition = AcpiPldVPosCenter;
    pld.HorizontalPosition = AcpiPldHPosCenter;
    pld.Shape = AcpiPldShapeUnknown;
    pld.Rotation = AcpiPldRotation0;
    pld.VerticalOffset = ACPI_PLD_OFFSET_NONE;
    pld.HorizontalOffset = ACPI_PLD_OFFSET_NONE;
    *Bytes = sizeof(pld);

    return IoSetDevicePropertyData(
        PhysicalDeviceObject,
        &DEVPKEY_Device_PhysicalDeviceLocation,
        LOCALE_NEUTRAL,
        PLUGPLAY_PROPERTY_PERSISTENT,
        DEVPROP_TYPE_BINARY,
        sizeof(pld),
        &pld);
}

static NTSTATUS
AvPublishRearInterfaceProperties(_Inout_ PAV_DEVICE_CONTEXT Device)
{
    static const WCHAR panelId[] =
        L"{00000000-0000-0000-ffff-ffffffffffff}\\0000\\6";
    ACPI_PLD_V2_BUFFER pld;
    PWSTR interfaces = NULL;
    PWSTR current;
    UNICODE_STRING symbolicLink;
    NTSTATUS status;
    NTSTATUS locationStatus = STATUS_NOT_FOUND;
    NTSTATUS panelStatus = STATUS_NOT_FOUND;
    ULONG count = 0;

    if (Device == NULL || Device->KsDevice == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    status = IoGetDeviceInterfaces(
        (LPGUID)&KSCATEGORY_VIDEO_CAMERA,
        Device->KsDevice->PhysicalDeviceObject,
        DEVICE_INTERFACE_INCLUDE_NONACTIVE,
        &interfaces);
    InterlockedExchange(
        &Device->InterfaceEnumerateStatus, (LONG)status);
    if (!NT_SUCCESS(status) || interfaces == NULL) {
        return status;
    }

    RtlZeroMemory(&pld, sizeof(pld));
    pld.Revision = 2;
    pld.IgnoreColor = 1;
    pld.UserVisible = 1;
    pld.Panel = AcpiPldPanelBack;
    pld.VerticalPosition = AcpiPldVPosCenter;
    pld.HorizontalPosition = AcpiPldHPosCenter;
    pld.Shape = AcpiPldShapeUnknown;
    pld.Rotation = AcpiPldRotation0;
    pld.VerticalOffset = ACPI_PLD_OFFSET_NONE;
    pld.HorizontalOffset = ACPI_PLD_OFFSET_NONE;

    for (current = interfaces; *current != L'\0';
         current += wcslen(current) + 1) {
        RtlInitUnicodeString(&symbolicLink, current);
        locationStatus = IoSetDeviceInterfacePropertyData(
            &symbolicLink,
            &DEVPKEY_Device_PhysicalDeviceLocation,
            LOCALE_NEUTRAL,
            PLUGPLAY_PROPERTY_PERSISTENT,
            DEVPROP_TYPE_BINARY,
            sizeof(pld),
            &pld);
        panelStatus = IoSetDeviceInterfacePropertyData(
            &symbolicLink,
            &AvRearDevpkeyDevicePanelId,
            LOCALE_NEUTRAL,
            PLUGPLAY_PROPERTY_PERSISTENT,
            DEVPROP_TYPE_STRING,
            sizeof(panelId),
            (PVOID)panelId);
        count += 1;
    }
    ExFreePool(interfaces);
    InterlockedExchange(
        &Device->InterfacePhysicalLocationStatus,
        (LONG)locationStatus);
    InterlockedExchange(
        &Device->InterfacePanelIdStatus,
        (LONG)panelStatus);
    InterlockedExchange(
        &Device->InterfacePropertyCount, (LONG)count);
    if (count == 0) {
        return STATUS_NOT_FOUND;
    }
    return !NT_SUCCESS(locationStatus) ?
        locationStatus : panelStatus;
}

static VOID
AvFillSafePattern(_Out_writes_(Width * Height) PULONG Rgb,
                  _In_ ULONG Width,
                  _In_ ULONG Height)
{
    static const ULONG bars[] = {
        0x00101010, 0x000000FF, 0x00FF0000, 0x00FF00FF,
        0x0000FF00, 0x0000FFFF, 0x00FFFF00, 0x00FFFFFF
    };
    ULONG x;
    ULONG y;

    for (y = 0; y < Height; ++y) {
        PULONG row = Rgb + ((Height - 1UL - y) * Width);
        for (x = 0; x < Width; ++x) {
            ULONG index = (x * RTL_NUMBER_OF(bars)) / Width;
            ULONG color = bars[index];

            if ((x > (Width / 2UL - 5UL) &&
                 x < (Width / 2UL + 5UL)) ||
                (y > (Height / 2UL - 5UL) &&
                 y < (Height / 2UL + 5UL))) {
                color = 0x0000FF00; // green cross: rear AV3
            }
            row[x] = color;
        }
    }
}

static ULONG
AvClampCoordinate(_In_ LONG Value, _In_ ULONG Limit)
{
    if (Value < 0) {
        return 0;
    }
    if ((ULONG)Value >= Limit) {
        return Limit - 1;
    }
    return (ULONG)Value;
}

static USHORT
AvRawAt(_In_reads_(Width * Height) const USHORT *Raw,
        _In_ ULONG Width, _In_ ULONG Height, _In_ LONG X, _In_ LONG Y)
{
    ULONG x = AvClampCoordinate(X, Width);
    ULONG y = AvClampCoordinate(Y, Height);
    return Raw[y * Width + x];
}

static USHORT
AvAverage2(_In_ USHORT A, _In_ USHORT B)
{
    return (USHORT)(((ULONG)A + (ULONG)B + 1UL) / 2UL);
}

static USHORT
AvAverage4(_In_ USHORT A, _In_ USHORT B, _In_ USHORT C, _In_ USHORT D)
{
    return (USHORT)(((ULONG)A + (ULONG)B + (ULONG)C + (ULONG)D + 2UL) /
                    4UL);
}

static UCHAR
AvScale10To8(_In_ USHORT Value, _In_ ULONG Gain)
{
    ULONGLONG scaled;

    if ((ULONG)Value <= AV_BLACK_LEVEL) {
        return 0;
    }
    scaled = ((ULONGLONG)((ULONG)Value - AV_BLACK_LEVEL) * Gain * 255ULL) /
             ((1023ULL - AV_BLACK_LEVEL) * AV_GAIN_ONE);
    return (UCHAR)(scaled > 255ULL ? 255ULL : scaled);
}

static UCHAR
AvScaleRearRaw(
    _In_ USHORT Value,
    _In_ ULONG Black,
    _In_ ULONGLONG ScaleQ16)
{
    ULONGLONG scaled;

    if ((ULONG)Value <= Black) {
        return 0;
    }
    scaled =
        ((ULONGLONG)((ULONG)Value - Black) * ScaleQ16 +
         0x8000ULL) >> 16;
    return (UCHAR)(scaled > 255ULL ? 255ULL : scaled);
}

//
// OV13B10 is GRBG. Downscale directly from one 2x2 Bayer cell per output
// pixel, keeping conversion bounded to the 640x480 preview buffer.
//
static VOID
AvBayerGrbgToRgb32Scaled(
    _Inout_ PAV_DEVICE_CONTEXT Device,
    _In_reads_(RawWidth * RawHeight) const USHORT *Raw,
    _In_ ULONG RawWidth,
    _In_ ULONG RawHeight)
{
    ULONGLONG sumR = 0, sumG = 0, sumB = 0;
    ULONGLONG countR = 0, countG = 0, countB = 0;
    ULONG rawMinimum = MAXUSHORT;
    ULONG rawMaximum = 0;
    ULONG rawNonZero = 0;
    ULONG gainR = AV_GAIN_ONE;
    ULONG gainB = AV_GAIN_ONE;
    ULONG black;
    ULONG white;
    ULONGLONG scaleR;
    ULONGLONG scaleG;
    ULONGLONG scaleB;
    ULONG x;
    ULONG y;

    for (y = 0; y + 1UL < RawHeight; y += 8UL) {
        for (x = 0; x + 1UL < RawWidth; x += 8UL) {
            ULONG g0 = Raw[y * RawWidth + x];
            ULONG r = Raw[y * RawWidth + x + 1UL];
            ULONG b = Raw[(y + 1UL) * RawWidth + x];
            ULONG g1 = Raw[(y + 1UL) * RawWidth + x + 1UL];

            if (g0 < rawMinimum) rawMinimum = g0;
            if (r < rawMinimum) rawMinimum = r;
            if (b < rawMinimum) rawMinimum = b;
            if (g1 < rawMinimum) rawMinimum = g1;
            if (g0 > rawMaximum) rawMaximum = g0;
            if (r > rawMaximum) rawMaximum = r;
            if (b > rawMaximum) rawMaximum = b;
            if (g1 > rawMaximum) rawMaximum = g1;
            if (g0 != 0) rawNonZero += 1;
            if (r != 0) rawNonZero += 1;
            if (b != 0) rawNonZero += 1;
            if (g1 != 0) rawNonZero += 1;
            sumG += g0 + g1;
            sumR += r;
            sumB += b;
            countG += 2;
            countR += 1;
            countB += 1;
        }
    }
    InterlockedExchange(&Device->RawMinimum, (LONG)rawMinimum);
    InterlockedExchange(&Device->RawMaximum, (LONG)rawMaximum);
    InterlockedExchange(
        &Device->RawMean,
        (LONG)((sumR + sumG + sumB) /
            (countR + countG + countB)));
    InterlockedExchange(&Device->RawNonZero, (LONG)rawNonZero);

    black = rawMinimum > 4UL ? rawMinimum - 4UL : 0UL;
    white = rawMaximum < MAXUSHORT - 4UL ?
        rawMaximum + 4UL : MAXUSHORT;
    if (white <= black + 8UL) {
        black = 0;
        white = 1023;
    }
    if (countR != 0 && countG != 0 &&
        (sumG / countG) > black &&
        (sumR / countR) > black) {
        gainR = (ULONG)((((sumG / countG) - black) * AV_GAIN_ONE) /
                        ((sumR / countR) - black));
    }
    if (countB != 0 && countG != 0 &&
        (sumG / countG) > black &&
        (sumB / countB) > black) {
        gainB = (ULONG)((((sumG / countG) - black) * AV_GAIN_ONE) /
                        ((sumB / countB) - black));
    }
    if (gainR < 512UL) gainR = 512UL;
    if (gainR > 4096UL) gainR = 4096UL;
    if (gainB < 512UL) gainB = 512UL;
    if (gainB > 4096UL) gainB = 4096UL;

    scaleG = (255ULL << 16) / (ULONGLONG)(white - black);
    scaleR = (scaleG * gainR) / AV_GAIN_ONE;
    scaleB = (scaleG * gainB) / AV_GAIN_ONE;
    for (y = 0; y < AV_OUTPUT_HEIGHT; ++y) {
        ULONG sourceY =
            (ULONG)(((ULONGLONG)y * (RawHeight - 2UL)) /
                    AV_OUTPUT_HEIGHT) & ~1UL;
        const USHORT *row0 = Raw + sourceY * RawWidth;
        const USHORT *row1 = row0 + RawWidth;
        PULONG destination =
            Device->CaptureBuffer +
            ((AV_OUTPUT_HEIGHT - 1UL - y) * AV_OUTPUT_WIDTH);

        for (x = 0; x < AV_OUTPUT_WIDTH; ++x) {
            ULONG sourceX =
                (ULONG)(((ULONGLONG)x * (RawWidth - 2UL)) /
                        AV_OUTPUT_WIDTH) & ~1UL;
            USHORT g = AvAverage2(
                row0[sourceX], row1[sourceX + 1UL]);
            UCHAR red = AvScaleRearRaw(
                row0[sourceX + 1UL], black, scaleR);
            UCHAR green = AvScaleRearRaw(g, black, scaleG);
            UCHAR blue = AvScaleRearRaw(
                row1[sourceX], black, scaleB);

            destination[x] =
                0xFF000000UL |
                ((ULONG)red << 16) |
                ((ULONG)green << 8) |
                (ULONG)blue;
        }
    }
    InterlockedExchange(&Device->ConversionCompleted, 1);
}

//
// OV13B10 reports GRBG.  Convert once, at PASSIVE_LEVEL, after capture.
// RGB32 is a bottom-up DIB, hence the destination row reversal.
//
static VOID
AvBayerGrbgToRgb32(_In_reads_(Width * Height) const USHORT *Raw,
                   _Out_writes_(Width * Height) PULONG Rgb,
                   _In_ ULONG Width, _In_ ULONG Height)
{
    ULONGLONG sumR = 0, sumG = 0, sumB = 0;
    ULONGLONG countR = 0, countG = 0, countB = 0;
    ULONG gainR = AV_GAIN_ONE, gainB = AV_GAIN_ONE;
    ULONG x, y;

    for (y = 0; y < Height; ++y) {
        for (x = 0; x < Width; ++x) {
            ULONG value = Raw[y * Width + x];
            value = value > AV_BLACK_LEVEL ? value - AV_BLACK_LEVEL : 0;
            if (((y & 1UL) == 0) && ((x & 1UL) != 0)) {
                sumR += value;
                countR += 1;
            } else if (((y & 1UL) != 0) && ((x & 1UL) == 0)) {
                sumB += value;
                countB += 1;
            } else {
                sumG += value;
                countG += 1;
            }
        }
    }
    if (countR != 0 && countG != 0 && sumR != 0) {
        gainR = (ULONG)(((sumG / countG) * AV_GAIN_ONE) /
                        (sumR / countR));
    }
    if (countB != 0 && countG != 0 && sumB != 0) {
        gainB = (ULONG)(((sumG / countG) * AV_GAIN_ONE) /
                        (sumB / countB));
    }
    if (gainR < 512UL) gainR = 512UL;
    if (gainR > 4096UL) gainR = 4096UL;
    if (gainB < 512UL) gainB = 512UL;
    if (gainB > 4096UL) gainB = 4096UL;

    for (y = 0; y < Height; ++y) {
        PULONG destination = Rgb + ((Height - 1UL - y) * Width);
        for (x = 0; x < Width; ++x) {
            USHORT r, g, b;
            USHORT center = AvRawAt(Raw, Width, Height, (LONG)x, (LONG)y);
            BOOLEAN evenY = ((y & 1UL) == 0);
            BOOLEAN evenX = ((x & 1UL) == 0);

            if (evenY && !evenX) {
                r = center;
                g = AvAverage4(
                    AvRawAt(Raw, Width, Height, (LONG)x - 1, (LONG)y),
                    AvRawAt(Raw, Width, Height, (LONG)x + 1, (LONG)y),
                    AvRawAt(Raw, Width, Height, (LONG)x, (LONG)y - 1),
                    AvRawAt(Raw, Width, Height, (LONG)x, (LONG)y + 1));
                b = AvAverage4(
                    AvRawAt(Raw, Width, Height, (LONG)x - 1, (LONG)y - 1),
                    AvRawAt(Raw, Width, Height, (LONG)x + 1, (LONG)y - 1),
                    AvRawAt(Raw, Width, Height, (LONG)x - 1, (LONG)y + 1),
                    AvRawAt(Raw, Width, Height, (LONG)x + 1, (LONG)y + 1));
            } else if (!evenY && evenX) {
                b = center;
                g = AvAverage4(
                    AvRawAt(Raw, Width, Height, (LONG)x - 1, (LONG)y),
                    AvRawAt(Raw, Width, Height, (LONG)x + 1, (LONG)y),
                    AvRawAt(Raw, Width, Height, (LONG)x, (LONG)y - 1),
                    AvRawAt(Raw, Width, Height, (LONG)x, (LONG)y + 1));
                r = AvAverage4(
                    AvRawAt(Raw, Width, Height, (LONG)x - 1, (LONG)y - 1),
                    AvRawAt(Raw, Width, Height, (LONG)x + 1, (LONG)y - 1),
                    AvRawAt(Raw, Width, Height, (LONG)x - 1, (LONG)y + 1),
                    AvRawAt(Raw, Width, Height, (LONG)x + 1, (LONG)y + 1));
            } else if (evenY) {
                g = center;
                r = AvAverage2(
                    AvRawAt(Raw, Width, Height, (LONG)x - 1, (LONG)y),
                    AvRawAt(Raw, Width, Height, (LONG)x + 1, (LONG)y));
                b = AvAverage2(
                    AvRawAt(Raw, Width, Height, (LONG)x, (LONG)y - 1),
                    AvRawAt(Raw, Width, Height, (LONG)x, (LONG)y + 1));
            } else {
                g = center;
                r = AvAverage2(
                    AvRawAt(Raw, Width, Height, (LONG)x, (LONG)y - 1),
                    AvRawAt(Raw, Width, Height, (LONG)x, (LONG)y + 1));
                b = AvAverage2(
                    AvRawAt(Raw, Width, Height, (LONG)x - 1, (LONG)y),
                    AvRawAt(Raw, Width, Height, (LONG)x + 1, (LONG)y));
            }
            destination[x] =
                ((ULONG)AvScale10To8(r, gainR) << 16) |
                ((ULONG)AvScale10To8(g, AV_GAIN_ONE) << 8) |
                (ULONG)AvScale10To8(b, gainB);
        }
    }
}

static const GUID AvFilterReferenceGuid =
    { 0x917c6d16, 0xec9a, 0x4bb1,
      { 0xa1, 0xe7, 0xec, 0x37, 0x3b, 0x77, 0x01, 0x39 } };
static const GUID AvPinCategoryCapture =
    { 0xfb6c4281, 0x0353, 0x11d1,
      { 0x90, 0x5f, 0x00, 0x00, 0xc0, 0xcc, 0x16, 0xba } };
static const GUID AvPinNameCapture = { STATIC_PINNAME_VIDEO_CAPTURE };
static const GUID AvSubtypeRgb32 = { AV_STATIC_RGB32 };
static const GUID AvFilterCategories[] = {
    { STATIC_KSCATEGORY_VIDEO_CAMERA },
    { STATIC_KSCATEGORY_VIDEO },
    { STATIC_KSCATEGORY_CAPTURE }
};

static KS_DATARANGE_VIDEO AvVideoFormat = {
    { sizeof(KS_DATARANGE_VIDEO), 0, AV_RGB_FRAME_BYTES, 0,
      STATICGUIDOF(KSDATAFORMAT_TYPE_VIDEO),
      { AV_STATIC_RGB32 },
      STATICGUIDOF(KSDATAFORMAT_SPECIFIER_VIDEOINFO) },
    TRUE, FALSE, 0, 0,
    { STATICGUIDOF(KSDATAFORMAT_TYPE_VIDEO), KS_AnalogVideo_None,
      { AV_OUTPUT_WIDTH, AV_OUTPUT_HEIGHT },
      { AV_OUTPUT_WIDTH, AV_OUTPUT_HEIGHT },
      { AV_OUTPUT_WIDTH, AV_OUTPUT_HEIGHT },
      1, 1, 1, 1,
      { AV_OUTPUT_WIDTH, AV_OUTPUT_HEIGHT },
      { AV_OUTPUT_WIDTH, AV_OUTPUT_HEIGHT },
      1, 1, 0, 0, 0, 0,
      (LONGLONG)AV_FRAME_INTERVAL, (LONGLONG)AV_FRAME_INTERVAL,
      (LONGLONG)AV_RGB_FRAME_BYTES * AV_TARGET_FPS * 8,
      (LONGLONG)AV_RGB_FRAME_BYTES * AV_TARGET_FPS * 8 },
    { { 0, 0, 0, 0 },
      { 0, 0, 0, 0 },
      (DWORD)(AV_RGB_FRAME_BYTES * AV_TARGET_FPS * 8), 0,
      (REFERENCE_TIME)AV_FRAME_INTERVAL,
      { sizeof(KS_BITMAPINFOHEADER), (LONG)AV_OUTPUT_WIDTH,
        (LONG)AV_OUTPUT_HEIGHT, 1, 32, KS_BI_RGB,
        AV_RGB_FRAME_BYTES, 0, 0, 0, 0 } }
};

static PKSDATARANGE AvPinRanges[] = {
    (PKSDATARANGE)&AvVideoFormat
};

static NTSTATUS
AvIntersectHandler(_In_ PKSFILTER Filter,
                   _In_ PIRP Irp,
                   _In_ PKSP_PIN PinInstance,
                   _In_ PKSDATARANGE CallerDataRange,
                   _In_ PKSDATARANGE DescriptorDataRange,
                   _In_ ULONG BufferSize,
                   _Out_writes_bytes_opt_(BufferSize) PVOID Data,
                   _Out_ PULONG DataSize)
{
    PKS_DATARANGE_VIDEO caller;
    PKS_DATARANGE_VIDEO descriptor;
    PKS_DATAFORMAT_VIDEOINFOHEADER result;
    ULONG formatSize = sizeof(KSDATAFORMAT) + sizeof(KS_VIDEOINFOHEADER);

    UNREFERENCED_PARAMETER(Filter);
    UNREFERENCED_PARAMETER(Irp);
    UNREFERENCED_PARAMETER(PinInstance);

    if (CallerDataRange == NULL || DescriptorDataRange == NULL ||
        DataSize == NULL ||
        CallerDataRange->FormatSize < sizeof(KS_DATARANGE_VIDEO) ||
        DescriptorDataRange->FormatSize < sizeof(KS_DATARANGE_VIDEO) ||
        !IsEqualGUIDAligned(&CallerDataRange->Specifier,
                            &KSDATAFORMAT_SPECIFIER_VIDEOINFO)) {
        return STATUS_NO_MATCH;
    }

    caller = (PKS_DATARANGE_VIDEO)CallerDataRange;
    descriptor = (PKS_DATARANGE_VIDEO)DescriptorDataRange;
    if (caller->bFixedSizeSamples != descriptor->bFixedSizeSamples ||
        caller->bTemporalCompression != descriptor->bTemporalCompression) {
        return STATUS_NO_MATCH;
    }

    *DataSize = formatSize;
    if (BufferSize == 0) {
        return STATUS_BUFFER_OVERFLOW;
    }
    if (Data == NULL || BufferSize < formatSize) {
        return STATUS_BUFFER_TOO_SMALL;
    }

    result = (PKS_DATAFORMAT_VIDEOINFOHEADER)Data;
    RtlZeroMemory(result, formatSize);
    RtlCopyMemory(&result->DataFormat,
                  DescriptorDataRange,
                  sizeof(KSDATAFORMAT));
    result->DataFormat.FormatSize = formatSize;
    result->DataFormat.SampleSize = AV_RGB_FRAME_BYTES;
    RtlCopyMemory(&result->VideoInfoHeader,
                  &descriptor->VideoInfoHeader,
                  sizeof(KS_VIDEOINFOHEADER));
    return STATUS_SUCCESS;
}

DECLARE_SIMPLE_FRAMING_EX(
    AvPinAllocatorFraming,
    STATICGUIDOF(KSMEMORY_TYPE_KERNEL_NONPAGED),
    KSALLOCATOR_REQUIREMENTF_SYSTEM_MEMORY |
        KSALLOCATOR_REQUIREMENTF_PREFERENCES_ONLY,
    2, 0, AV_RGB_FRAME_BYTES, AV_RGB_FRAME_BYTES);

static const KSPIN_DISPATCH AvPinDispatch = {
    AvPinCreate,
    AvPinClose,
    AvPinProcess,
    NULL,
    AvPinSetDataFormat,
    AvPinSetState,
    NULL,
    NULL,
    NULL,
    NULL
};

static const KSPIN_DESCRIPTOR_EX AvPinDescriptors[] = {
    {
        &AvPinDispatch,
        NULL,
        {
            0, NULL,
            0, NULL,
            RTL_NUMBER_OF(AvPinRanges), AvPinRanges,
            KSPIN_DATAFLOW_OUT,
            KSPIN_COMMUNICATION_BOTH,
            &AvPinCategoryCapture,
            &AvPinNameCapture,
            0
        },
        KSPIN_FLAG_PROCESS_IN_RUN_STATE_ONLY |
            KSPIN_FLAG_DO_NOT_INITIATE_PROCESSING |
            KSPIN_FLAG_FIXED_FORMAT,
        1,
        1,
        &AvPinAllocatorFraming,
        AvIntersectHandler
    }
};

static const KSFILTER_DISPATCH AvFilterDispatch = {
    NULL, NULL, NULL, NULL
};

static const KSFILTER_DESCRIPTOR AvFilterDescriptor = {
    &AvFilterDispatch,
    NULL,
    KSFILTER_DESCRIPTOR_VERSION,
    KSFILTER_FLAG_PRIORITIZE_REFERENCEGUID,
    &AvFilterReferenceGuid,
    RTL_NUMBER_OF(AvPinDescriptors),
    sizeof(AvPinDescriptors[0]),
    AvPinDescriptors,
    RTL_NUMBER_OF(AvFilterCategories),
    AvFilterCategories,
    0,
    sizeof(KSNODE_DESCRIPTOR),
    NULL,
    0,
    NULL,
    NULL
};

static const KSFILTER_DESCRIPTOR *const AvFilterDescriptors[] = {
    &AvFilterDescriptor
};

static const KSDEVICE_DISPATCH AvDeviceDispatch = {
    AvDeviceAdd,
    AvDeviceStart,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    AvDeviceRemove,
    NULL,
    AvDeviceSurpriseRemoval,
    NULL,
    NULL,
    NULL
};

static const KSDEVICE_DESCRIPTOR AvDeviceDescriptor = {
    &AvDeviceDispatch,
    RTL_NUMBER_OF(AvFilterDescriptors),
    AvFilterDescriptors,
    KSDEVICE_DESCRIPTOR_VERSION
};

static NTSTATUS
AvDeviceAdd(_In_ PKSDEVICE Device)
{
    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_OBJECT_ATTRIBUTES workAttributes;
    WDF_DMA_ENABLER_CONFIG dmaConfig;
    WDF_WORKITEM_CONFIG workConfig;
    PAV_WORK_CONTEXT workContext;
    PAV_DEVICE_CONTEXT avContext = NULL;
    PNABU_OWN_STAGE23_CONTEXT hardware;
    WDFDEVICE wdfDevice = NULL;
    HANDLE diagnosticKey = NULL;
    NTSTATUS status;
    ULONG locationBytes = 0;

    avContext = (PAV_DEVICE_CONTEXT)ExAllocatePool2(
        POOL_FLAG_NON_PAGED, sizeof(*avContext), AV_POOL_TAG);
    if (avContext == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(avContext, sizeof(*avContext));
    avContext->KsDevice = Device;
    status = IoOpenDeviceRegistryKey(
        Device->PhysicalDeviceObject,
        PLUGPLAY_REGKEY_DEVICE,
        KEY_SET_VALUE,
        &diagnosticKey);
    if (NT_SUCCESS(status)) {
        avContext->DiagnosticKey = diagnosticKey;
    }
    status = AvPublishRearPhysicalLocation(
        Device->PhysicalDeviceObject, &locationBytes);
    InterlockedExchange(
        &avContext->PhysicalLocationSetStatus, (LONG)status);
    InterlockedExchange(
        &avContext->PhysicalLocationBytes, (LONG)locationBytes);
    InterlockedExchange(
        &avContext->PhysicalLocationPanel, AcpiPldPanelBack);
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(
        &attributes, NABU_OWN_STAGE23_CONTEXT);
    status = WdfDeviceMiniportCreate(
        g_AvWdfDriver,
        &attributes,
        Device->FunctionalDeviceObject,
        Device->NextDeviceObject,
        Device->PhysicalDeviceObject,
        &wdfDevice);
    if (!NT_SUCCESS(status)) {
        if (avContext->DiagnosticKey != NULL) {
            ZwClose(avContext->DiagnosticKey);
        }
        ExFreePoolWithTag(avContext, AV_POOL_TAG);
        return status;
    }

    hardware = NabuOwnGetContext(wdfDevice);
    RtlZeroMemory(hardware, sizeof(*hardware));
    hardware->Device = wdfDevice;
    hardware->ArmRequested = NABU_OWN_ARM_REAL_CAPTURE;
    hardware->VcmModeRequested = 5;

    WDF_DMA_ENABLER_CONFIG_INIT(
        &dmaConfig,
        WdfDmaProfileScatterGather64Duplex,
        NABU_OWN_DMA_BUFFER_BYTES);
    status = WdfDmaEnablerCreate(
        wdfDevice,
        &dmaConfig,
        WDF_NO_OBJECT_ATTRIBUTES,
        &hardware->DmaEnabler);
    hardware->DmaEnablerCreateStatus = (ULONG)status;
    if (!NT_SUCCESS(status)) {
        WdfObjectDelete(wdfDevice);
        if (avContext->DiagnosticKey != NULL) {
            ZwClose(avContext->DiagnosticKey);
        }
        ExFreePoolWithTag(avContext, AV_POOL_TAG);
        return status;
    }

    avContext->RgbBuffer = (PULONG)ExAllocatePool2(
        POOL_FLAG_NON_PAGED, AV_RGB_FRAME_BYTES, AV_POOL_TAG);
    if (avContext->RgbBuffer == NULL) {
        WdfObjectDelete(wdfDevice);
        if (avContext->DiagnosticKey != NULL) {
            ZwClose(avContext->DiagnosticKey);
        }
        ExFreePoolWithTag(avContext, AV_POOL_TAG);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    AvFillSafePattern(
        avContext->RgbBuffer, AV_OUTPUT_WIDTH, AV_OUTPUT_HEIGHT);
    avContext->CaptureBuffer = (PULONG)ExAllocatePool2(
        POOL_FLAG_NON_PAGED, AV_RGB_FRAME_BYTES, AV_POOL_TAG);
    if (avContext->CaptureBuffer == NULL) {
        ExFreePoolWithTag(avContext->RgbBuffer, AV_POOL_TAG);
        avContext->RgbBuffer = NULL;
        WdfObjectDelete(wdfDevice);
        if (avContext->DiagnosticKey != NULL) {
            ZwClose(avContext->DiagnosticKey);
        }
        ExFreePoolWithTag(avContext, AV_POOL_TAG);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(avContext->CaptureBuffer, AV_RGB_FRAME_BYTES);
    WDF_WORKITEM_CONFIG_INIT(&workConfig, AvRearCaptureWorkItem);
    workConfig.AutomaticSerialization = FALSE;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(
        &workAttributes, AV_WORK_CONTEXT);
    workAttributes.ParentObject = wdfDevice;
    status = WdfWorkItemCreate(
        &workConfig,
        &workAttributes,
        &avContext->CaptureWorkItem);
    if (!NT_SUCCESS(status)) {
        ExFreePoolWithTag(avContext->CaptureBuffer, AV_POOL_TAG);
        avContext->CaptureBuffer = NULL;
        ExFreePoolWithTag(avContext->RgbBuffer, AV_POOL_TAG);
        avContext->RgbBuffer = NULL;
        WdfObjectDelete(wdfDevice);
        if (avContext->DiagnosticKey != NULL) {
            ZwClose(avContext->DiagnosticKey);
        }
        ExFreePoolWithTag(avContext, AV_POOL_TAG);
        return status;
    }
    workContext = AvGetWorkContext(avContext->CaptureWorkItem);
    workContext->DeviceContext = avContext;

    avContext->WdfDevice = wdfDevice;
    avContext->Hardware = hardware;
    AvWriteRearDiagnostics(avContext);
    Device->Context = avContext;
    return STATUS_SUCCESS;
}

static NTSTATUS
AvDeviceStart(_In_ PKSDEVICE Device, _In_ PIRP Irp,
              _In_opt_ PCM_RESOURCE_LIST Translated,
              _In_opt_ PCM_RESOURCE_LIST Untranslated)
{
    PAV_DEVICE_CONTEXT context =
        Device != NULL ? (PAV_DEVICE_CONTEXT)Device->Context : NULL;

    UNREFERENCED_PARAMETER(Irp);
    UNREFERENCED_PARAMETER(Translated);
    UNREFERENCED_PARAMETER(Untranslated);
    if (context == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }
    (VOID)AvPublishRearInterfaceProperties(context);
    AvWriteRearDiagnostics(context);
    return STATUS_SUCCESS;
}

static VOID
AvCleanupDevice(_In_ PKSDEVICE Device)
{
    PAV_DEVICE_CONTEXT context =
        Device != NULL ? (PAV_DEVICE_CONTEXT)Device->Context : NULL;

    if (context == NULL ||
        InterlockedExchange(&context->Cleaned, 1) != 0) {
        return;
    }
    InterlockedExchange(&context->Removing, 1);
    if (context->CaptureWorkItem != NULL) {
        PAV_WORK_CONTEXT workContext =
            AvGetWorkContext(context->CaptureWorkItem);

        WdfWorkItemFlush(context->CaptureWorkItem);
        workContext->DeviceContext = NULL;
        WdfObjectDelete(context->CaptureWorkItem);
        context->CaptureWorkItem = NULL;
    }
    if (context->Hardware != NULL) {
        NabuOwnReleaseDmaBuffer(context->Hardware);
    }
    if (context->RgbBuffer != NULL) {
        ExFreePoolWithTag(context->RgbBuffer, AV_POOL_TAG);
        context->RgbBuffer = NULL;
    }
    if (context->CaptureBuffer != NULL) {
        ExFreePoolWithTag(context->CaptureBuffer, AV_POOL_TAG);
        context->CaptureBuffer = NULL;
    }
    if (context->WdfDevice != NULL) {
        WdfObjectDelete(context->WdfDevice);
        context->WdfDevice = NULL;
    }
    if (context->DiagnosticKey != NULL) {
        ZwClose(context->DiagnosticKey);
        context->DiagnosticKey = NULL;
    }
    Device->Context = NULL;
    ExFreePoolWithTag(context, AV_POOL_TAG);
}

static VOID
AvDeviceRemove(_In_ PKSDEVICE Device, _In_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(Irp);
    AvCleanupDevice(Device);
}

static VOID
AvDeviceSurpriseRemoval(_In_ PKSDEVICE Device, _In_ PIRP Irp)
{
    PAV_DEVICE_CONTEXT context =
        Device != NULL ? (PAV_DEVICE_CONTEXT)Device->Context : NULL;

    UNREFERENCED_PARAMETER(Irp);
    if (context != NULL) {
        InterlockedExchange(&context->Removing, 1);
    }
}

static NTSTATUS
AvRunRearCapture(_Inout_ PAV_DEVICE_CONTEXT Device)
{
    PNABU_OWN_STAGE23_CONTEXT hardware;
    const USHORT *raw;
    PULONG oldBuffer;
    NTSTATUS status;

    if (Device == NULL || Device->Hardware == NULL ||
        KeGetCurrentIrql() != PASSIVE_LEVEL) {
        return STATUS_INVALID_DEVICE_STATE;
    }

    hardware = Device->Hardware;
    InterlockedExchange(
        &Device->CaptureStatus, (LONG)STATUS_PENDING);
    InterlockedExchange(&Device->ConversionCompleted, 0);
    hardware->DmaBufferAlreadyPresent = 0;
    hardware->DmaBufferReleased = 0;
    hardware->VcmCompleted = 0;
    hardware->VcmActivationCount = 0;
    hardware->VcmActivationFailIndex = MAXULONG;
    hardware->RealAttempted = 0;
    hardware->RealCompleted = 0;
    hardware->RealBufferChanged = 0;
    hardware->RealBufferTailChanged = 0;
    hardware->RealChipId = 0;
    hardware->VcmModeRequested = 5;
    NabuOwnCreateDmaBuffer(hardware);
    raw = (const USHORT *)NabuOwnGetDmaVirtualAddress(hardware);
    if (raw == NULL) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto Finished;
    }

    NabuOwnRunVcmProbe(hardware);
    if (hardware->VcmCompleted == 0 ||
        hardware->RealCompleted == 0 ||
        hardware->RealChipId != 0x0D42UL ||
        hardware->RealBufferTailChanged == 0) {
        status = STATUS_IO_DEVICE_ERROR;
        goto Finished;
    }

    AvBayerGrbgToRgb32Scaled(
        Device, raw, NABU_REAR_WIDTH, NABU_REAR_HEIGHT);
    if (InterlockedCompareExchange(
            &Device->ConversionCompleted, 0, 0) == 0 ||
        InterlockedCompareExchange(
            &Device->RawNonZero, 0, 0) < 100 ||
        InterlockedCompareExchange(
            &Device->RawMaximum, 0, 0) <=
        InterlockedCompareExchange(
            &Device->RawMinimum, 0, 0)) {
        status = STATUS_DATA_ERROR;
        goto Finished;
    }

    oldBuffer = (PULONG)InterlockedExchangePointer(
        (PVOID volatile *)&Device->RgbBuffer,
        Device->CaptureBuffer);
    Device->CaptureBuffer = oldBuffer;
    InterlockedExchange(&Device->PublishedReal, 1);
    status = STATUS_SUCCESS;

Finished:
    NabuOwnReleaseDmaBuffer(hardware);
    InterlockedExchange(&Device->CaptureStatus, (LONG)status);
    AvWriteRearDiagnostics(Device);
    return status;
}

VOID
AvRearCaptureWorkItem(_In_ WDFWORKITEM WorkItem)
{
    PAV_WORK_CONTEXT workContext = AvGetWorkContext(WorkItem);
    PAV_DEVICE_CONTEXT device;
    PNABU_OWN_STAGE23_CONTEXT hardware;
    LARGE_INTEGER delay;
    NTSTATUS status = STATUS_CANCELLED;
    ULONG attempt;

    if (workContext == NULL || workContext->DeviceContext == NULL) {
        return;
    }
    device = (PAV_DEVICE_CONTEXT)workContext->DeviceContext;
    hardware = device->Hardware;
    delay.QuadPart =
        -((LONGLONG)AV_REAR_INITIAL_DELAY_MS * 10000LL);
    (VOID)KeDelayExecutionThread(
        KernelMode, FALSE, &delay);

    InterlockedExchange(&device->CaptureSuccessAttempt, 0);
    for (attempt = 1; attempt <= AV_REAR_MAX_ATTEMPTS; ++attempt) {
        if (InterlockedCompareExchange(
                &device->Removing, 0, 0) != 0 ||
            InterlockedCompareExchange(
                &device->ActivePinCount, 0, 0) == 0) {
            status = STATUS_CANCELLED;
            break;
        }
        InterlockedExchange(
            &device->CaptureRetryCount, (LONG)attempt);
        status = AvRunRearCapture(device);
        if (NT_SUCCESS(status)) {
            InterlockedExchange(
                &device->CaptureSuccessAttempt, (LONG)attempt);
            break;
        }

        if (hardware == NULL ||
            hardware->VcmPhase != 13 ||
            hardware->VcmActivationCount != 0 ||
            hardware->RealAttempted != 0) {
            break;
        }
        InterlockedExchange(
            &device->CaptureActivationStatus,
            (LONG)hardware->VcmDeactivateStatus);
        delay.QuadPart =
            -((LONGLONG)AV_REAR_RETRY_DELAY_MS * 10000LL);
        (VOID)KeDelayExecutionThread(
            KernelMode, FALSE, &delay);
    }

    InterlockedExchange(&device->CaptureStatus, (LONG)status);
    if (!NT_SUCCESS(status)) {
        InterlockedExchange(&device->CaptureAttempted, 0);
    }
    InterlockedExchange(&device->CaptureQueued, 0);
    AvWriteRearDiagnostics(device);
}

static NTSTATUS
AvPinCreate(_In_ PKSPIN Pin, _In_ PIRP Irp)
{
    PKSDEVICE ksDevice = KsPinGetDevice(Pin);
    PAV_DEVICE_CONTEXT deviceContext;
    PAV_PIN_CONTEXT pinContext;
    PAV_TIMER_CONTEXT timerContext;
    WDF_TIMER_CONFIG timerConfig;
    WDF_OBJECT_ATTRIBUTES attributes;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(Irp);
    if (ksDevice == NULL || ksDevice->Context == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }
    deviceContext = (PAV_DEVICE_CONTEXT)ksDevice->Context;
    if (InterlockedCompareExchange(&deviceContext->Removing, 0, 0) != 0) {
        return STATUS_DELETE_PENDING;
    }

    pinContext = (PAV_PIN_CONTEXT)ExAllocatePool2(
        POOL_FLAG_NON_PAGED, sizeof(*pinContext), AV_POOL_TAG);
    if (pinContext == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(pinContext, sizeof(*pinContext));
    pinContext->Pin = Pin;
    pinContext->Device = deviceContext;

    WDF_TIMER_CONFIG_INIT_PERIODIC(
        &timerConfig, AvTimerCallback, AV_TIMER_PERIOD_MS);
    timerConfig.AutomaticSerialization = FALSE;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(
        &attributes, AV_TIMER_CONTEXT);
    attributes.ParentObject = deviceContext->WdfDevice;
    status = WdfTimerCreate(
        &timerConfig, &attributes, &pinContext->Timer);
    if (!NT_SUCCESS(status)) {
        ExFreePoolWithTag(pinContext, AV_POOL_TAG);
        return status;
    }
    timerContext = AvGetTimerContext(pinContext->Timer);
    timerContext->PinContext = pinContext;
    Pin->StreamHeaderSize = sizeof(KSSTREAM_HEADER) + sizeof(KS_FRAME_INFO);
    Pin->Context = pinContext;
    InterlockedIncrement(&deviceContext->PinCreateCount);
    InterlockedIncrement(&deviceContext->ActivePinCount);
    return STATUS_SUCCESS;
}

static NTSTATUS
AvPinClose(_In_ PKSPIN Pin, _In_ PIRP Irp)
{
    PAV_PIN_CONTEXT pinContext = (PAV_PIN_CONTEXT)Pin->Context;

    UNREFERENCED_PARAMETER(Irp);
    if (pinContext == NULL) {
        return STATUS_SUCCESS;
    }
    InterlockedExchange(&pinContext->Running, 0);
    if (pinContext->Timer != NULL) {
        WdfTimerStop(pinContext->Timer, TRUE);
        WdfObjectDelete(pinContext->Timer);
        pinContext->Timer = NULL;
    }
    if (pinContext->Device != NULL) {
        InterlockedIncrement(&pinContext->Device->PinCloseCount);
        InterlockedDecrement(&pinContext->Device->ActivePinCount);
        AvWriteRearDiagnostics(pinContext->Device);
    }
    Pin->Context = NULL;
    ExFreePoolWithTag(pinContext, AV_POOL_TAG);
    return STATUS_SUCCESS;
}

static NTSTATUS
AvPinSetDataFormat(_In_ PKSPIN Pin,
                   _In_opt_ PKSDATAFORMAT OldFormat,
                   _In_opt_ PKSMULTIPLE_ITEM OldAttributes,
                   _In_ const KSDATARANGE *DataRange,
                   _In_opt_ const KSATTRIBUTE_LIST *Attributes)
{
    PKS_DATAFORMAT_VIDEOINFOHEADER connection;

    UNREFERENCED_PARAMETER(OldFormat);
    UNREFERENCED_PARAMETER(OldAttributes);
    UNREFERENCED_PARAMETER(Attributes);

    UNREFERENCED_PARAMETER(DataRange);
    if (Pin == NULL || Pin->ConnectionFormat == NULL ||
        Pin->ConnectionFormat->FormatSize <
            sizeof(KS_DATAFORMAT_VIDEOINFOHEADER) ||
        !IsEqualGUIDAligned(&Pin->ConnectionFormat->MajorFormat,
                            &KSDATAFORMAT_TYPE_VIDEO) ||
        !IsEqualGUIDAligned(&Pin->ConnectionFormat->SubFormat,
                            &AvSubtypeRgb32) ||
        !IsEqualGUIDAligned(&Pin->ConnectionFormat->Specifier,
                            &KSDATAFORMAT_SPECIFIER_VIDEOINFO)) {
        return STATUS_NO_MATCH;
    }
    connection =
        (PKS_DATAFORMAT_VIDEOINFOHEADER)Pin->ConnectionFormat;
    if (connection->VideoInfoHeader.bmiHeader.biSize !=
            sizeof(KS_BITMAPINFOHEADER) ||
        connection->VideoInfoHeader.bmiHeader.biWidth !=
            (LONG)AV_OUTPUT_WIDTH ||
        connection->VideoInfoHeader.bmiHeader.biHeight !=
            (LONG)AV_OUTPUT_HEIGHT ||
        connection->VideoInfoHeader.bmiHeader.biPlanes != 1 ||
        connection->VideoInfoHeader.bmiHeader.biBitCount != 32 ||
        connection->VideoInfoHeader.bmiHeader.biCompression != KS_BI_RGB ||
        connection->VideoInfoHeader.bmiHeader.biSizeImage <
            AV_RGB_FRAME_BYTES) {
        return STATUS_NO_MATCH;
    }
    return STATUS_SUCCESS;
}

static NTSTATUS
AvPinSetState(_In_ PKSPIN Pin, _In_ KSSTATE To, _In_ KSSTATE From)
{
    PAV_PIN_CONTEXT context = (PAV_PIN_CONTEXT)Pin->Context;

    if (context == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }
    UNREFERENCED_PARAMETER(From);
    if (To != KSSTATE_RUN) {
        if (context->Device != NULL) {
            InterlockedIncrement(&context->Device->NonRunCount);
        }
        InterlockedExchange(&context->Running, 0);
        if (context->Timer != NULL) {
            WdfTimerStop(context->Timer, FALSE);
        }
    } else {
        if (context->Device != NULL) {
            InterlockedIncrement(&context->Device->RunCount);
            if (InterlockedCompareExchange(
                    &context->Device->CaptureAttempted, 1, 0) == 0 &&
                context->Device->CaptureWorkItem != NULL &&
                InterlockedCompareExchange(
                    &context->Device->CaptureQueued, 1, 0) == 0) {
                WdfWorkItemEnqueue(
                    context->Device->CaptureWorkItem);
            }
        }
        InterlockedExchange(&context->FrameReady, 1);
        InterlockedExchange(&context->Running, 1);
        WdfTimerStart(context->Timer, WDF_REL_TIMEOUT_IN_MS(1));
    }
    return STATUS_SUCCESS;
}

VOID
AvTimerCallback(_In_ WDFTIMER Timer)
{
    PAV_TIMER_CONTEXT timerContext = AvGetTimerContext(Timer);
    PAV_PIN_CONTEXT pinContext = timerContext->PinContext;

    if (pinContext != NULL && pinContext->Device != NULL) {
        InterlockedIncrement(&pinContext->Device->TimerTickCount);
    }
    if (pinContext != NULL &&
        InterlockedCompareExchange(&pinContext->Running, 0, 0) != 0 &&
        InterlockedCompareExchange(&pinContext->FrameReady, 0, 0) != 0) {
        KsPinAttemptProcessing(pinContext->Pin, TRUE);
    }
}

static NTSTATUS
AvPinProcess(_In_ PKSPIN Pin)
{
    PAV_PIN_CONTEXT pinContext = (PAV_PIN_CONTEXT)Pin->Context;
    PAV_DEVICE_CONTEXT device;
    PULONG rgbBuffer;
    PKSSTREAM_POINTER streamPointer;
    LARGE_INTEGER now;

    if (pinContext == NULL ||
        InterlockedCompareExchange(&pinContext->Running, 0, 0) == 0 ||
        InterlockedCompareExchange(&pinContext->FrameReady, 0, 0) == 0) {
        return STATUS_PENDING;
    }
    device = pinContext->Device;
    if (device == NULL) {
        return STATUS_PENDING;
    }
    rgbBuffer = (PULONG)InterlockedCompareExchangePointer(
        (PVOID volatile *)&device->RgbBuffer, NULL, NULL);
    if (rgbBuffer == NULL) {
        return STATUS_PENDING;
    }
    InterlockedIncrement(&device->ProcessCallCount);

    streamPointer = KsPinGetLeadingEdgeStreamPointer(
        Pin, KSSTREAM_POINTER_STATE_LOCKED);
    if (streamPointer == NULL) {
        InterlockedIncrement(&device->NoStreamPointerCount);
        return STATUS_PENDING;
    }
    if (streamPointer->OffsetOut.Data == NULL ||
        streamPointer->OffsetOut.Remaining < AV_RGB_FRAME_BYTES) {
        InterlockedIncrement(&device->BufferTooSmallCount);
        KsStreamPointerUnlock(streamPointer, FALSE);
        return STATUS_BUFFER_TOO_SMALL;
    }

    RtlCopyMemory(
        streamPointer->OffsetOut.Data,
        rgbBuffer,
        AV_RGB_FRAME_BYTES);
    streamPointer->StreamHeader->DataUsed = AV_RGB_FRAME_BYTES;
    streamPointer->StreamHeader->Duration = AV_FRAME_INTERVAL;
    streamPointer->StreamHeader->OptionsFlags =
        KSSTREAM_HEADER_OPTIONSF_TIMEVALID |
        KSSTREAM_HEADER_OPTIONSF_DURATIONVALID |
        KSSTREAM_HEADER_OPTIONSF_SPLICEPOINT;
    KeQuerySystemTimePrecise(&now);
    streamPointer->StreamHeader->PresentationTime.Time = now.QuadPart;
    streamPointer->StreamHeader->PresentationTime.Numerator = 1;
    streamPointer->StreamHeader->PresentationTime.Denominator = 1;
    pinContext->FrameNumber += 1;
    if (streamPointer->StreamHeader->Size >=
        sizeof(KSSTREAM_HEADER) + sizeof(KS_FRAME_INFO)) {
        PKS_FRAME_INFO frameInfo =
            (PKS_FRAME_INFO)(streamPointer->StreamHeader + 1);
        RtlZeroMemory(frameInfo, sizeof(*frameInfo));
        frameInfo->ExtendedHeaderSize = sizeof(*frameInfo);
        frameInfo->dwFrameFlags = KS_VIDEO_FLAG_FRAME;
        frameInfo->PictureNumber = (LONGLONG)pinContext->FrameNumber;
    }

    KsStreamPointerUnlock(streamPointer, TRUE);
    InterlockedIncrement(&device->FrameDeliveredCount);
    return STATUS_PENDING;
}

static VOID
AvDriverUnload(_In_ PDRIVER_OBJECT DriverObject)
{
    PDRIVER_UNLOAD ksUnload = g_AvKsUnload;

    if (g_AvWdfDriver != NULL) {
        WdfDriverMiniportUnload(g_AvWdfDriver);
        g_AvWdfDriver = NULL;
    }
    if (ksUnload != NULL) {
        ksUnload(DriverObject);
    }
}

NTSTATUS
DriverEntry(_In_ PDRIVER_OBJECT DriverObject,
            _In_ PUNICODE_STRING RegistryPath)
{
    WDF_DRIVER_CONFIG config;
    NTSTATUS status;

    g_AvWdfDriver = NULL;
    g_AvKsUnload = NULL;
    WDF_DRIVER_CONFIG_INIT(&config, WDF_NO_EVENT_CALLBACK);
    config.DriverInitFlags |= WdfDriverInitNoDispatchOverride;
    status = WdfDriverCreate(
        DriverObject,
        RegistryPath,
        WDF_NO_OBJECT_ATTRIBUTES,
        &config,
        &g_AvWdfDriver);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    status = KsInitializeDriver(
        DriverObject, RegistryPath, &AvDeviceDescriptor);
    if (!NT_SUCCESS(status)) {
        WdfDriverMiniportUnload(g_AvWdfDriver);
        g_AvWdfDriver = NULL;
        return status;
    }
    g_AvKsUnload = DriverObject->DriverUnload;
    DriverObject->DriverUnload = AvDriverUnload;
    return STATUS_SUCCESS;
}
