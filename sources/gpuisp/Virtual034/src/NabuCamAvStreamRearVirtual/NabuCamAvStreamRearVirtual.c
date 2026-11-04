//
// NabuCamAvStreamRearVirtual.c
//
// Root-enumerated AVStream preview for the physical rear-camera backend.
// KSSTATE_RUN requests serialized Stage39 optical frames through a private
// IOCTL, converts each accepted frame to RGB32, and atomically publishes it.
// AVStream itself owns no Qualcomm camera hardware or DMA objects.
//

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
#include "BackendBridgeRear.h"
#include "..\NabuCamHwRear\NabuCamRearIoctl.h"
#include "AndroidFactoryTuningRear.h"

#include "..\GpuBridgeProtocol.h"
#pragma warning(disable:4100)
#pragma warning(disable:4201)

#define AV_POOL_TAG           'RVAN'
#define AV_OUTPUT_WIDTH       1280UL
#define AV_OUTPUT_HEIGHT      960UL
#define AV_COLOR_WIDTH        640UL
#define AV_COLOR_HEIGHT       480UL
#define AV_RGB_STRIDE         (AV_OUTPUT_WIDTH * 4UL)
#define AV_RGB_FRAME_BYTES    (AV_RGB_STRIDE * AV_OUTPUT_HEIGHT)
#define AV_RGB24_STRIDE       (((AV_OUTPUT_WIDTH * 3UL) + 3UL) & ~3UL)
#define AV_RGB24_FRAME_BYTES  (AV_RGB24_STRIDE * AV_OUTPUT_HEIGHT)
#define AV_COLOR_CELLS        (AV_COLOR_WIDTH * AV_COLOR_HEIGHT)
#define AV_COLOR_FRAME_BYTES  (AV_COLOR_CELLS * 4UL)
#define AV_TARGET_FPS         30UL
#define AV_TIMER_PERIOD_MS    8UL
#define AV_DELIVERY_TICKS     4L
#define AV_FRAME_INTERVAL     (10000000ULL / AV_TARGET_FPS)
#define AV_REAR_INITIAL_DELAY_MS 50UL
#define AV_REAR_REFRESH_GUARD_MS 1UL
#define AV_REAR_BACKEND_IDLE_HOLD_MS 500UL
#define AV_BLACK_LEVEL        64UL
#define AV_GAIN_ONE           1024UL
#define AV_RAW_LUT_SIZE       1024UL
#define AV_ANDROID_LTM_DARK_END 102UL
#define AV_ANDROID_LTM_BRIGHT_START 191UL
/* Android AEC/ADRC convergence speed is 0.15 per frame (154 in Q10). */
#define AV_ANDROID_LTM_STEP_Q10 154UL
#define AV_REAR_DENOISE_FULL_P95 64UL
#define AV_REAR_DENOISE_OFF_P95 128UL
#define AV_DENOISE_EDGE_THRESHOLD 18UL
#define AV_DENOISE_SPATIAL_EDGE_RAW8 20UL
#define AV_CONVERSION_THREAD_PRIORITY 12
/*
 * Budget that decides whether a frame still gets the optional native Bayer detail
 * reconstruction.  It is deliberately below the rear's steady-state conversion cost, which is
 * a measured decision, not an oversight: with the gate at 90 ms the pass ran and each frame
 * cost +16 ms (15.6 -> 12.9 fps delivered), while the captured frames gained 1.4% edge energy
 * against 3% more noise - nothing measurable, because the rear is limited by focus, not by
 * reconstruction.  The front keeps the pass (there the same measurement gives +9% edges for
 * +3% noise), so the gate stays a real spike guard on this path and simply declines detail.
 */
#define AV_CONVERSION_OVERLOAD_US 45000L
/*
 * Luma refinement runs on the 640x480 colour grid, before the 2x output pass.
 * A 5-tap local mean doubles as the noise smoother and as the unsharp blur, so
 * a flat area is pulled towards its own mean and the high-pass there is zero:
 * sharpening can only act where the denoiser already left a real edge.
 * AV_LUMA_EDGE_LUMA8 sets where edge protection starts; the ramp runs to 2x it.
 */
#define AV_LUMA_EDGE_LUMA8      32UL
#define AV_LUMA_MOTION_EDGE_LUMA8 16UL
#define AV_LUMA_DETAIL_CLAMP    24L
#define AV_STATIC_RGB32       \
    0xe436eb7e, 0x524f, 0x11ce, 0x9f, 0x53, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70
#define AV_STATIC_RGB24       \
    0xe436eb7d, 0x524f, 0x11ce, 0x9f, 0x53, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70

typedef struct _AV_DEVICE_CONTEXT {
    #include "..\GpuBridgeFields.h"
    PKSDEVICE KsDevice;
    WDFDEVICE WdfDevice;
    HANDLE DiagnosticKey;
    PULONG volatile RgbBuffer;
    PULONG CaptureBuffer;
    PULONG NoiseBuffer;
    PULONG RefineBuffer;
    PUSHORT LumaHistory;
    USHORT LumaRows[3][AV_COLOR_WIDTH];
    PUSHORT RawBuffer;
    PUSHORT PendingRawBuffer;
    FAST_MUTEX RawQueueLock;
    WDFWORKITEM CaptureWorkItem;
    WDFWORKITEM ConversionWorkItem;
    HANDLE ConversionThreadHandle;
    KEVENT ConversionWorkEvent;
    KEVENT ConversionExitEvent;
    volatile LONG ConversionThreadStopRequested;
    volatile LONG ConversionThreadRunning;
    volatile LONG ConversionThreadPriority;
    volatile LONG ConversionThreadSignalCount;
    volatile LONG ConversionThreadWakeCount;
    WDFTIMER BackendIdleTimer;
    volatile LONG CaptureAttempted;
    volatile LONG CaptureQueued;
    volatile LONG CaptureStatus;
    volatile LONG CaptureRetryCount;
    volatile LONG CaptureSuccessAttempt;
    volatile LONG CaptureCompletedCount;
    volatile LONG CaptureFailureCount;
    volatile LONG CaptureLastDurationUs;
    volatile LONG CaptureMaximumDurationUs;
    volatile LONG CaptureActivationStatus;
    volatile LONG PublishedReal;
    volatile LONG RawMinimum;
    volatile LONG RawMaximum;
    volatile LONG RawMean;
    volatile LONG RawNonZero;
    volatile LONG PercentileBlack;
    volatile LONG PercentileWhite;
    volatile LONG ConversionCompleted;
    volatile LONG LastConversionDurationUs;
    volatile LONG MaximumConversionDurationUs;
    volatile LONG AnalysisDurationUs;
    volatile LONG ColorDurationUs;
    volatile LONG RefineDurationUs;
    volatile LONG UpscaleDurationUs;
    volatile LONG StableBlack;
    volatile LONG StableWhite;
    volatile LONG StableGainR;
    volatile LONG StableGainB;
    volatile LONG StableLiftQ10;
    volatile LONG ToneInitialized;
    volatile LONG ToneLiftPasses;
    volatile LONG FactoryTuningInitialized;
    volatile LONG StableAwbRefQ10;
    volatile LONG StableCcmPosQ10;
    volatile LONG AwbRgQ14;
    volatile LONG AwbBgQ14;
    volatile LONG StableFactoryGainR;
    volatile LONG StableFactoryGainB;
    volatile LONG HighlightCompression;
    volatile LONG AndroidLtmInitialized;
    volatile LONG AndroidLtmStrengthQ10;
    volatile LONG AndroidLtmP95;
    volatile LONG DenoiseStrengthQ10;
    volatile LONG DenoiseTemporalAccepted;
    volatile LONG RawConversionBusy;
    volatile LONG RawCopyAccepted;
    volatile LONG RawDroppedCount;
    volatile LONG RawDuplicateCount;
    volatile LONG RawPendingValid;
    volatile LONG RawPendingWidth;
    volatile LONG RawPendingHeight;
    volatile LONG RawPendingSignature;
    volatile LONG RawPendingQueuedCount;
    volatile LONG RawPendingDequeuedCount;
    volatile LONG RawPendingOverwriteCount;
    volatile LONG RawSignature;
    volatile LONG RawWidth;
    volatile LONG RawHeight;
    UCHAR ToneLutR[AV_RAW_LUT_SIZE];
    UCHAR ToneLutG[AV_RAW_LUT_SIZE];
    UCHAR ToneLutB[AV_RAW_LUT_SIZE];
    USHORT LinearLut[AV_RAW_LUT_SIZE];
    ULONG DetailReciprocalQ16[AV_RAW_LUT_SIZE];
    USHORT SourceXMap[AV_OUTPUT_WIDTH];
    USHORT SourceYMap[AV_OUTPUT_HEIGHT];
    USHORT FactoryLscR[AV_FACTORY_LSC_ROWS][AV_OUTPUT_WIDTH];
    USHORT FactoryLscG[AV_FACTORY_LSC_ROWS][AV_OUTPUT_WIDTH];
    USHORT FactoryLscB[AV_FACTORY_LSC_ROWS][AV_OUTPUT_WIDTH];
    USHORT FactoryLscYRow[AV_OUTPUT_HEIGHT];
    USHORT FactoryLscYWeightQ10[AV_OUTPUT_HEIGHT];
    USHORT FactoryBaseLscR[AV_COLOR_WIDTH * AV_COLOR_HEIGHT];
    USHORT FactoryBaseLscG[AV_COLOR_WIDTH * AV_COLOR_HEIGHT];
    USHORT FactoryBaseLscB[AV_COLOR_WIDTH * AV_COLOR_HEIGHT];
    volatile LONG ActivePinCount;
    volatile LONG PinCreateCount;
    volatile LONG PinCloseCount;
    volatile LONG RunCount;
    volatile LONG NonRunCount;
    volatile LONG TimerTickCount;
    volatile LONG ProcessCallCount;
    volatile LONG FrameDeliveredCount;
    volatile LONG PublishedGeneration;
    volatile LONG PublishedFirstPixel;
    volatile LONG PublishedCenterPixel;
    volatile LONG LastDeliveredGeneration;
    volatile LONG LastDeliveredFirstPixel;
    volatile LONG LastDeliveredCenterPixel;
    volatile LONG LastFrameExtent;
    volatile LONG LastFrameDataUsed;
    volatile LONG LastFrameOptions;
    volatile LONG LastOffsetRemaining;
    volatile LONG LastHeaderDataMatchesOffset;
    volatile LONG LastPresentationLow;
    volatile LONG LastPresentationHigh;
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
    volatile LONG DeliveryTick;
    volatile LONG LastDeliveredGeneration;
    BOOLEAN OutputRgb24;
    ULONGLONG FrameNumber;
} AV_PIN_CONTEXT, *PAV_PIN_CONTEXT;

#include "..\GpuRgbMetadata.h"

typedef struct _AV_TIMER_CONTEXT {
    PAV_PIN_CONTEXT PinContext;
} AV_TIMER_CONTEXT, *PAV_TIMER_CONTEXT;

typedef struct _AV_WORK_CONTEXT {
    PVOID DeviceContext;
} AV_WORK_CONTEXT, *PAV_WORK_CONTEXT;

typedef struct _AV_IDLE_TIMER_CONTEXT {
    PVOID DeviceContext;
} AV_IDLE_TIMER_CONTEXT, *PAV_IDLE_TIMER_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(AV_TIMER_CONTEXT, AvGetTimerContext);
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(AV_WORK_CONTEXT, AvGetWorkContext);
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(
    AV_IDLE_TIMER_CONTEXT, AvGetIdleTimerContext);

static WDFDRIVER g_AvWdfDriver;
static PDRIVER_UNLOAD g_AvKsUnload;

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_TIMER AvTimerCallback;
EVT_WDF_TIMER AvRearBackendIdleTimerCallback;
EVT_WDF_WORKITEM AvRearCaptureWorkItem;
EVT_WDF_WORKITEM AvRearConversionWorkItem;

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
static VOID AvProcessRearConversionQueue(PAV_DEVICE_CONTEXT Device);
static VOID AvRearConversionThread(PVOID StartContext);

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
AvWriteRearBridgeValue(_In_opt_ PVOID WriterContext,
                       _In_ PCWSTR Name,
                       _In_ ULONG Value)
{
    AvWriteRearValue((HANDLE)WriterContext, Name, Value);
}

static VOID
AvWriteRearDiagnostics(_In_ PAV_DEVICE_CONTEXT Device)
{
#define AV_WRITE_REAR(Name, Field) \
    AvWriteRearValue( \
        Device->DiagnosticKey, L"NabuAvRear_" Name, \
        (ULONG)InterlockedCompareExchange(&Device->Field, 0, 0))

    AvWriteRearValue(Device->DiagnosticKey, L"NabuAvRear_Build", 2265);
    AV_WRITE_REAR(L"CaptureAttempted", CaptureAttempted);
    AV_WRITE_REAR(L"CaptureQueued", CaptureQueued);
    AV_WRITE_REAR(L"CaptureStatus", CaptureStatus);
    AV_WRITE_REAR(L"CaptureRetryCount", CaptureRetryCount);
    AV_WRITE_REAR(L"CaptureSuccessAttempt", CaptureSuccessAttempt);
    AV_WRITE_REAR(L"CaptureCompletedCount", CaptureCompletedCount);
    AV_WRITE_REAR(L"CaptureFailureCount", CaptureFailureCount);
    AV_WRITE_REAR(L"CaptureLastDurationUs", CaptureLastDurationUs);
    AV_WRITE_REAR(
        L"CaptureMaximumDurationUs", CaptureMaximumDurationUs);
    AV_WRITE_REAR(L"CaptureActivationStatus", CaptureActivationStatus);
    AV_WRITE_REAR(L"PublishedReal", PublishedReal);
    AV_WRITE_REAR(L"RawMinimum", RawMinimum);
    AV_WRITE_REAR(L"RawMaximum", RawMaximum);
    AV_WRITE_REAR(L"RawMean", RawMean);
    AV_WRITE_REAR(L"RawNonZero", RawNonZero);
    AV_WRITE_REAR(L"PercentileBlack", PercentileBlack);
    AV_WRITE_REAR(L"PercentileWhite", PercentileWhite);
    AV_WRITE_REAR(L"ConversionCompleted", ConversionCompleted);
    AV_WRITE_REAR(L"LastConversionDurationUs", LastConversionDurationUs);
    AV_WRITE_REAR(
        L"MaximumConversionDurationUs", MaximumConversionDurationUs);
    AV_WRITE_REAR(L"AnalysisDurationUs", AnalysisDurationUs);
    AV_WRITE_REAR(L"ColorDurationUs", ColorDurationUs);
    AV_WRITE_REAR(L"RefineDurationUs", RefineDurationUs);
    AV_WRITE_REAR(L"UpscaleDurationUs", UpscaleDurationUs);
    AV_WRITE_REAR(L"StableBlack", StableBlack);
    AV_WRITE_REAR(L"StableWhite", StableWhite);
    AV_WRITE_REAR(L"StableGainR", StableGainR);
    AV_WRITE_REAR(L"StableGainB", StableGainB);
    AV_WRITE_REAR(L"StableFactoryGainR", StableFactoryGainR);
    AV_WRITE_REAR(L"StableFactoryGainB", StableFactoryGainB);
    AV_WRITE_REAR(L"StableAwbRefQ10", StableAwbRefQ10);
    AV_WRITE_REAR(L"StableCcmPosQ10", StableCcmPosQ10);
    AV_WRITE_REAR(L"AwbRgQ14", AwbRgQ14);
    AV_WRITE_REAR(L"AwbBgQ14", AwbBgQ14);
    AV_WRITE_REAR(L"ToneLiftPasses", ToneLiftPasses);
    AV_WRITE_REAR(L"ToneLiftQ10", StableLiftQ10);
    AV_WRITE_REAR(L"HighlightCompression", HighlightCompression);
    AV_WRITE_REAR(L"AndroidLtmInitialized", AndroidLtmInitialized);
    AV_WRITE_REAR(L"AndroidLtmStrengthQ10", AndroidLtmStrengthQ10);
    AV_WRITE_REAR(L"AndroidLtmP95", AndroidLtmP95);
    AV_WRITE_REAR(L"DenoiseStrengthQ10", DenoiseStrengthQ10);
    AV_WRITE_REAR(L"DenoiseTemporalAccepted", DenoiseTemporalAccepted);
    AV_WRITE_REAR(L"RawConversionBusy", RawConversionBusy);
    AV_WRITE_REAR(L"RawCopyAccepted", RawCopyAccepted);
    AV_WRITE_REAR(L"RawDroppedCount", RawDroppedCount);
    AV_WRITE_REAR(L"RawDuplicateCount", RawDuplicateCount);
    AV_WRITE_REAR(L"RawPendingValid", RawPendingValid);
    AV_WRITE_REAR(L"RawPendingQueuedCount", RawPendingQueuedCount);
    AV_WRITE_REAR(L"RawPendingDequeuedCount", RawPendingDequeuedCount);
    AV_WRITE_REAR(L"RawPendingOverwriteCount", RawPendingOverwriteCount);
    AV_WRITE_REAR(L"ConversionThreadRunning", ConversionThreadRunning);
    AV_WRITE_REAR(L"ConversionThreadPriority", ConversionThreadPriority);
    AV_WRITE_REAR(
        L"ConversionThreadSignalCount", ConversionThreadSignalCount);
    AV_WRITE_REAR(
        L"ConversionThreadWakeCount", ConversionThreadWakeCount);
    AV_WRITE_REAR(L"ActivePinCount", ActivePinCount);
    AV_WRITE_REAR(L"PinCreateCount", PinCreateCount);
    AV_WRITE_REAR(L"PinCloseCount", PinCloseCount);
    AV_WRITE_REAR(L"RunCount", RunCount);
    AV_WRITE_REAR(L"NonRunCount", NonRunCount);
    AV_WRITE_REAR(L"TimerTickCount", TimerTickCount);
    AV_WRITE_REAR(L"ProcessCallCount", ProcessCallCount);
    AV_WRITE_REAR(L"FrameDeliveredCount", FrameDeliveredCount);
    AV_WRITE_REAR(L"PublishedGeneration", PublishedGeneration);
    AV_WRITE_REAR(L"PublishedFirstPixel", PublishedFirstPixel);
    AV_WRITE_REAR(L"PublishedCenterPixel", PublishedCenterPixel);
    AV_WRITE_REAR(L"LastDeliveredGeneration", LastDeliveredGeneration);
    AV_WRITE_REAR(L"LastDeliveredFirstPixel", LastDeliveredFirstPixel);
    AV_WRITE_REAR(L"LastDeliveredCenterPixel", LastDeliveredCenterPixel);
    AV_WRITE_REAR(L"LastFrameExtent", LastFrameExtent);
    AV_WRITE_REAR(L"LastFrameDataUsed", LastFrameDataUsed);
    AV_WRITE_REAR(L"LastFrameOptions", LastFrameOptions);
    AV_WRITE_REAR(L"LastOffsetRemaining", LastOffsetRemaining);
    AV_WRITE_REAR(
        L"LastHeaderDataMatchesOffset", LastHeaderDataMatchesOffset);
    AV_WRITE_REAR(L"LastPresentationLow", LastPresentationLow);
    AV_WRITE_REAR(L"LastPresentationHigh", LastPresentationHigh);
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

    if (Device->WdfDevice != NULL) {
        NabuRearBackendWriteDiagnostics(
            Device->WdfDevice,
            AvWriteRearBridgeValue,
            Device->DiagnosticKey);
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

static ULONG
AvRearAveragePixel2(_In_ ULONG A, _In_ ULONG B)
{
    return 0xFF000000UL |
        (((((A >> 16) & 0xFFUL) + ((B >> 16) & 0xFFUL) + 1UL) >> 1) << 16) |
        (((((A >> 8) & 0xFFUL) + ((B >> 8) & 0xFFUL) + 1UL) >> 1) << 8) |
        ((((A & 0xFFUL) + (B & 0xFFUL) + 1UL) >> 1));
}

static ULONG
AvRearAveragePixel4(_In_ ULONG A, _In_ ULONG B,
                    _In_ ULONG C, _In_ ULONG D)
{
    return 0xFF000000UL |
        (((((A >> 16) & 0xFFUL) + ((B >> 16) & 0xFFUL) +
           ((C >> 16) & 0xFFUL) + ((D >> 16) & 0xFFUL) + 2UL) >> 2) << 16) |
        (((((A >> 8) & 0xFFUL) + ((B >> 8) & 0xFFUL) +
           ((C >> 8) & 0xFFUL) + ((D >> 8) & 0xFFUL) + 2UL) >> 2) << 8) |
        (((A & 0xFFUL) + (B & 0xFFUL) +
          (C & 0xFFUL) + (D & 0xFFUL) + 2UL) >> 2);
}

static ULONG
AvRearAddLumaDetail(_In_ ULONG Pixel, _In_ LONG Delta)
{
    LONG r = (LONG)((Pixel >> 16) & 0xFFUL) + Delta;
    LONG g = (LONG)((Pixel >> 8) & 0xFFUL) + Delta;
    LONG b = (LONG)(Pixel & 0xFFUL) + Delta;

    if (r < 0) r = 0; else if (r > 255) r = 255;
    if (g < 0) g = 0; else if (g > 255) g = 255;
    if (b < 0) b = 0; else if (b > 255) b = 255;
    return 0xFF000000UL | ((ULONG)r << 16) |
        ((ULONG)g << 8) | (ULONG)b;
}

static ULONG
AvRearApplyAndroidCac(_In_ ULONG Pixel, _In_ ULONG Reference)
{
    LONG r = (LONG)((Pixel >> 16) & 0xFFUL);
    LONG g = (LONG)((Pixel >> 8) & 0xFFUL);
    LONG b = (LONG)(Pixel & 0xFFUL);
    LONG rr = (LONG)((Reference >> 16) & 0xFFUL);
    LONG rg = (LONG)((Reference >> 8) & 0xFFUL);
    LONG rb = (LONG)(Reference & 0xFFUL);
    LONG y = (r + 2 * g + b) >> 2;
    LONG ry = (rr + 2 * rg + rb) >> 2;
    LONG cr = r - y;
    LONG cb = b - y;
    LONG refCr = rr - ry;
    LONG refCb = rb - ry;
    LONG dy = y - ry;
    LONG dcr = cr - refCr;
    LONG dcb = cb - refCb;

    if (dy < 0) dy = -dy;
    if (dcr < 0) dcr = -dcr;
    if (dcb < 0) dcb = -dcb;
    /* Exact rear CAC22 spot thresholds from the OV13B10 factory profile. */
    if (dy < 10 || (dcr <= 10 && dcb <= 10)) {
        return Pixel;
    }
    if (cr > refCr + 10) cr = refCr + 10;
    if (cr < refCr - 10) cr = refCr - 10;
    if (cb > refCb + 10) cb = refCb + 10;
    if (cb < refCb - 10) cb = refCb - 10;
    r = y + cr;
    b = y + cb;
    g = y - (54 * cr + 19 * cb) / 183;
    if (r < 0) r = 0; else if (r > 255) r = 255;
    if (g < 0) g = 0; else if (g > 255) g = 255;
    if (b < 0) b = 0; else if (b > 255) b = 255;
    return 0xFF000000UL | ((ULONG)r << 16) |
        ((ULONG)g << 8) | (ULONG)b;
}

static VOID
AvRearUpscaleWithBayerDetail(
    _Inout_ PAV_DEVICE_CONTEXT Device,
    _In_reads_(RawWidth * RawHeight) const USHORT *Raw,
    _In_ ULONG RawWidth,
    _In_ ULONG RawHeight,
    _In_reads_(AV_COLOR_WIDTH * AV_COLOR_HEIGHT) const ULONG *Base,
    _Out_writes_(AV_OUTPUT_WIDTH * AV_OUTPUT_HEIGHT) PULONG Output)
{
    ULONG detailStrengthQ8 = 0;
    ULONG detailThresholdRaw = 64UL;
    ULONG baseX;
    ULONG baseY;

    /* Preserve AV136 detail normally, but skip the optional native-detail
     * reconstruction for one recovery frame after a conversion spike. */
    if (InterlockedCompareExchange(
            &Device->LastConversionDurationUs, 0, 0) <=
            AV_CONVERSION_OVERLOAD_US &&
        RawWidth == AV_OUTPUT_WIDTH && RawHeight == AV_OUTPUT_HEIGHT) {
        ULONG p95 = (ULONG)InterlockedCompareExchange(
            &Device->AndroidLtmP95, 0, 0);

        /* Native Bayer detail is restored as scene noise permits, but never
         * switched off: at AV_REAR_DENOISE_FULL_P95 the old ramp returned 0,
         * which is what made a dim rear frame look painted.  The luma
         * denoiser now removes the grain this path would have amplified, so
         * the floor can be high. */
        if (p95 >= AV_REAR_DENOISE_OFF_P95) {
            detailStrengthQ8 = 320UL;
            detailThresholdRaw = 12UL;
        } else if (p95 > AV_REAR_DENOISE_FULL_P95) {
            ULONG sceneQ10 =
                ((p95 - AV_REAR_DENOISE_FULL_P95) * 1024UL + 32UL) /
                (AV_REAR_DENOISE_OFF_P95 -
                 AV_REAR_DENOISE_FULL_P95);

            detailStrengthQ8 =
                192UL + ((sceneQ10 * 128UL + 512UL) >> 10);
            detailThresholdRaw =
                24UL - ((sceneQ10 * 12UL + 512UL) >> 10);
        } else {
            detailStrengthQ8 = 192UL;
            detailThresholdRaw = 24UL;
        }
    }
    for (baseY = 0; baseY < AV_COLOR_HEIGHT; ++baseY) {
        ULONG previousY = baseY != 0UL ? baseY - 1UL : baseY;
        ULONG outputY = baseY * 2UL;

        for (baseX = 0; baseX < AV_COLOR_WIDTH; ++baseX) {
            ULONG previousX = baseX != 0UL ? baseX - 1UL : baseX;
            ULONG p00 = Base[baseY * AV_COLOR_WIDTH + baseX];
            ULONG pLeft = Base[baseY * AV_COLOR_WIDTH + previousX];
            ULONG pUp = Base[previousY * AV_COLOR_WIDTH + baseX];
            ULONG pUpLeft =
                Base[previousY * AV_COLOR_WIDTH + previousX];
            ULONG outputX = baseX * 2UL;

            p00 = AvRearApplyAndroidCac(
                p00, AvRearAveragePixel4(p00, pLeft, pUp, pUpLeft));
            /* Centre the 2x2 Bayer-cell chroma sample on native luma. */
            ULONG topLeft = AvRearAveragePixel4(
                p00, pLeft, pUp, pUpLeft);
            ULONG topRight = AvRearAveragePixel2(p00, pUp);
            ULONG bottomLeft = AvRearAveragePixel2(p00, pLeft);
            ULONG bottomRight = p00;

            if (detailStrengthQ8 != 0UL) {
                ULONG evenX = baseX * 2UL;
                ULONG evenY = (AV_COLOR_HEIGHT - 1UL - baseY) * 2UL;
                ULONG green0 = Raw[evenY * RawWidth + evenX];
                ULONG green1 = Raw[(evenY + 1UL) * RawWidth + evenX + 1UL];
                ULONG greenAverage = (green0 + green1 + 1UL) >> 1;
                ULONG greenIndex;
                ULONG greenBLeft;
                ULONG greenBDown;
                ULONG greenRRight;
                ULONG greenRUp;
                ULONG greenB;
                ULONG greenR;
                LONG deltaTopLeft;
                LONG deltaTopRight;
                LONG deltaBottomLeft;
                LONG deltaBottomRight;

                greenBLeft = evenX >= 2UL ?
                    Raw[(evenY + 1UL) * RawWidth + evenX - 1UL] : green1;
                greenBDown = evenY + 2UL < RawHeight ?
                    Raw[(evenY + 2UL) * RawWidth + evenX] : green0;
                greenRRight = evenX + 2UL < RawWidth ?
                    Raw[evenY * RawWidth + evenX + 2UL] : green0;
                greenRUp = evenY >= 2UL ?
                    Raw[(evenY - 1UL) * RawWidth + evenX + 1UL] : green1;
                greenB = (green0 + green1 + greenBLeft + greenBDown + 2UL) >> 2;
                greenR = (green0 + green1 + greenRRight + greenRUp + 2UL) >> 2;

                /* Vertical flip changes GRBG to BGGR: B, G1, G0, R. */
                {
                    LONG outputLuma = (LONG)(
                        (((p00 >> 16) & 0xFFUL) +
                         2UL * ((p00 >> 8) & 0xFFUL) +
                         (p00 & 0xFFUL)) >> 2);
                    LONG scaleQ8;
                    LONG rawDelta;
                    LONG magnitude;
                    greenIndex = greenAverage < AV_RAW_LUT_SIZE ?
                        greenAverage : AV_RAW_LUT_SIZE - 1UL;
                    scaleQ8 = (LONG)(((ULONG)outputLuma *
                        Device->DetailReciprocalQ16[greenIndex] + 128UL) >> 8);
                    if (scaleQ8 > 768) scaleQ8 = 768;
                    rawDelta = (LONG)greenB - (LONG)greenAverage;
                    magnitude = rawDelta < 0 ? -rawDelta : rawDelta;
                    deltaTopLeft = magnitude > (LONG)detailThresholdRaw ?
                        ((rawDelta < 0 ? -(magnitude - (LONG)detailThresholdRaw) : magnitude - (LONG)detailThresholdRaw) * scaleQ8 * (LONG)detailStrengthQ8) >> 16 : 0;
                    rawDelta = (LONG)green1 - (LONG)greenAverage;
                    magnitude = rawDelta < 0 ? -rawDelta : rawDelta;
                    deltaTopRight = magnitude > (LONG)detailThresholdRaw ?
                        ((rawDelta < 0 ? -(magnitude - (LONG)detailThresholdRaw) : magnitude - (LONG)detailThresholdRaw) * scaleQ8 * (LONG)detailStrengthQ8) >> 16 : 0;
                    rawDelta = (LONG)green0 - (LONG)greenAverage;
                    magnitude = rawDelta < 0 ? -rawDelta : rawDelta;
                    deltaBottomLeft = magnitude > (LONG)detailThresholdRaw ?
                        ((rawDelta < 0 ? -(magnitude - (LONG)detailThresholdRaw) : magnitude - (LONG)detailThresholdRaw) * scaleQ8 * (LONG)detailStrengthQ8) >> 16 : 0;
                    rawDelta = (LONG)greenR - (LONG)greenAverage;
                    magnitude = rawDelta < 0 ? -rawDelta : rawDelta;
                    deltaBottomRight = magnitude > (LONG)detailThresholdRaw ?
                        ((rawDelta < 0 ? -(magnitude - (LONG)detailThresholdRaw) : magnitude - (LONG)detailThresholdRaw) * scaleQ8 * (LONG)detailStrengthQ8) >> 16 : 0;
                }
                /* Android preview ASF30 layer-1 clamp is exactly +/-32. */
                if (deltaTopLeft < -32) deltaTopLeft = -32;
                if (deltaTopLeft > 32) deltaTopLeft = 32;
                if (deltaTopRight < -32) deltaTopRight = -32;
                if (deltaTopRight > 32) deltaTopRight = 32;
                if (deltaBottomLeft < -32) deltaBottomLeft = -32;
                if (deltaBottomLeft > 32) deltaBottomLeft = 32;
                if (deltaBottomRight < -32) deltaBottomRight = -32;
                if (deltaBottomRight > 32) deltaBottomRight = 32;
                if (deltaTopLeft != 0) {
                    topLeft = AvRearAddLumaDetail(topLeft, deltaTopLeft);
                }
                if (deltaTopRight != 0) {
                    topRight = AvRearAddLumaDetail(topRight, deltaTopRight);
                }
                if (deltaBottomLeft != 0) {
                    bottomLeft = AvRearAddLumaDetail(bottomLeft, deltaBottomLeft);
                }
                if (deltaBottomRight != 0) {
                    bottomRight = AvRearAddLumaDetail(bottomRight, deltaBottomRight);
                }
            }
            Output[outputY * AV_OUTPUT_WIDTH + outputX] = topLeft;
            Output[outputY * AV_OUTPUT_WIDTH + outputX + 1UL] = topRight;
            Output[(outputY + 1UL) * AV_OUTPUT_WIDTH + outputX] = bottomLeft;
            Output[(outputY + 1UL) * AV_OUTPUT_WIDTH + outputX + 1UL] =
                bottomRight;
        }
    }
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
AvLiftPreviewTone(_In_ UCHAR Value, _In_ ULONG LiftQ10)
{
    ULONG original = Value;
    ULONG value = Value;
    ULONG wholePasses = LiftQ10 >> 10;
    ULONG fraction = LiftQ10 & 1023UL;
    ULONG pass;

    for (pass = 0; pass < wholePasses; ++pass) {
        value +=
            (value * (255UL - value) + 127UL) / 255UL;
        if (value > 255UL) {
            value = 255UL;
        }
    }
    if (fraction != 0UL && value < 255UL) {
        ULONG next = value +
            (value * (255UL - value) + 127UL) / 255UL;
        if (next > 255UL) {
            next = 255UL;
        }
        value =
            (value * (1024UL - fraction) + next * fraction + 512UL) >> 10;
    }

    if (value > original) {
        ULONG weightQ10;

        if (original <= 64UL) {
            weightQ10 = 1024UL;
        } else if (original >= 160UL) {
            weightQ10 = 0UL;
        } else {
            weightQ10 =
                ((160UL - original) * 1024UL + 48UL) / 96UL;
        }
        value = original +
            (((value - original) * weightQ10 + 512UL) >> 10);
    }
    return (UCHAR)value;
}

static UCHAR
AvScaleRearRaw(
    _In_ USHORT Value,
    _In_ ULONG Black,
    _In_ ULONGLONG ScaleQ16,
    _In_ ULONG LiftQ10)
{
    ULONGLONG scaled;
    UCHAR result;

    if ((ULONG)Value <= Black) {
        return 0;
    }
    scaled =
        ((ULONGLONG)((ULONG)Value - Black) * ScaleQ16 +
         0x8000ULL) >> 16;
    result = AvLiftPreviewTone(
        (UCHAR)(scaled > 255ULL ? 255ULL : scaled),
        LiftQ10);
    if (result > 184U) {
        result = (UCHAR)(184UL +
            (((ULONG)result - 184UL + 1UL) / 2UL));
    }
    return result;
}

static VOID
AvInitializeRearFactoryLsc(_Inout_ PAV_DEVICE_CONTEXT Device)
{
    ULONG row;
    ULONG x;
    ULONG y;

    if (InterlockedCompareExchange(
            &Device->FactoryTuningInitialized, 0, 0) != 0) {
        return;
    }
    for (row = 0; row < AV_FACTORY_LSC_ROWS; ++row) {
        for (x = 0; x < AV_OUTPUT_WIDTH; ++x) {
            ULONG positionQ10 =
                (x * ((AV_FACTORY_LSC_COLS - 1UL) << 10)) /
                (AV_OUTPUT_WIDTH - 1UL);
            ULONG column = positionQ10 >> 10;
            ULONG weightQ10 = positionQ10 & 1023UL;
            ULONG base;

            if (column >= AV_FACTORY_LSC_COLS - 1UL) {
                column = AV_FACTORY_LSC_COLS - 2UL;
                weightQ10 = 1024UL;
            }
            base = row * AV_FACTORY_LSC_COLS + column;
            Device->FactoryLscR[row][x] = (USHORT)(
                ((ULONG)AvFactoryRearLscR[base] *
                     (1024UL - weightQ10) +
                 (ULONG)AvFactoryRearLscR[base + 1UL] *
                     weightQ10 + 512UL) >> 10);
            Device->FactoryLscG[row][x] = (USHORT)(
                ((ULONG)AvFactoryRearLscG[base] *
                     (1024UL - weightQ10) +
                 (ULONG)AvFactoryRearLscG[base + 1UL] *
                     weightQ10 + 512UL) >> 10);
            Device->FactoryLscB[row][x] = (USHORT)(
                ((ULONG)AvFactoryRearLscB[base] *
                     (1024UL - weightQ10) +
                 (ULONG)AvFactoryRearLscB[base + 1UL] *
                     weightQ10 + 512UL) >> 10);
        }
    }
    for (y = 0; y < AV_OUTPUT_HEIGHT; ++y) {
        ULONG positionQ10 =
            (y * ((AV_FACTORY_LSC_ROWS - 1UL) << 10)) /
            (AV_OUTPUT_HEIGHT - 1UL);
        ULONG yRow = positionQ10 >> 10;
        ULONG yWeightQ10 = positionQ10 & 1023UL;

        if (yRow >= AV_FACTORY_LSC_ROWS - 1UL) {
            yRow = AV_FACTORY_LSC_ROWS - 2UL;
            yWeightQ10 = 1024UL;
        }
        Device->FactoryLscYRow[y] = (USHORT)yRow;
        Device->FactoryLscYWeightQ10[y] = (USHORT)yWeightQ10;
    }
    /* AV119: resolve the Android lens mesh once on the 640x480 Bayer-cell
     * colour grid. This removes six interpolating multiplies per pixel. */
    for (y = 0; y < AV_COLOR_HEIGHT; ++y) {
        ULONG outputY = y * 2UL;
        ULONG yRow = Device->FactoryLscYRow[outputY];
        ULONG yWeightQ10 = Device->FactoryLscYWeightQ10[outputY];

        for (x = 0; x < AV_COLOR_WIDTH; ++x) {
            ULONG outputX = x * 2UL;
            ULONG index = y * AV_COLOR_WIDTH + x;
            ULONG topIndex = yRow * AV_OUTPUT_WIDTH + outputX;
            ULONG bottomIndex = topIndex + AV_OUTPUT_WIDTH;

            Device->FactoryBaseLscR[index] = (USHORT)(
                ((ULONG)(&Device->FactoryLscR[0][0])[topIndex] *
                     (1024UL - yWeightQ10) +
                 (ULONG)(&Device->FactoryLscR[0][0])[bottomIndex] *
                     yWeightQ10 + 512UL) >> 10);
            Device->FactoryBaseLscG[index] = (USHORT)(
                ((ULONG)(&Device->FactoryLscG[0][0])[topIndex] *
                     (1024UL - yWeightQ10) +
                 (ULONG)(&Device->FactoryLscG[0][0])[bottomIndex] *
                     yWeightQ10 + 512UL) >> 10);
            Device->FactoryBaseLscB[index] = (USHORT)(
                ((ULONG)(&Device->FactoryLscB[0][0])[topIndex] *
                     (1024UL - yWeightQ10) +
                 (ULONG)(&Device->FactoryLscB[0][0])[bottomIndex] *
                     yWeightQ10 + 512UL) >> 10);
        }
    }
    for (x = 0; x < AV_RAW_LUT_SIZE; ++x) {
        ULONG linear;
        ULONG gammaIndex;
        ULONG fractionQ10;
        ULONG output10;
        ULONG detailDenominator = x > 60UL ? x - 60UL : 1UL;

        Device->DetailReciprocalQ16[x] =
            (65536UL + detailDenominator / 2UL) / detailDenominator;

        if (x <= AV_FACTORY_BLACK_LEVEL) {
            linear = 0;
        } else {
            linear = ((x - AV_FACTORY_BLACK_LEVEL) * 1023UL +
                (1023UL - AV_FACTORY_BLACK_LEVEL) / 2UL) /
                (1023UL - AV_FACTORY_BLACK_LEVEL);
        }
        Device->LinearLut[x] = (USHORT)linear;
        if (x >= 1023UL) {
            Device->ToneLutR[x] = 255U;
            continue;
        }
        gammaIndex = x >> 2;
        fractionQ10 = (x & 3UL) << 8;
        output10 =
            ((ULONG)AvFactoryRearGamma[gammaIndex] *
                 (1024UL - fractionQ10) +
             (ULONG)AvFactoryRearGamma[gammaIndex + 1UL] *
                 fractionQ10 + 512UL) >> 10;
        output10 = (output10 + 2UL) >> 2;
        Device->ToneLutR[x] =
            (UCHAR)(output10 > 255UL ? 255UL : output10);
    }
    KeMemoryBarrier();
    InterlockedExchange(&Device->FactoryTuningInitialized, 1);
}

static ULONG
AvRearLinearize10(_In_ ULONG Value)
{
    if (Value <= AV_FACTORY_BLACK_LEVEL) {
        return 0;
    }
    if (Value > 1023UL) {
        Value = 1023UL;
    }
    return ((Value - AV_FACTORY_BLACK_LEVEL) * 1023UL +
            (1023UL - AV_FACTORY_BLACK_LEVEL) / 2UL) /
           (1023UL - AV_FACTORY_BLACK_LEVEL);
}

static ULONG
AvRearFactoryLsc(
    _In_ PAV_DEVICE_CONTEXT Device,
    _In_reads_(AV_FACTORY_LSC_ROWS * AV_OUTPUT_WIDTH) const USHORT *Table,
    _In_ ULONG X,
    _In_ ULONG Y)
{
    ULONG row = Device->FactoryLscYRow[Y];
    ULONG weightQ10 = Device->FactoryLscYWeightQ10[Y];
    return ((ULONG)Table[row * AV_OUTPUT_WIDTH + X] *
                (1024UL - weightQ10) +
            (ULONG)Table[(row + 1UL) * AV_OUTPUT_WIDTH + X] *
                weightQ10 + 512UL) >> 10;
}

static UCHAR
AvRearFactoryGamma8(_In_ ULONG Value)
{
    ULONG index;
    ULONG fractionQ10;
    ULONG output10;

    if (Value >= 1023UL) {
        return 255U;
    }
    index = Value >> 2;
    fractionQ10 = (Value & 3UL) << 8;
    output10 =
        ((ULONG)AvFactoryRearGamma[index] *
             (1024UL - fractionQ10) +
         (ULONG)AvFactoryRearGamma[index + 1UL] *
             fractionQ10 + 512UL) >> 10;
    output10 = (output10 + 2UL) >> 2;
    return (UCHAR)(output10 > 255UL ? 255UL : output10);
}

static ULONG
AvRearCcmPositionForReference(_In_ ULONG Reference)
{
    static const USHORT positionsQ10[AV_FACTORY_AWB_REFS] = {
        4096, 4096, 3072, 2048, 1024,
        1024, 1024, 0, 0, 0
    };

    return positionsQ10[
        Reference < AV_FACTORY_AWB_REFS ?
            Reference : AV_FACTORY_AWB_REFS - 1UL];
}

static __forceinline ULONG
AvPackRearFactoryPixel(
    _In_ PAV_DEVICE_CONTEXT Device,
    _In_ ULONG RawR,
    _In_ ULONG RawG,
    _In_ ULONG RawB,
    _In_ ULONG CellIndex,
    _In_ ULONG GainR,
    _In_ ULONG GainB,
    _In_reads_(9) const LONG *Matrix)
{
    ULONG r = Device->LinearLut[RawR];
    ULONG g = Device->LinearLut[RawG];
    ULONG b = Device->LinearLut[RawB];
    ULONG lscR = Device->FactoryBaseLscR[CellIndex];
    ULONG lscG = Device->FactoryBaseLscG[CellIndex];
    ULONG lscB = Device->FactoryBaseLscB[CellIndex];
    LONG correctedR;
    LONG correctedG;
    LONG correctedB;
    UCHAR outR;
    UCHAR outG;
    UCHAR outB;

    r = (r * lscR + 512UL) >> 10;
    g = (g * lscG + 512UL) >> 10;
    b = (b * lscB + 512UL) >> 10;
    r = (r * GainR + 512UL) >> 10;
    b = (b * GainB + 512UL) >> 10;
    correctedR =
        Matrix[0] * (LONG)r + Matrix[1] * (LONG)g +
        Matrix[2] * (LONG)b;
    correctedG =
        Matrix[3] * (LONG)r + Matrix[4] * (LONG)g +
        Matrix[5] * (LONG)b;
    correctedB =
        Matrix[6] * (LONG)r + Matrix[7] * (LONG)g +
        Matrix[8] * (LONG)b;
    correctedR >>= 12;
    correctedG >>= 12;
    correctedB >>= 12;
    if (correctedR < 0) correctedR = 0;
    if (correctedG < 0) correctedG = 0;
    if (correctedB < 0) correctedB = 0;
    if (correctedR > 1023) correctedR = 1023;
    if (correctedG > 1023) correctedG = 1023;
    if (correctedB > 1023) correctedB = 1023;
    outR = Device->ToneLutR[(ULONG)correctedR];
    outG = Device->ToneLutR[(ULONG)correctedG];
    outB = Device->ToneLutR[(ULONG)correctedB];
    return 0xFF000000UL |
        ((ULONG)outR << 16) |
        ((ULONG)outG << 8) |
        (ULONG)outB;
}

/*
 * AV99 measured stock-Android OV13B10 preview transfer.  The control points
 * are same-scene AV96 -> Android luminance quantiles:
 *   0->0, 12->42, 30->114, 64->179, 255->255.
 * The low-light maximum is the factory AEC HistExt gain (3.5x).  Strength is
 * reduced between the factory LTM dark boundary (0.4 * 255 = 102) and bright
 * boundary ((1 - 0.25) * 255 = 191), then rate-limited by Android's ADRC
 * convergence speed of 0.15 per frame.
 */
static UCHAR
AvRearAndroidTone8(_In_ ULONG Value)
{
    ULONG output;

    if (Value >= 255UL) {
        return 255U;
    }
    if (Value <= 12UL) {
        output = (Value * 42UL + 6UL) / 12UL;
    } else if (Value <= 30UL) {
        output = 42UL +
            ((Value - 12UL) * 72UL + 9UL) / 18UL;
    } else if (Value <= 64UL) {
        output = 114UL +
            ((Value - 30UL) * 65UL + 17UL) / 34UL;
    } else {
        output = 179UL +
            ((Value - 64UL) * 76UL + 95UL) / 191UL;
    }
    return (UCHAR)(output > 255UL ? 255UL : output);
}

static __forceinline ULONG
AvRearPixelLuma(_In_ ULONG Pixel)
{
    ULONG red = (Pixel >> 16) & 0xFFUL;
    ULONG green = (Pixel >> 8) & 0xFFUL;
    ULONG blue = Pixel & 0xFFUL;

    return (54UL * red + 183UL * green + 19UL * blue + 128UL) >> 8;
}

static VOID
AvRearBuildAndroidDeltaLut(
    _Inout_ PAV_DEVICE_CONTEXT Device,
    _Out_writes_(256) UCHAR *DeltaLut)
{
    ULONG strengthQ10 = (ULONG)InterlockedCompareExchange(
        &Device->AndroidLtmStrengthQ10, 0, 0);
    ULONG i;

    if (InterlockedCompareExchange(
            &Device->AndroidLtmInitialized, 0, 0) == 0) {
        strengthQ10 = 512UL;
    }
    for (i = 0; i < 256UL; ++i) {
        ULONG full = AvRearAndroidTone8(i);

        DeltaLut[i] = (UCHAR)(
            ((full - i) * strengthQ10 + 512UL) / 1024UL);
    }
}

static __forceinline ULONG
AvRearApplyAndroidTonePixel(
    _In_ ULONG Pixel,
    _In_reads_(256) const UCHAR *DeltaLut)
{
    ULONG red = (Pixel >> 16) & 0xFFUL;
    ULONG green = (Pixel >> 8) & 0xFFUL;
    ULONG blue = Pixel & 0xFFUL;
    ULONG delta = DeltaLut[AvRearPixelLuma(Pixel)];

    red += delta;
    green += delta;
    blue += delta;
    if (red > 255UL) red = 255UL;
    if (green > 255UL) green = 255UL;
    if (blue > 255UL) blue = 255UL;
    return 0xFF000000UL | (red << 16) | (green << 8) | blue;
}

static __forceinline ULONG
AvRearApplyFastTemporalChroma(
    _In_ PAV_DEVICE_CONTEXT Device,
    _In_ ULONG Pixel,
    _In_ ULONG Index,
    _In_ ULONG Divisor,
    _Inout_ PULONG Accepted)
{
    ULONG previousPixel;
    ULONG currentR;
    ULONG currentG;
    ULONG currentB;
    ULONG previousR;
    ULONG previousG;
    ULONG previousB;
    LONG deltaG;

    if (Divisor == 0UL || Device->RgbBuffer == NULL) {
        return Pixel;
    }
    previousPixel = Device->RgbBuffer[Index];
    currentR = (Pixel >> 16) & 0xFFUL;
    currentG = (Pixel >> 8) & 0xFFUL;
    currentB = Pixel & 0xFFUL;
    previousR = (previousPixel >> 16) & 0xFFUL;
    previousG = (previousPixel >> 8) & 0xFFUL;
    previousB = previousPixel & 0xFFUL;
    deltaG = (LONG)currentG - (LONG)previousG;
    if (deltaG > (LONG)AV_DENOISE_EDGE_THRESHOLD ||
        deltaG < -(LONG)AV_DENOISE_EDGE_THRESHOLD) {
        return Pixel;
    }
    if (Divisor == 2UL) {
        currentR = (currentR + previousR + 1) >> 1;
        currentG = (currentG * 3 + previousG + 2) >> 2;
        currentB = (currentB + previousB + 1) >> 1;
    } else {
        currentR = (currentR * 3 + previousR + 2) >> 2;
        currentB = (currentB * 3 + previousB + 2) >> 2;
    }
    *Accepted += 1UL;
    return 0xFF000000UL | ((ULONG)currentR << 16) |
        ((ULONG)currentG << 8) | (ULONG)currentB;
}

static VOID
AvRearUpdateAndroidLtm(
    _Inout_ PAV_DEVICE_CONTEXT Device,
    _In_reads_(256) const ULONG *Histogram,
    _In_ ULONG SampleCount)
{
    ULONG highTarget;
    ULONG cumulative = 0;
    ULONG p95 = 255UL;
    ULONG targetStrengthQ10;
    ULONG strengthQ10;
    ULONG i;

    if (SampleCount == 0UL) {
        return;
    }
    highTarget = (SampleCount * 95UL + 99UL) / 100UL;
    for (i = 0; i < 256UL; ++i) {
        cumulative += Histogram[i];
        if (cumulative >= highTarget) {
            p95 = i;
            break;
        }
    }
    if (p95 <= AV_ANDROID_LTM_DARK_END) {
        targetStrengthQ10 = 1024UL;
    } else if (p95 >= AV_ANDROID_LTM_BRIGHT_START) {
        targetStrengthQ10 = 0UL;
    } else {
        targetStrengthQ10 =
            ((AV_ANDROID_LTM_BRIGHT_START - p95) * 1024UL + 44UL) /
            (AV_ANDROID_LTM_BRIGHT_START - AV_ANDROID_LTM_DARK_END);
    }
    strengthQ10 = (ULONG)InterlockedCompareExchange(
        &Device->AndroidLtmStrengthQ10, 0, 0);
    if (InterlockedCompareExchange(
            &Device->AndroidLtmInitialized, 1, 0) == 0) {
        strengthQ10 = targetStrengthQ10;
    } else if (targetStrengthQ10 > strengthQ10 +
               AV_ANDROID_LTM_STEP_Q10) {
        strengthQ10 += AV_ANDROID_LTM_STEP_Q10;
    } else if (strengthQ10 > targetStrengthQ10 +
               AV_ANDROID_LTM_STEP_Q10) {
        strengthQ10 -= AV_ANDROID_LTM_STEP_Q10;
    } else {
        strengthQ10 = targetStrengthQ10;
    }
    InterlockedExchange(
        &Device->AndroidLtmStrengthQ10, (LONG)strengthQ10);
    InterlockedExchange(&Device->AndroidLtmP95, (LONG)p95);
}

/*
 * Luma noise reduction, temporal luma filtering and edge-protected unsharp on
 * the 640x480 colour grid, written into RefineBuffer for the 2x output pass.
 *
 * Until now every filter in this pipeline touched only R and B, so the grain
 * that dominates a dim rear frame passed through untouched and nothing ever
 * put detail back.  The 5-tap local mean does double duty here: it is what a
 * flat pixel is pulled towards, and it is the blur the high-pass is taken
 * against, so the two stages cancel each other in noise-only areas and the
 * sharpen can only fire on an edge the denoiser chose to keep.
 */
static VOID
AvRearRefineLuma(
    _Inout_ PAV_DEVICE_CONTEXT Device,
    _In_ ULONG NoiseQ10)
{
    const PULONG source = Device->NoiseBuffer;
    PULONG destination = Device->RefineBuffer;
    PUSHORT history = Device->LumaHistory;
    PUSHORT previousRow;
    PUSHORT currentRow;
    PUSHORT nextRow;
    ULONG denoiseQ10;
    ULONG temporalQ10;
    ULONG sharpenQ10;
    ULONG y;
    ULONG x;

    if (source == NULL || destination == NULL || history == NULL) {
        return;
    }
    denoiseQ10 = 256UL + ((NoiseQ10 * 320UL) >> 10);
    temporalQ10 = 256UL + ((NoiseQ10 * 320UL) >> 10);
    sharpenQ10 = 384UL + (((1024UL - NoiseQ10) * 256UL) >> 10);
    previousRow = &Device->LumaRows[0][0];
    currentRow = &Device->LumaRows[1][0];
    nextRow = &Device->LumaRows[2][0];
    for (x = 0; x < AV_COLOR_WIDTH; ++x) {
        const USHORT luma =
            (USHORT)AvRearPixelLuma(source[x]);

        previousRow[x] = luma;
        currentRow[x] = luma;
    }
    for (y = 0; y < AV_COLOR_HEIGHT; ++y) {
        const ULONG rowBase = y * AV_COLOR_WIDTH;
        const PULONG row = source + rowBase;
        PULONG out = destination + rowBase;
        PUSHORT historyRow = history + rowBase;

        if (y + 1UL < AV_COLOR_HEIGHT) {
            const PULONG below = row + AV_COLOR_WIDTH;

            for (x = 0; x < AV_COLOR_WIDTH; ++x) {
                nextRow[x] = (USHORT)AvRearPixelLuma(below[x]);
            }
        } else {
            for (x = 0; x < AV_COLOR_WIDTH; ++x) {
                nextRow[x] = currentRow[x];
            }
        }
        for (x = 0; x < AV_COLOR_WIDTH; ++x) {
            const ULONG center = currentRow[x];
            const ULONG left = x != 0UL ? currentRow[x - 1UL] : center;
            const ULONG right = x + 1UL < AV_COLOR_WIDTH ?
                currentRow[x + 1UL] : center;
            const ULONG up = previousRow[x];
            const ULONG down = nextRow[x];
            ULONG gradient = center > left ?
                center - left : left - center;
            ULONG delta = center > right ?
                center - right : right - center;
            ULONG mean;
            ULONG flatQ10;
            ULONG weight;
            LONG smoothed;
            ULONG temporal;
            ULONG difference;
            LONG refined;
            LONG highPass;
            LONG adjustment;

            if (delta > gradient) gradient = delta;
            delta = center > up ? center - up : up - center;
            if (delta > gradient) gradient = delta;
            delta = center > down ? center - down : down - center;
            if (delta > gradient) gradient = delta;
            mean = (left + right + up + down +
                center * 4UL + 4UL) >> 3;
            /* AV_LUMA_EDGE_LUMA8 * 2 maps onto Q10 in one shift. */
            flatQ10 = gradient >= AV_LUMA_EDGE_LUMA8 * 2UL ? 0UL :
                (AV_LUMA_EDGE_LUMA8 * 2UL - gradient) << 4;
            weight = (denoiseQ10 * flatQ10) >> 10;
            smoothed = (LONG)center +
                ((((LONG)mean - (LONG)center) * (LONG)weight) >> 10);
            difference = (ULONG)(smoothed > (LONG)historyRow[x] ?
                smoothed - (LONG)historyRow[x] :
                (LONG)historyRow[x] - smoothed);
            temporal = difference >= AV_LUMA_MOTION_EDGE_LUMA8 ? 0UL :
                (AV_LUMA_MOTION_EDGE_LUMA8 - difference) << 6;
            temporal = (temporalQ10 * temporal) >> 10;
            refined = smoothed +
                ((((LONG)historyRow[x] - smoothed) *
                    (LONG)temporal) >> 10);
            highPass = refined - (LONG)mean;
            adjustment = (highPass * (LONG)sharpenQ10) >> 10;
            if (adjustment > (LONG)AV_LUMA_DETAIL_CLAMP) {
                adjustment = (LONG)AV_LUMA_DETAIL_CLAMP;
            } else if (adjustment < -(LONG)AV_LUMA_DETAIL_CLAMP) {
                adjustment = -(LONG)AV_LUMA_DETAIL_CLAMP;
            }
            refined += adjustment;
            if (refined < 0) refined = 0;
            if (refined > 255) refined = 255;
            historyRow[x] = (USHORT)refined;
            out[x] = AvRearAddLumaDetail(
                row[x], (LONG)refined - (LONG)center);
        }
        {
            PUSHORT sliding = previousRow;

            previousRow = currentRow;
            currentRow = nextRow;
            nextRow = sliding;
        }
    }
}

/* Chroma-only ANR/TF approximation driven by the stock preview p95. */
static VOID
AvRearApplyLowLightDenoise(_Inout_ PAV_DEVICE_CONTEXT Device)
{
    ULONG p95;
    ULONG targetStrengthQ10;
    ULONG strengthQ10;
    ULONG temporalAccepted = 0;
    PULONG source;
    PULONG destination;
    PULONG previous;
    ULONG x;
    ULONG y;

    if (Device == NULL || Device->CaptureBuffer == NULL ||
        Device->NoiseBuffer == NULL) {
        return;
    }
    p95 = (ULONG)InterlockedCompareExchange(
        &Device->AndroidLtmP95, 0, 0);
    if (p95 <= AV_REAR_DENOISE_FULL_P95) {
        targetStrengthQ10 = 1024UL;
    } else if (p95 >= AV_REAR_DENOISE_OFF_P95) {
        targetStrengthQ10 = 0UL;
    } else {
        targetStrengthQ10 =
            ((AV_REAR_DENOISE_OFF_P95 - p95) * 1024UL + 40UL) /
            (AV_REAR_DENOISE_OFF_P95 - AV_REAR_DENOISE_FULL_P95);
    }
    strengthQ10 = (ULONG)InterlockedCompareExchange(
        &Device->DenoiseStrengthQ10, 0, 0);
    if (strengthQ10 == 0UL || targetStrengthQ10 == 0UL) {
        strengthQ10 = targetStrengthQ10;
    } else if (targetStrengthQ10 > strengthQ10 + 128UL) {
        strengthQ10 += 128UL;
    } else if (strengthQ10 > targetStrengthQ10 + 128UL) {
        strengthQ10 -= 128UL;
    } else {
        strengthQ10 = targetStrengthQ10;
    }
    InterlockedExchange(
        &Device->DenoiseStrengthQ10, (LONG)strengthQ10);
    if (strengthQ10 == 0UL) {
        InterlockedExchange(&Device->DenoiseTemporalAccepted, 0);
        return;
    }

    source = Device->CaptureBuffer;
    destination = Device->NoiseBuffer;
    previous = InterlockedCompareExchange(
        &Device->PublishedReal, 0, 0) != 0 ? Device->RgbBuffer : NULL;
    RtlCopyMemory(destination, source, AV_RGB_FRAME_BYTES);
    for (y = 1UL; y + 1UL < AV_OUTPUT_HEIGHT; ++y) {
        for (x = 1UL; x + 1UL < AV_OUTPUT_WIDTH; ++x) {
            ULONG index = y * AV_OUTPUT_WIDTH + x;
            ULONG center = source[index];
            ULONG centerY = AvRearPixelLuma(center);
            LONG centerCr = (LONG)((center >> 16) & 0xFFUL) - (LONG)centerY;
            LONG centerCb = (LONG)(center & 0xFFUL) - (LONG)centerY;
            LONG sumCr = centerCr * 4;
            LONG sumCb = centerCb * 4;
            LONG weight = 4;
            const LONG offsets[4] = {
                -1, 1, -(LONG)AV_OUTPUT_WIDTH, (LONG)AV_OUTPUT_WIDTH
            };
            ULONG neighbour;
            LONG filteredCr;
            LONG filteredCb;
            LONG outCr;
            LONG outCb;
            LONG outR;
            LONG outG;
            LONG outB;

            for (neighbour = 0; neighbour < RTL_NUMBER_OF(offsets);
                 ++neighbour) {
                ULONG pixel = source[(LONG)index + offsets[neighbour]];
                ULONG pixelY = AvRearPixelLuma(pixel);
                LONG deltaY = (LONG)pixelY - (LONG)centerY;

                if (deltaY < 0) deltaY = -deltaY;
                if ((ULONG)deltaY <= AV_DENOISE_EDGE_THRESHOLD) {
                    sumCr += (LONG)((pixel >> 16) & 0xFFUL) -
                        (LONG)pixelY;
                    sumCb += (LONG)(pixel & 0xFFUL) - (LONG)pixelY;
                    weight += 1;
                }
            }
            if (previous != NULL) {
                ULONG pixel = previous[index];
                ULONG pixelY = AvRearPixelLuma(pixel);
                LONG deltaY = (LONG)pixelY - (LONG)centerY;

                if (deltaY < 0) deltaY = -deltaY;
                if ((ULONG)deltaY <= AV_DENOISE_EDGE_THRESHOLD) {
                    sumCr += 2 * ((LONG)((pixel >> 16) & 0xFFUL) -
                        (LONG)pixelY);
                    sumCb += 2 * ((LONG)(pixel & 0xFFUL) -
                        (LONG)pixelY);
                    weight += 2;
                    temporalAccepted += 1;
                }
            }
            filteredCr = sumCr / weight;
            filteredCb = sumCb / weight;
            outCr = (centerCr * (LONG)(1024UL - strengthQ10) +
                     filteredCr * (LONG)strengthQ10 + 512) / 1024;
            outCb = (centerCb * (LONG)(1024UL - strengthQ10) +
                     filteredCb * (LONG)strengthQ10 + 512) / 1024;
            outR = (LONG)centerY + outCr;
            outB = (LONG)centerY + outCb;
            outG = (LONG)centerY - (54 * outCr + 19 * outCb) / 183;
            if (outR < 0) outR = 0;
            if (outG < 0) outG = 0;
            if (outB < 0) outB = 0;
            if (outR > 255) outR = 255;
            if (outG > 255) outG = 255;
            if (outB > 255) outB = 255;
            destination[index] = 0xFF000000UL |
                ((ULONG)outR << 16) | ((ULONG)outG << 8) | (ULONG)outB;
        }
    }
    Device->CaptureBuffer = destination;
    Device->NoiseBuffer = source;
    InterlockedExchange(
        &Device->DenoiseTemporalAccepted, (LONG)temporalAccepted);
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
    ULONG histogram[256];
    ULONG androidHistogram[256];
    UCHAR androidDeltaLut[256];
    ULONG androidSampleCount = 0;
    ULONG cumulative;
    ULONG sampleTotal;
    ULONG lowTarget;
    ULONG highTarget;
    ULONG percentileLow = 0;
    ULONG percentileHigh = 255;
    BOOLEAN percentileLowFound = FALSE;
    ULONG gainR = AV_GAIN_ONE;
    ULONG gainB = AV_GAIN_ONE;
    ULONG sampleMean;
    ULONG normalizedMean;
    ULONG liftQ10;
    ULONG targetLiftQ10;
    ULONG black;
    ULONG white;
    ULONGLONG scaleR;
    ULONGLONG scaleG;
    ULONGLONG scaleB;
    ULONG meanR;
    ULONG meanG;
    ULONG meanB;
    ULONG rgQ14;
    ULONG bgQ14;
    ULONG bestReference = 0;
    ULONGLONG bestDistance = (ULONGLONG)-1;
    ULONG reference;
    ULONG targetCcmPositionQ10;
    ULONG ccmPositionQ10;
    ULONG lowerMatrix;
    ULONG matrixWeightQ10;
    LONG matrix[9];
    ULONG x;
    ULONG y;
    ULONG denoiseDivisor = 0;
    ULONG denoiseSpatialAccepted = 0;
    ULONG denoiseTemporalAccepted = 0;
    LARGE_INTEGER stageFrequency;
    LARGE_INTEGER stageStarted;
    LARGE_INTEGER stageStopped;
    ULONGLONG stageTicks;
    ULONGLONG stageUs;

    stageStarted = KeQueryPerformanceCounter(&stageFrequency);
    AvInitializeRearFactoryLsc(Device);
    RtlZeroMemory(histogram, sizeof(histogram));
    RtlZeroMemory(androidHistogram, sizeof(androidHistogram));
    AvRearBuildAndroidDeltaLut(Device, androidDeltaLut);
    if (InterlockedCompareExchange(&Device->PublishedReal, 0, 0) != 0) {
        ULONG p95 = (ULONG)InterlockedCompareExchange(
            &Device->AndroidLtmP95, 0, 0);

        if (p95 <= AV_REAR_DENOISE_FULL_P95) {
            denoiseDivisor = 2UL;
            InterlockedExchange(&Device->DenoiseStrengthQ10, 1024);
        } else if (p95 < AV_REAR_DENOISE_OFF_P95) {
            denoiseDivisor = 4UL;
            InterlockedExchange(&Device->DenoiseStrengthQ10, 512);
        } else {
            /* Match Android ANR10's always-on mild bright-scene chroma pass. */
            denoiseDivisor = 4UL;
            InterlockedExchange(&Device->DenoiseStrengthQ10, 512);
        }
    }

    for (y = 0; y + 1UL < RawHeight; y += 8UL) {
        for (x = 0; x + 1UL < RawWidth; x += 8UL) {
            ULONG g0 = Raw[y * RawWidth + x];
            ULONG r = Raw[y * RawWidth + x + 1UL];
            ULONG b = Raw[(y + 1UL) * RawWidth + x];
            ULONG g1 = Raw[(y + 1UL) * RawWidth + x + 1UL];

            if (g0 >= AV_RAW_LUT_SIZE) g0 = AV_RAW_LUT_SIZE - 1UL;
            if (r >= AV_RAW_LUT_SIZE) r = AV_RAW_LUT_SIZE - 1UL;
            if (b >= AV_RAW_LUT_SIZE) b = AV_RAW_LUT_SIZE - 1UL;
            if (g1 >= AV_RAW_LUT_SIZE) g1 = AV_RAW_LUT_SIZE - 1UL;
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
            histogram[g0 >> 2] += 1;
            histogram[r >> 2] += 1;
            histogram[b >> 2] += 1;
            histogram[g1 >> 2] += 1;
            countG += 2;
            countR += 1;
            countB += 1;
        }
    }
    InterlockedExchange(&Device->RawMinimum, (LONG)rawMinimum);
    InterlockedExchange(&Device->RawMaximum, (LONG)rawMaximum);
    sampleMean = (ULONG)((sumR + sumG + sumB) /
        (countR + countG + countB));
    InterlockedExchange(&Device->RawMean, (LONG)sampleMean);
    InterlockedExchange(&Device->RawNonZero, (LONG)rawNonZero);

    sampleTotal = (ULONG)(countR + countG + countB);
    lowTarget = sampleTotal / 200UL;
    if (lowTarget == 0UL) {
        lowTarget = 1UL;
    }
    highTarget = (sampleTotal * 995UL + 999UL) / 1000UL;
    cumulative = 0;
    for (x = 0; x < RTL_NUMBER_OF(histogram); ++x) {
        cumulative += histogram[x];
        if (!percentileLowFound && cumulative >= lowTarget) {
            percentileLow = x;
            percentileLowFound = TRUE;
        }
        if (cumulative >= highTarget) {
            percentileHigh = x;
            break;
        }
    }
    black = percentileLow * 4UL;
    black = black > 12UL ? black - 12UL : 0UL;
    /*
     * OV13B10's calibrated optical black is close to 64 in the 10-bit
     * stream. A dark scene can put the 0.5 percentile well above that;
     * treating scene shadows as sensor black crushed the lower quarter of
     * the preview. Keep a small margin without discarding real detail.
     */
    if (black > 80UL) {
        black = 80UL;
    }
    white = percentileHigh * 4UL + 19UL;
    if (white > 1023UL) {
        white = 1023UL;
    }
    /* Keep bright-scene exposure visible instead of stretching it away. */
    if (sampleMean > 220UL) {
        ULONG minimumWhite = 640UL;
        ULONG boundedMean = sampleMean > 500UL ? 500UL : sampleMean;

        minimumWhite +=
            ((boundedMean - 220UL) * 260UL + 140UL) / 280UL;
        if (white < minimumWhite) {
            white = minimumWhite;
        }
    }
    if (white <= black + 32UL) {
        white = black + 32UL;
        if (white > 1023UL) {
            black = 991UL;
            white = 1023UL;
        }
    }
    InterlockedExchange(
        &Device->PercentileBlack, (LONG)(percentileLow * 4UL));
    InterlockedExchange(
        &Device->PercentileWhite, (LONG)(percentileHigh * 4UL + 3UL));
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
    /*
     * Small module-specific daylight calibration.  Gray-world alone leaves
     * neutral surfaces slightly green/cyan on OV13B10; retain the adaptive
     * gains and add only the measured red/blue trim.
     */
    gainR = (gainR * 118UL + 50UL) / 100UL;
    gainB = (gainB * 106UL + 50UL) / 100UL;
    if (gainR > 4096UL) gainR = 4096UL;
    if (gainB > 4096UL) gainB = 4096UL;

    if (InterlockedCompareExchange(
            &Device->ToneInitialized, 1, 0) == 0) {
        InterlockedExchange(&Device->StableBlack, (LONG)black);
        InterlockedExchange(&Device->StableWhite, (LONG)white);
        InterlockedExchange(&Device->StableGainR, (LONG)gainR);
        InterlockedExchange(&Device->StableGainB, (LONG)gainB);
    } else {
        ULONG previousBlack = (ULONG)InterlockedCompareExchange(
            &Device->StableBlack, 0, 0);
        ULONG previousWhite = (ULONG)InterlockedCompareExchange(
            &Device->StableWhite, 0, 0);
        ULONG previousGainR = (ULONG)InterlockedCompareExchange(
            &Device->StableGainR, 0, 0);
        ULONG previousGainB = (ULONG)InterlockedCompareExchange(
            &Device->StableGainB, 0, 0);

        black = (previousBlack * 7UL + black + 4UL) / 8UL;
        white = (previousWhite * 7UL + white + 4UL) / 8UL;
        gainR = (previousGainR * 7UL + gainR + 4UL) / 8UL;
        gainB = (previousGainB * 7UL + gainB + 4UL) / 8UL;
        if (white <= black + 8UL) {
            white = black + 8UL;
        }
        InterlockedExchange(&Device->StableBlack, (LONG)black);
        InterlockedExchange(&Device->StableWhite, (LONG)white);
        InterlockedExchange(&Device->StableGainR, (LONG)gainR);
        InterlockedExchange(&Device->StableGainB, (LONG)gainB);
    }

    normalizedMean = sampleMean > black ?
        (ULONG)(((ULONGLONG)(sampleMean - black) * 255ULL) /
            (ULONGLONG)(white - black)) :
        0UL;
    /*
     * Interpolate shadow lift continuously and limit its movement to 1/64
     * of a pass per physical frame.  Bright raw scenes must not receive the
     * unconditional full lift used by AV83: that curve raised a normalized
     * mean near 210 again and washed detail out of sunlit white surfaces.
     * Android's GTM keeps sunlit windows controlled while lifting the room
     * behind them. Use a stronger shadow-only curve rather than increasing
     * sensor exposure, preserving highlight headroom.
     */
    if (normalizedMean <= 48UL) {
        targetLiftQ10 = 2304UL;
    } else if (normalizedMean >= 144UL) {
        targetLiftQ10 = 256UL;
    } else {
        targetLiftQ10 = 256UL +
            ((144UL - normalizedMean) * 2048UL + 48UL) / 96UL;
    }
    liftQ10 = (ULONG)InterlockedCompareExchange(
        &Device->StableLiftQ10, 0, 0);
    if (liftQ10 == 0UL) {
        liftQ10 = targetLiftQ10;
    } else if (targetLiftQ10 > liftQ10 + 16UL) {
        liftQ10 += 16UL;
    } else if (liftQ10 > targetLiftQ10 + 16UL) {
        liftQ10 -= 16UL;
    } else {
        liftQ10 = targetLiftQ10;
    }
    InterlockedExchange(&Device->StableLiftQ10, (LONG)liftQ10);
    InterlockedExchange(
        &Device->ToneLiftPasses, (LONG)((liftQ10 + 512UL) >> 10));
    InterlockedExchange(&Device->HighlightCompression, 1);
    scaleG = (255ULL << 16) / (ULONGLONG)(white - black);
    scaleR = (scaleG * gainR) / AV_GAIN_ONE;
    scaleB = (scaleG * gainB) / AV_GAIN_ONE;
    meanR = countR != 0 ? (ULONG)(sumR / countR) : 0UL;
    meanG = countG != 0 ? (ULONG)(sumG / countG) : 0UL;
    meanB = countB != 0 ? (ULONG)(sumB / countB) : 0UL;
    if (meanG > AV_FACTORY_BLACK_LEVEL &&
        meanR > AV_FACTORY_BLACK_LEVEL &&
        meanB > AV_FACTORY_BLACK_LEVEL) {
        rgQ14 =
            ((meanR - AV_FACTORY_BLACK_LEVEL) << 14) /
            (meanG - AV_FACTORY_BLACK_LEVEL);
        bgQ14 =
            ((meanB - AV_FACTORY_BLACK_LEVEL) << 14) /
            (meanG - AV_FACTORY_BLACK_LEVEL);
    } else {
        rgQ14 = AvFactoryRearAwbRefQ14[2][0];
        bgQ14 = AvFactoryRearAwbRefQ14[2][1];
    }
    InterlockedExchange(&Device->AwbRgQ14, (LONG)rgQ14);
    InterlockedExchange(&Device->AwbBgQ14, (LONG)bgQ14);
    for (reference = 0;
         reference < AV_FACTORY_AWB_REFS;
         ++reference) {
        LONGLONG deltaR =
            (LONGLONG)rgQ14 -
            AvFactoryRearAwbRefQ14[reference][0];
        LONGLONG deltaB =
            (LONGLONG)bgQ14 -
            AvFactoryRearAwbRefQ14[reference][1];
        ULONGLONG distance =
            (ULONGLONG)(deltaR * deltaR) +
            (ULONGLONG)(deltaB * deltaB);

        if (distance < bestDistance) {
            bestDistance = distance;
            bestReference = reference;
        }
    }
    gainR = AvFactoryRearMwbGainQ10[bestReference][0];
    gainB = AvFactoryRearMwbGainQ10[bestReference][1];
    /* Same-scene Android neutral-axis calibration (rear OV13B10). */
    gainR = (gainR * 953UL + 500UL) / 1000UL;
    gainB = (gainB * 1067UL + 500UL) / 1000UL;
    if (gainR < 768UL) gainR = 768UL;
    if (gainR > 4096UL) gainR = 4096UL;
    if (gainB < 768UL) gainB = 768UL;
    if (gainB > 4096UL) gainB = 4096UL;
    {
        ULONG previousR = (ULONG)InterlockedCompareExchange(
            &Device->StableFactoryGainR, 0, 0);
        ULONG previousB = (ULONG)InterlockedCompareExchange(
            &Device->StableFactoryGainB, 0, 0);

        if (previousR != 0UL) {
            gainR = (previousR * 7UL + gainR + 4UL) / 8UL;
            gainB = (previousB * 7UL + gainB + 4UL) / 8UL;
        }
        InterlockedExchange(
            &Device->StableFactoryGainR, (LONG)gainR);
        InterlockedExchange(
            &Device->StableFactoryGainB, (LONG)gainB);
    }
    targetCcmPositionQ10 =
        AvRearCcmPositionForReference(bestReference);
    ccmPositionQ10 = (ULONG)InterlockedCompareExchange(
        &Device->StableCcmPosQ10, 0, 0);
    if (ccmPositionQ10 == 0UL) {
        ccmPositionQ10 = targetCcmPositionQ10;
    } else {
        ccmPositionQ10 =
            (ccmPositionQ10 * 7UL +
             targetCcmPositionQ10 + 4UL) / 8UL;
    }
    InterlockedExchange(
        &Device->StableCcmPosQ10, (LONG)ccmPositionQ10);
    lowerMatrix = ccmPositionQ10 >> 10;
    matrixWeightQ10 = ccmPositionQ10 & 1023UL;
    if (lowerMatrix >= AV_FACTORY_CCM_COUNT - 1UL) {
        lowerMatrix = AV_FACTORY_CCM_COUNT - 2UL;
        matrixWeightQ10 = 1024UL;
    }
    for (x = 0; x < RTL_NUMBER_OF(matrix); ++x) {
        matrix[x] = (LONG)(
            ((LONGLONG)AvFactoryRearCcmQ12[lowerMatrix][x] *
                 (1024UL - matrixWeightQ10) +
             (LONGLONG)AvFactoryRearCcmQ12[lowerMatrix + 1UL][x] *
                 matrixWeightQ10) >> 10);
    }
    black = AV_FACTORY_BLACK_LEVEL;
    white = 1023UL;
    InterlockedExchange(
        &Device->PercentileBlack, AV_FACTORY_BLACK_LEVEL);
    InterlockedExchange(&Device->PercentileWhite, 1023);
    InterlockedExchange(&Device->StableBlack, AV_FACTORY_BLACK_LEVEL);
    InterlockedExchange(&Device->StableWhite, 1023);
    InterlockedExchange(&Device->StableGainR, (LONG)gainR);
    InterlockedExchange(&Device->StableGainB, (LONG)gainB);
    InterlockedExchange(&Device->StableLiftQ10, 0);
    InterlockedExchange(&Device->ToneLiftPasses, 0);
    InterlockedExchange(&Device->HighlightCompression, 0);
    InterlockedExchange(
        &Device->StableAwbRefQ10, (LONG)(bestReference << 10));
    for (x = 0; x < AV_COLOR_WIDTH; ++x) {
        Device->SourceXMap[x] =
            RawWidth == NABU_REAR_PREVIEW_RAW_WIDTH ?
            (USHORT)(x * 2UL) :
            (USHORT)(((ULONGLONG)x * (RawWidth - 2UL)) /
                AV_COLOR_WIDTH);
        Device->SourceXMap[x] &= (USHORT)~1U;
    }
    for (y = 0; y < AV_COLOR_HEIGHT; ++y) {
        Device->SourceYMap[y] =
            RawHeight == NABU_REAR_PREVIEW_RAW_HEIGHT ?
            (USHORT)(y * 2UL) :
            (USHORT)(((ULONGLONG)y * (RawHeight - 2UL)) /
                AV_COLOR_HEIGHT);
        Device->SourceYMap[y] &= (USHORT)~1U;
    }
    stageStopped = KeQueryPerformanceCounter(NULL);
    stageTicks = (ULONGLONG)(
        stageStopped.QuadPart - stageStarted.QuadPart);
    stageUs = stageFrequency.QuadPart > 0 ?
        (stageTicks * 1000000ULL) /
            (ULONGLONG)stageFrequency.QuadPart : 0ULL;
    InterlockedExchange(
        &Device->AnalysisDurationUs,
        (LONG)(stageUs > MAXLONG ? MAXLONG : stageUs));
    stageStarted = KeQueryPerformanceCounter(NULL);
    for (y = 0; y < AV_COLOR_HEIGHT; ++y) {
        ULONG sourceY = Device->SourceYMap[y];
        const USHORT *row0 = Raw + sourceY * RawWidth;
        const USHORT *row1 = row0 + RawWidth;
        PULONG destination =
            Device->NoiseBuffer +
            ((AV_COLOR_HEIGHT - 1UL - y) * AV_COLOR_WIDTH);

        for (x = 0; x < AV_COLOR_WIDTH; ++x) {
            ULONG basePixel;
            ULONG sourceX = Device->SourceXMap[x];
            USHORT g = AvAverage2(
                row0[sourceX], row1[sourceX + 1UL]);
            ULONG redIndex;
            ULONG greenIndex = g < AV_RAW_LUT_SIZE ?
                (ULONG)g : AV_RAW_LUT_SIZE - 1UL;
            ULONG rawRed = row0[sourceX + 1UL];
            ULONG rawBlue = row1[sourceX];
            ULONG blueIndex;

            /* Interpolate R/B to the centre of the GRBG Bayer cell. */
            if (sourceX >= 2UL && sourceY >= 2UL &&
                sourceX + 2UL < RawWidth &&
                sourceY + 2UL < RawHeight) {
                const USHORT *rowDownR = Raw +
                    (sourceY + 2UL) * RawWidth;
                const USHORT *rowUpB = Raw +
                    (sourceY - 1UL) * RawWidth;
                rawRed = (rawRed + row0[sourceX - 1UL] +
                    rowDownR[sourceX + 1UL] +
                    rowDownR[sourceX - 1UL] + 2UL) >> 2;
                rawBlue = (rawBlue + row1[sourceX + 2UL] +
                    rowUpB[sourceX] + rowUpB[sourceX + 2UL] + 2UL) >> 2;
            }

            /* Android ANR10 preview: bilateral chroma size 2, Y edge 20. */
            if (denoiseDivisor != 0UL &&
                sourceX + 3UL < RawWidth) {
                ULONG nextG = AvAverage2(
                    row0[sourceX + 2UL], row1[sourceX + 3UL]);
                ULONG greenDelta = (ULONG)g > nextG ?
                    (ULONG)g - nextG : nextG - (ULONG)g;

                if (greenDelta <= AV_DENOISE_SPATIAL_EDGE_RAW8 * 4UL) {
                    ULONG nextRed = row0[sourceX + 3UL];
                    ULONG nextBlue = row1[sourceX + 2UL];

                    if (denoiseDivisor == 2UL) {
                        rawRed = (rawRed + nextRed + 1UL) >> 1;
                        rawBlue = (rawBlue + nextBlue + 1UL) >> 1;
                    } else {
                        rawRed = (rawRed * 3UL + nextRed + 2UL) >> 2;
                        rawBlue = (rawBlue * 3UL + nextBlue + 2UL) >> 2;
                    }
                    denoiseSpatialAccepted += 1UL;
                }
            }
            redIndex = rawRed < AV_RAW_LUT_SIZE ?
                rawRed : AV_RAW_LUT_SIZE - 1UL;
            blueIndex = rawBlue < AV_RAW_LUT_SIZE ?
                rawBlue : AV_RAW_LUT_SIZE - 1UL;
            basePixel = AvPackRearFactoryPixel(
                Device,
                redIndex,
                greenIndex,
                blueIndex,
                y * AV_COLOR_WIDTH + x,
                gainR,
                gainB,
                matrix);
            if ((x & 63UL) == 0UL) {
                androidHistogram[AvRearPixelLuma(basePixel)] += 1UL;
                androidSampleCount += 1UL;
            }
            basePixel = AvRearApplyAndroidTonePixel(
                basePixel, androidDeltaLut);
            basePixel = AvRearApplyFastTemporalChroma(
                Device,
                basePixel,
                ((AV_COLOR_HEIGHT - 1UL - y) * 2UL + 1UL) *
                    AV_OUTPUT_WIDTH + (x * 2UL + 1UL),
                denoiseDivisor,
                &denoiseTemporalAccepted);
            destination[x] = basePixel;
        }
    }
    AvRearUpdateAndroidLtm(
        Device, androidHistogram, androidSampleCount);
    stageStopped = KeQueryPerformanceCounter(NULL);
    stageTicks = (ULONGLONG)(
        stageStopped.QuadPart - stageStarted.QuadPart);
    stageUs = stageFrequency.QuadPart > 0 ?
        (stageTicks * 1000000ULL) /
            (ULONGLONG)stageFrequency.QuadPart : 0ULL;
    InterlockedExchange(
        &Device->ColorDurationUs,
        (LONG)(stageUs > MAXLONG ? MAXLONG : stageUs));
    stageStarted = KeQueryPerformanceCounter(NULL);
    {
        ULONG p95 = (ULONG)InterlockedCompareExchange(
            &Device->AndroidLtmP95, 0, 0);
        ULONG noiseQ10;

        if (p95 <= AV_REAR_DENOISE_FULL_P95) {
            noiseQ10 = 1024UL;
        } else if (p95 >= AV_REAR_DENOISE_OFF_P95) {
            noiseQ10 = 0UL;
        } else {
            noiseQ10 = ((AV_REAR_DENOISE_OFF_P95 - p95) * 1024UL + 32UL) /
                (AV_REAR_DENOISE_OFF_P95 - AV_REAR_DENOISE_FULL_P95);
        }
        AvRearRefineLuma(Device, noiseQ10);
    }
    stageStopped = KeQueryPerformanceCounter(NULL);
    stageTicks = (ULONGLONG)(
        stageStopped.QuadPart - stageStarted.QuadPart);
    stageUs = stageFrequency.QuadPart > 0 ?
        (stageTicks * 1000000ULL) /
            (ULONGLONG)stageFrequency.QuadPart : 0ULL;
    InterlockedExchange(
        &Device->RefineDurationUs,
        (LONG)(stageUs > MAXLONG ? MAXLONG : stageUs));
    stageStarted = KeQueryPerformanceCounter(NULL);
    AvRearUpscaleWithBayerDetail(
        Device,
        Raw,
        RawWidth,
        RawHeight,
        Device->RefineBuffer != NULL ?
            Device->RefineBuffer : Device->NoiseBuffer,
        Device->CaptureBuffer);
    stageStopped = KeQueryPerformanceCounter(NULL);
    stageTicks = (ULONGLONG)(
        stageStopped.QuadPart - stageStarted.QuadPart);
    stageUs = stageFrequency.QuadPart > 0 ?
        (stageTicks * 1000000ULL) /
            (ULONGLONG)stageFrequency.QuadPart : 0ULL;
    InterlockedExchange(
        &Device->UpscaleDurationUs,
        (LONG)(stageUs > MAXLONG ? MAXLONG : stageUs));
    InterlockedExchange(
        &Device->DenoiseTemporalAccepted,
        (LONG)denoiseTemporalAccepted);
    InterlockedExchange(&Device->ConversionCompleted, 1);
}

static VOID
AvConvertRearRaw(
    _In_opt_ PVOID ConsumerContext,
    _In_reads_(Width * Height) const USHORT *Raw,
    _In_ ULONG Width,
    _In_ ULONG Height)
{
    PAV_DEVICE_CONTEXT device =
        (PAV_DEVICE_CONTEXT)ConsumerContext;
    LARGE_INTEGER frequency;
    LARGE_INTEGER started;
    LARGE_INTEGER stopped;
    ULONGLONG elapsedTicks;
    ULONGLONG elapsedUs;
    LONG previousMaximum;

    if (device == NULL || Raw == NULL ||
        !((Width == NABU_REAR_WIDTH && Height == NABU_REAR_HEIGHT) ||
          (Width == NABU_REAR_PREVIEW_RAW_WIDTH &&
           Height == NABU_REAR_PREVIEW_RAW_HEIGHT))) {
        return;
    }
    started = KeQueryPerformanceCounter(&frequency);
    AvBayerGrbgToRgb32Scaled(
        device,
        Raw,
        Width,
        Height);
    stopped = KeQueryPerformanceCounter(NULL);
    elapsedTicks = (ULONGLONG)(stopped.QuadPart - started.QuadPart);
    elapsedUs = frequency.QuadPart > 0 ?
        (elapsedTicks * 1000000ULL) / (ULONGLONG)frequency.QuadPart :
        0;
    if (elapsedUs > MAXLONG) {
        elapsedUs = MAXLONG;
    }
    InterlockedExchange(
        &device->LastConversionDurationUs, (LONG)elapsedUs);
    previousMaximum = InterlockedCompareExchange(
        &device->MaximumConversionDurationUs, 0, 0);
    while ((LONG)elapsedUs > previousMaximum) {
        LONG observed = InterlockedCompareExchange(
            &device->MaximumConversionDurationUs,
            (LONG)elapsedUs,
            previousMaximum);
        if (observed == previousMaximum) {
            break;
        }
        previousMaximum = observed;
    }
}

static ULONG
AvRearRawSignature(
    _In_reads_(PixelCount) const USHORT *Raw,
    _In_ ULONG PixelCount)
{
    ULONG signature = 2166136261UL;
    ULONG i;

    for (i = 0; i < PixelCount; i += 257UL) {
        signature ^= Raw[i];
        signature *= 16777619UL;
    }
    signature ^= Raw[PixelCount - 1UL];
    signature *= 16777619UL;
    return signature;
}

static BOOLEAN AvGpuConsumeRaw(PAV_DEVICE_CONTEXT,const USHORT*,ULONG,ULONG);

static VOID
AvConsumeRearRaw(
    _In_opt_ PVOID ConsumerContext,
    _In_reads_(Width * Height) const USHORT *Raw,
    _In_ ULONG Width,
    _In_ ULONG Height)
{
    PAV_DEVICE_CONTEXT device =
        (PAV_DEVICE_CONTEXT)ConsumerContext;
    ULONG pixelCount;
    ULONG signature;
    BOOLEAN enqueueConversion = FALSE;

    if (device == NULL || Raw == NULL || device->RawBuffer == NULL ||
        device->PendingRawBuffer == NULL ||
        (device->ConversionWorkItem == NULL &&
         device->ConversionThreadHandle == NULL) ||
        InterlockedCompareExchange(&device->Removing, 0, 0) != 0 ||
        !((Width == NABU_REAR_WIDTH && Height == NABU_REAR_HEIGHT) ||
          (Width == NABU_REAR_PREVIEW_RAW_WIDTH &&
           Height == NABU_REAR_PREVIEW_RAW_HEIGHT))) {
        return;
    }
    if(AvGpuConsumeRaw(device,Raw,Width,Height))return;
    pixelCount = Width * Height;
    signature = AvRearRawSignature(Raw, pixelCount);
    ExAcquireFastMutex(&device->RawQueueLock);
    if ((InterlockedCompareExchange(
             &device->RawSignature, 0, 0) != 0 &&
         (ULONG)InterlockedCompareExchange(
             &device->RawSignature, 0, 0) == signature) ||
        (InterlockedCompareExchange(
             &device->RawPendingValid, 0, 0) != 0 &&
         (ULONG)InterlockedCompareExchange(
             &device->RawPendingSignature, 0, 0) == signature)) {
        InterlockedIncrement(&device->RawDuplicateCount);
        ExReleaseFastMutex(&device->RawQueueLock);
        return;
    }
    if (InterlockedCompareExchange(
            &device->RawConversionBusy, 0, 0) != 0) {
        if (InterlockedCompareExchange(
                &device->RawPendingValid, 0, 0) != 0) {
            InterlockedIncrement(&device->RawPendingOverwriteCount);
            InterlockedIncrement(&device->RawDroppedCount);
        }
        RtlCopyMemory(
            device->PendingRawBuffer,
            Raw,
            (SIZE_T)pixelCount * sizeof(USHORT));
        InterlockedExchange(&device->RawPendingWidth, (LONG)Width);
        InterlockedExchange(&device->RawPendingHeight, (LONG)Height);
        InterlockedExchange(
            &device->RawPendingSignature, (LONG)signature);
        KeMemoryBarrier();
        InterlockedExchange(&device->RawPendingValid, 1);
        InterlockedIncrement(&device->RawPendingQueuedCount);
    } else {
        InterlockedExchange(&device->RawConversionBusy, 1);
        RtlCopyMemory(
            device->RawBuffer,
            Raw,
            (SIZE_T)pixelCount * sizeof(USHORT));
        InterlockedExchange(&device->RawWidth, (LONG)Width);
        InterlockedExchange(&device->RawHeight, (LONG)Height);
        InterlockedExchange(&device->RawSignature, (LONG)signature);
        KeMemoryBarrier();
        enqueueConversion = TRUE;
    }
    InterlockedExchange(&device->RawCopyAccepted, 1);
    ExReleaseFastMutex(&device->RawQueueLock);
    if (enqueueConversion) {
        if (device->ConversionThreadHandle != NULL &&
            InterlockedCompareExchange(
                &device->ConversionThreadRunning, 0, 0) != 0) {
            InterlockedIncrement(&device->ConversionThreadSignalCount);
            KeSetEvent(
                &device->ConversionWorkEvent, IO_NO_INCREMENT, FALSE);
        } else {
            WdfWorkItemEnqueue(device->ConversionWorkItem);
        }
    }
}

static VOID
AvProcessRearConversionQueue(_Inout_ PAV_DEVICE_CONTEXT device)
{
    ULONG width;
    ULONG height;
    BOOLEAN processPending;

    if (device == NULL || device->RawBuffer == NULL ||
        InterlockedCompareExchange(&device->Removing, 0, 0) != 0) {
        if (device != NULL) {
            InterlockedExchange(&device->RawConversionBusy, 0);
        }
        return;
    }

    do {
        width = (ULONG)InterlockedCompareExchange(
            &device->RawWidth, 0, 0);
        height = (ULONG)InterlockedCompareExchange(
            &device->RawHeight, 0, 0);
        InterlockedExchange(&device->ConversionCompleted, 0);
        if (AvGpuWaitRgbReaders(device)) {
            AvConvertRearRaw(device, device->RawBuffer, width, height);
        }
        if (InterlockedCompareExchange(
                &device->ConversionCompleted, 0, 0) != 0 &&
            InterlockedCompareExchange(&device->RawNonZero, 0, 0) >= 100 &&
            InterlockedCompareExchange(&device->RawMaximum, 0, 0) >
                InterlockedCompareExchange(&device->RawMinimum, 0, 0)) {
            InterlockedExchange(
                &device->PublishedFirstPixel,
                (LONG)device->CaptureBuffer[0]);
            InterlockedExchange(
                &device->PublishedCenterPixel,
                (LONG)device->CaptureBuffer[
                    (AV_OUTPUT_HEIGHT / 2UL) * AV_OUTPUT_WIDTH +
                    (AV_OUTPUT_WIDTH / 2UL)]);
            AvGpuPublishRgb(device);
        }
        KeMemoryBarrier();

        processPending = FALSE;
        ExAcquireFastMutex(&device->RawQueueLock);
        if (InterlockedCompareExchange(&device->Removing, 0, 0) == 0 &&
            InterlockedCompareExchange(
                &device->RawPendingValid, 0, 0) != 0) {
            PUSHORT oldRaw = device->RawBuffer;

            device->RawBuffer = device->PendingRawBuffer;
            device->PendingRawBuffer = oldRaw;
            InterlockedExchange(
                &device->RawWidth,
                InterlockedCompareExchange(
                    &device->RawPendingWidth, 0, 0));
            InterlockedExchange(
                &device->RawHeight,
                InterlockedCompareExchange(
                    &device->RawPendingHeight, 0, 0));
            InterlockedExchange(
                &device->RawSignature,
                InterlockedCompareExchange(
                    &device->RawPendingSignature, 0, 0));
            InterlockedExchange(&device->RawPendingValid, 0);
            InterlockedIncrement(&device->RawPendingDequeuedCount);
            processPending = TRUE;
        } else {
            InterlockedExchange(&device->RawConversionBusy, 0);
        }
        ExReleaseFastMutex(&device->RawQueueLock);
    } while (processPending);
}

VOID
AvRearConversionWorkItem(_In_ WDFWORKITEM WorkItem)
{
    PAV_WORK_CONTEXT workContext = AvGetWorkContext(WorkItem);

    if (workContext == NULL || workContext->DeviceContext == NULL) {
        return;
    }
    AvProcessRearConversionQueue(
        (PAV_DEVICE_CONTEXT)workContext->DeviceContext);
}

static VOID
AvRearConversionThread(_In_ PVOID StartContext)
{
    PAV_DEVICE_CONTEXT device =
        (PAV_DEVICE_CONTEXT)StartContext;

    (VOID)KeSetPriorityThread(
        KeGetCurrentThread(), AV_CONVERSION_THREAD_PRIORITY);
    InterlockedExchange(
        &device->ConversionThreadPriority,
        AV_CONVERSION_THREAD_PRIORITY);
    while (InterlockedCompareExchange(
               &device->ConversionThreadStopRequested, 0, 0) == 0) {
        NTSTATUS waitStatus = KeWaitForSingleObject(
            &device->ConversionWorkEvent,
            Executive,
            KernelMode,
            FALSE,
            NULL);

        if (!NT_SUCCESS(waitStatus) ||
            InterlockedCompareExchange(
                &device->ConversionThreadStopRequested, 0, 0) != 0) {
            break;
        }
        InterlockedIncrement(&device->ConversionThreadWakeCount);
        AvProcessRearConversionQueue(device);
    }
    InterlockedExchange(&device->ConversionThreadRunning, 0);
    KeSetEvent(&device->ConversionExitEvent, IO_NO_INCREMENT, FALSE);
    PsTerminateSystemThread(STATUS_SUCCESS);
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

#define CODEX_GPU_PATTERN 1
#define CODEX_GPU_ROTATE 0
#include "..\GpuBridgeImpl.h"

static const GUID AvFilterReferenceGuid =
    { 0x917c6d16, 0xec9a, 0x4bb1,
      { 0xa1, 0xe7, 0xec, 0x37, 0x3b, 0x77, 0x01, 0x39 } };
static const GUID AvPinCategoryCapture =
    { 0xfb6c4281, 0x0353, 0x11d1,
      { 0x90, 0x5f, 0x00, 0x00, 0xc0, 0xcc, 0x16, 0xba } };
static const GUID AvPinNameCapture = { STATIC_PINNAME_VIDEO_CAPTURE };
static const GUID AvSubtypeRgb32 = { AV_STATIC_RGB32 };
static const GUID AvSubtypeRgb24 = { AV_STATIC_RGB24 };
static const GUID AvFilterCategories[] = {
    { STATIC_KSCATEGORY_VIDEO_CAMERA },
    { STATIC_KSCATEGORY_VIDEO },
    { STATIC_KSCATEGORY_CAPTURE }
};

static KS_DATARANGE_VIDEO AvVideoFormatRgb32 = {
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

static KS_DATARANGE_VIDEO AvVideoFormatRgb24 = {
    { sizeof(KS_DATARANGE_VIDEO), 0, AV_RGB24_FRAME_BYTES, 0,
      STATICGUIDOF(KSDATAFORMAT_TYPE_VIDEO),
      { AV_STATIC_RGB24 },
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
      (LONGLONG)AV_RGB24_FRAME_BYTES * AV_TARGET_FPS * 8,
      (LONGLONG)AV_RGB24_FRAME_BYTES * AV_TARGET_FPS * 8 },
    { { 0, 0, 0, 0 },
      { 0, 0, 0, 0 },
      (DWORD)(AV_RGB24_FRAME_BYTES * AV_TARGET_FPS * 8), 0,
      (REFERENCE_TIME)AV_FRAME_INTERVAL,
      { sizeof(KS_BITMAPINFOHEADER), (LONG)AV_OUTPUT_WIDTH,
        (LONG)AV_OUTPUT_HEIGHT, 1, 24, KS_BI_RGB,
        AV_RGB24_FRAME_BYTES, 0, 0, 0, 0 } }
};

static PKSDATARANGE AvPinRanges[] = {
    (PKSDATARANGE)&AvVideoFormatRgb32,
    (PKSDATARANGE)&AvVideoFormatRgb24
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
    result->DataFormat.SampleSize =
        IsEqualGUIDAligned(&DescriptorDataRange->SubFormat,
                           &AvSubtypeRgb24) ?
            AV_RGB24_FRAME_BYTES : AV_RGB_FRAME_BYTES;
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
    &AvGpuAutomation,
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

VOID
AvRearBackendIdleTimerCallback(_In_ WDFTIMER Timer)
{
    PAV_IDLE_TIMER_CONTEXT timerContext = AvGetIdleTimerContext(Timer);
    PAV_DEVICE_CONTEXT device = timerContext != NULL ?
        (PAV_DEVICE_CONTEXT)timerContext->DeviceContext : NULL;

    if (device == NULL ||
        InterlockedCompareExchange(&device->Removing, 0, 0) != 0 ||
        InterlockedCompareExchange(&device->ActivePinCount, 0, 0) != 0 ||
        InterlockedCompareExchange(&device->CaptureQueued, 0, 0) != 0 ||
        device->WdfDevice == NULL) {
        return;
    }
    NabuRearBackendRelease(device->WdfDevice);
    AvWriteRearDiagnostics(device);
}

static NTSTATUS
AvDeviceAdd(_In_ PKSDEVICE Device)
{
    WDF_OBJECT_ATTRIBUTES workAttributes;
    WDF_WORKITEM_CONFIG workConfig;
    PAV_WORK_CONTEXT workContext;
    PAV_IDLE_TIMER_CONTEXT idleTimerContext;
    WDF_TIMER_CONFIG idleTimerConfig;
    WDF_OBJECT_ATTRIBUTES idleTimerAttributes;
    PAV_DEVICE_CONTEXT avContext = NULL;
    WDFDEVICE wdfDevice = NULL;
    HANDLE diagnosticKey = NULL;
    HANDLE conversionThread = NULL;
    OBJECT_ATTRIBUTES threadAttributes;
    NTSTATUS threadStatus;
    NTSTATUS status;
    ULONG locationBytes = 0;

    avContext = (PAV_DEVICE_CONTEXT)ExAllocatePool2(
        POOL_FLAG_NON_PAGED, sizeof(*avContext), AV_POOL_TAG);
    if (avContext == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(avContext, sizeof(*avContext));
    avContext->KsDevice = Device;
    KeInitializeEvent(
        &avContext->ConversionWorkEvent, SynchronizationEvent, FALSE);
    KeInitializeEvent(
        &avContext->ConversionExitEvent, NotificationEvent, FALSE);
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
    status = WdfDeviceMiniportCreate(
        g_AvWdfDriver,
        WDF_NO_OBJECT_ATTRIBUTES,
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

    status = NabuRearBackendInitialize(wdfDevice);
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
        NabuRearBackendCleanup(wdfDevice);
        WdfObjectDelete(wdfDevice);
        if (avContext->DiagnosticKey != NULL) {
            ZwClose(avContext->DiagnosticKey);
        }
        ExFreePoolWithTag(avContext, AV_POOL_TAG);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    // Use opaque black while the first optical frame is acquired.  Transparent
    // RGB32 exposed the application's blue surface during cold start.
    {
        ULONG pixel;
        for (pixel = 0; pixel < AV_OUTPUT_WIDTH * AV_OUTPUT_HEIGHT; ++pixel) {
            avContext->RgbBuffer[pixel] = 0xFF000000UL;
        }
    }
    avContext->CaptureBuffer = (PULONG)ExAllocatePool2(
        POOL_FLAG_NON_PAGED, AV_RGB_FRAME_BYTES, AV_POOL_TAG);
    if (avContext->CaptureBuffer == NULL) {
        ExFreePoolWithTag(avContext->RgbBuffer, AV_POOL_TAG);
        avContext->RgbBuffer = NULL;
        NabuRearBackendCleanup(wdfDevice);
        WdfObjectDelete(wdfDevice);
        if (avContext->DiagnosticKey != NULL) {
            ZwClose(avContext->DiagnosticKey);
        }
        ExFreePoolWithTag(avContext, AV_POOL_TAG);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    avContext->NoiseBuffer = (PULONG)ExAllocatePool2(
        POOL_FLAG_NON_PAGED, AV_RGB_FRAME_BYTES, AV_POOL_TAG);
    if (avContext->NoiseBuffer == NULL) {
        ExFreePoolWithTag(avContext->CaptureBuffer, AV_POOL_TAG);
        avContext->CaptureBuffer = NULL;
        ExFreePoolWithTag(avContext->RgbBuffer, AV_POOL_TAG);
        avContext->RgbBuffer = NULL;
        NabuRearBackendCleanup(wdfDevice);
        WdfObjectDelete(wdfDevice);
        if (avContext->DiagnosticKey != NULL) {
            ZwClose(avContext->DiagnosticKey);
        }
        ExFreePoolWithTag(avContext, AV_POOL_TAG);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    avContext->RawBuffer = (PUSHORT)ExAllocatePool2(
        POOL_FLAG_NON_PAGED, NABU_REAR_FRAME_BYTES, AV_POOL_TAG);
    if (avContext->RawBuffer == NULL) {
        ExFreePoolWithTag(avContext->NoiseBuffer, AV_POOL_TAG);
        avContext->NoiseBuffer = NULL;
        ExFreePoolWithTag(avContext->CaptureBuffer, AV_POOL_TAG);
        avContext->CaptureBuffer = NULL;
        ExFreePoolWithTag(avContext->RgbBuffer, AV_POOL_TAG);
        avContext->RgbBuffer = NULL;
        NabuRearBackendCleanup(wdfDevice);
        WdfObjectDelete(wdfDevice);
        if (avContext->DiagnosticKey != NULL) {
            ZwClose(avContext->DiagnosticKey);
        }
        ExFreePoolWithTag(avContext, AV_POOL_TAG);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    avContext->PendingRawBuffer = (PUSHORT)ExAllocatePool2(
        POOL_FLAG_NON_PAGED, NABU_REAR_FRAME_BYTES, AV_POOL_TAG);
    if (avContext->PendingRawBuffer == NULL) {
        ExFreePoolWithTag(avContext->RawBuffer, AV_POOL_TAG);
        avContext->RawBuffer = NULL;
        ExFreePoolWithTag(avContext->NoiseBuffer, AV_POOL_TAG);
        avContext->NoiseBuffer = NULL;
        ExFreePoolWithTag(avContext->CaptureBuffer, AV_POOL_TAG);
        avContext->CaptureBuffer = NULL;
        ExFreePoolWithTag(avContext->RgbBuffer, AV_POOL_TAG);
        avContext->RgbBuffer = NULL;
        NabuRearBackendCleanup(wdfDevice);
        WdfObjectDelete(wdfDevice);
        if (avContext->DiagnosticKey != NULL) {
            ZwClose(avContext->DiagnosticKey);
        }
        ExFreePoolWithTag(avContext, AV_POOL_TAG);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    ExInitializeFastMutex(&avContext->RawQueueLock);
    ExInitializeFastMutex(&avContext->GpuIoLock);
    KeInitializeSpinLock(&avContext->GpuRgbMetadataLock);
    ExInitializeRundownProtection(&avContext->GpuRundown);
    /*
     * The luma refinement is optional by construction: if either buffer is
     * refused the refine pass returns immediately and the 2x output pass reads
     * NoiseBuffer as before, so a memory-tight pad keeps working.
     */
    avContext->RefineBuffer = (PULONG)ExAllocatePool2(
        POOL_FLAG_NON_PAGED, AV_COLOR_FRAME_BYTES, AV_POOL_TAG);
    avContext->LumaHistory = (PUSHORT)ExAllocatePool2(
        POOL_FLAG_NON_PAGED,
        AV_COLOR_CELLS * sizeof(USHORT),
        AV_POOL_TAG);
    if (avContext->LumaHistory != NULL) {
        RtlZeroMemory(
            avContext->LumaHistory,
            AV_COLOR_CELLS * sizeof(USHORT));
    } else {
        ExFreePoolWithTag(avContext->RefineBuffer, AV_POOL_TAG);
        avContext->RefineBuffer = NULL;
    }
    {
        ULONG pixel;
        for (pixel = 0; pixel < AV_OUTPUT_WIDTH * AV_OUTPUT_HEIGHT; ++pixel) {
            avContext->CaptureBuffer[pixel] = 0xFF000000UL;
            avContext->NoiseBuffer[pixel] = 0xFF000000UL;
        }
    }
    WDF_TIMER_CONFIG_INIT(
        &idleTimerConfig, AvRearBackendIdleTimerCallback);
    idleTimerConfig.AutomaticSerialization = FALSE;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(
        &idleTimerAttributes, AV_IDLE_TIMER_CONTEXT);
    idleTimerAttributes.ParentObject = wdfDevice;
    idleTimerAttributes.ExecutionLevel = WdfExecutionLevelPassive;
    status = WdfTimerCreate(
        &idleTimerConfig,
        &idleTimerAttributes,
        &avContext->BackendIdleTimer);
    if (!NT_SUCCESS(status)) {
        ExFreePoolWithTag(avContext->PendingRawBuffer, AV_POOL_TAG);
        avContext->PendingRawBuffer = NULL;
        ExFreePoolWithTag(avContext->RawBuffer, AV_POOL_TAG);
        avContext->RawBuffer = NULL;
        ExFreePoolWithTag(avContext->NoiseBuffer, AV_POOL_TAG);
        avContext->NoiseBuffer = NULL;
        ExFreePoolWithTag(avContext->CaptureBuffer, AV_POOL_TAG);
        avContext->CaptureBuffer = NULL;
        ExFreePoolWithTag(avContext->RgbBuffer, AV_POOL_TAG);
        avContext->RgbBuffer = NULL;
        NabuRearBackendCleanup(wdfDevice);
        WdfObjectDelete(wdfDevice);
        if (avContext->DiagnosticKey != NULL) {
            ZwClose(avContext->DiagnosticKey);
        }
        ExFreePoolWithTag(avContext, AV_POOL_TAG);
        return status;
    }
    idleTimerContext = AvGetIdleTimerContext(avContext->BackendIdleTimer);
    idleTimerContext->DeviceContext = avContext;
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
        idleTimerContext->DeviceContext = NULL;
        WdfObjectDelete(avContext->BackendIdleTimer);
        avContext->BackendIdleTimer = NULL;
        ExFreePoolWithTag(avContext->RefineBuffer, AV_POOL_TAG);
        avContext->RefineBuffer = NULL;
        ExFreePoolWithTag(avContext->LumaHistory, AV_POOL_TAG);
        avContext->LumaHistory = NULL;
        ExFreePoolWithTag(avContext->PendingRawBuffer, AV_POOL_TAG);
        avContext->PendingRawBuffer = NULL;
        ExFreePoolWithTag(avContext->RawBuffer, AV_POOL_TAG);
        avContext->RawBuffer = NULL;
        ExFreePoolWithTag(avContext->NoiseBuffer, AV_POOL_TAG);
        avContext->NoiseBuffer = NULL;
        ExFreePoolWithTag(avContext->CaptureBuffer, AV_POOL_TAG);
        avContext->CaptureBuffer = NULL;
        ExFreePoolWithTag(avContext->RgbBuffer, AV_POOL_TAG);
        avContext->RgbBuffer = NULL;
        NabuRearBackendCleanup(wdfDevice);
        WdfObjectDelete(wdfDevice);
        if (avContext->DiagnosticKey != NULL) {
            ZwClose(avContext->DiagnosticKey);
        }
        ExFreePoolWithTag(avContext, AV_POOL_TAG);
        return status;
    }
    workContext = AvGetWorkContext(avContext->CaptureWorkItem);
    workContext->DeviceContext = avContext;
    WDF_WORKITEM_CONFIG_INIT(&workConfig, AvRearConversionWorkItem);
    workConfig.AutomaticSerialization = FALSE;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(
        &workAttributes, AV_WORK_CONTEXT);
    workAttributes.ParentObject = wdfDevice;
    status = WdfWorkItemCreate(
        &workConfig,
        &workAttributes,
        &avContext->ConversionWorkItem);
    if (!NT_SUCCESS(status)) {
        workContext->DeviceContext = NULL;
        WdfObjectDelete(avContext->CaptureWorkItem);
        avContext->CaptureWorkItem = NULL;
        idleTimerContext->DeviceContext = NULL;
        WdfObjectDelete(avContext->BackendIdleTimer);
        avContext->BackendIdleTimer = NULL;
        ExFreePoolWithTag(avContext->RefineBuffer, AV_POOL_TAG);
        avContext->RefineBuffer = NULL;
        ExFreePoolWithTag(avContext->LumaHistory, AV_POOL_TAG);
        avContext->LumaHistory = NULL;
        ExFreePoolWithTag(avContext->PendingRawBuffer, AV_POOL_TAG);
        avContext->PendingRawBuffer = NULL;
        ExFreePoolWithTag(avContext->RawBuffer, AV_POOL_TAG);
        avContext->RawBuffer = NULL;
        ExFreePoolWithTag(avContext->NoiseBuffer, AV_POOL_TAG);
        avContext->NoiseBuffer = NULL;
        ExFreePoolWithTag(avContext->CaptureBuffer, AV_POOL_TAG);
        avContext->CaptureBuffer = NULL;
        ExFreePoolWithTag(avContext->RgbBuffer, AV_POOL_TAG);
        avContext->RgbBuffer = NULL;
        NabuRearBackendCleanup(wdfDevice);
        WdfObjectDelete(wdfDevice);
        if (avContext->DiagnosticKey != NULL) {
            ZwClose(avContext->DiagnosticKey);
        }
        ExFreePoolWithTag(avContext, AV_POOL_TAG);
        return status;
    }
    workContext = AvGetWorkContext(avContext->ConversionWorkItem);
    workContext->DeviceContext = avContext;
    InitializeObjectAttributes(
        &threadAttributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    InterlockedExchange(&avContext->ConversionThreadStopRequested, 0);
    InterlockedExchange(&avContext->ConversionThreadRunning, 1);
    threadStatus = PsCreateSystemThread(
        &conversionThread,
        THREAD_ALL_ACCESS,
        &threadAttributes,
        NULL,
        NULL,
        AvRearConversionThread,
        avContext);
    if (NT_SUCCESS(threadStatus)) {
        avContext->ConversionThreadHandle = conversionThread;
    } else {
        InterlockedExchange(&avContext->ConversionThreadRunning, 0);
    }

    avContext->WdfDevice = wdfDevice;
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
    HANDLE conversionThread;

    if (context == NULL ||
        InterlockedExchange(&context->Cleaned, 1) != 0) {
        return;
    }
    InterlockedExchange(&context->Removing, 1);
    ExWaitForRundownProtectionRelease(&context->GpuRundown);
    InterlockedExchange(&context->GpuMode,0);
    conversionThread = context->ConversionThreadHandle;
    if (conversionThread != NULL ||
        InterlockedCompareExchange(
            &context->ConversionThreadRunning, 0, 0) != 0) {
        InterlockedExchange(
            &context->ConversionThreadStopRequested, 1);
        KeSetEvent(
            &context->ConversionWorkEvent, IO_NO_INCREMENT, FALSE);
        (VOID)KeWaitForSingleObject(
            &context->ConversionExitEvent,
            Executive,
            KernelMode,
            FALSE,
            NULL);
    }
    context->ConversionThreadHandle = NULL;
    if (conversionThread != NULL) {
        (VOID)ZwClose(conversionThread);
    }
    if (context->BackendIdleTimer != NULL) {
        PAV_IDLE_TIMER_CONTEXT idleTimerContext =
            AvGetIdleTimerContext(context->BackendIdleTimer);

        WdfTimerStop(context->BackendIdleTimer, TRUE);
        idleTimerContext->DeviceContext = NULL;
        WdfObjectDelete(context->BackendIdleTimer);
        context->BackendIdleTimer = NULL;
    }
    if (context->CaptureWorkItem != NULL) {
        PAV_WORK_CONTEXT workContext =
            AvGetWorkContext(context->CaptureWorkItem);

        WdfWorkItemFlush(context->CaptureWorkItem);
        workContext->DeviceContext = NULL;
        WdfObjectDelete(context->CaptureWorkItem);
        context->CaptureWorkItem = NULL;
    }
    if (context->ConversionWorkItem != NULL) {
        PAV_WORK_CONTEXT workContext =
            AvGetWorkContext(context->ConversionWorkItem);

        WdfWorkItemFlush(context->ConversionWorkItem);
        workContext->DeviceContext = NULL;
        WdfObjectDelete(context->ConversionWorkItem);
        context->ConversionWorkItem = NULL;
    }
    if (context->RawBuffer != NULL) {
        ExFreePoolWithTag(context->RawBuffer, AV_POOL_TAG);
        context->RawBuffer = NULL;
    }
    if(context->GpuRawBuffer){ExFreePoolWithTag(context->GpuRawBuffer,AV_POOL_TAG);context->GpuRawBuffer=NULL;}
    if (context->PendingRawBuffer != NULL) {
        ExFreePoolWithTag(context->PendingRawBuffer, AV_POOL_TAG);
        context->PendingRawBuffer = NULL;
    }
    if (context->RgbBuffer != NULL) {
        ExFreePoolWithTag(context->RgbBuffer, AV_POOL_TAG);
        context->RgbBuffer = NULL;
    }
    if (context->CaptureBuffer != NULL) {
        ExFreePoolWithTag(context->CaptureBuffer, AV_POOL_TAG);
        context->CaptureBuffer = NULL;
    }
    if (context->NoiseBuffer != NULL) {
        ExFreePoolWithTag(context->NoiseBuffer, AV_POOL_TAG);
        context->NoiseBuffer = NULL;
    }
    if (context->RefineBuffer != NULL) {
        ExFreePoolWithTag(context->RefineBuffer, AV_POOL_TAG);
        context->RefineBuffer = NULL;
    }
    if (context->LumaHistory != NULL) {
        ExFreePoolWithTag(context->LumaHistory, AV_POOL_TAG);
        context->LumaHistory = NULL;
    }
    if (context->WdfDevice != NULL) {
        NabuRearBackendCleanup(context->WdfDevice);
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
    NTSTATUS status;

    if (Device == NULL || Device->WdfDevice == NULL ||
        KeGetCurrentIrql() != PASSIVE_LEVEL) {
        return STATUS_INVALID_DEVICE_STATE;
    }

    InterlockedExchange(
        &Device->CaptureStatus, (LONG)STATUS_PENDING);
    InterlockedExchange(&Device->RawCopyAccepted, 0);
    status = NabuRearBackendCapture(
        Device->WdfDevice,
        AvConsumeRearRaw,
        Device);
    InterlockedExchange(&Device->CaptureStatus, (LONG)status);
    AvWriteRearDiagnostics(Device);
    return status;
}

VOID
AvRearCaptureWorkItem(_In_ WDFWORKITEM WorkItem)
{
    PAV_WORK_CONTEXT workContext = AvGetWorkContext(WorkItem);
    PAV_DEVICE_CONTEXT device;
    LARGE_INTEGER delay;
    LARGE_INTEGER started;
    LARGE_INTEGER finished;
    LONG attempt;
    LONG durationUs;
    LONG maximumUs;
    NTSTATUS status = STATUS_CANCELLED;

    if (workContext == NULL || workContext->DeviceContext == NULL) {
        return;
    }
    device = (PAV_DEVICE_CONTEXT)workContext->DeviceContext;
    if (InterlockedCompareExchange(
            &device->PublishedGeneration, 0, 0) == 0) {
        delay.QuadPart =
            -((LONGLONG)AV_REAR_INITIAL_DELAY_MS * 10000LL);
        (VOID)KeDelayExecutionThread(
            KernelMode, FALSE, &delay);
    }

    attempt = InterlockedIncrement(&device->CaptureRetryCount);
    InterlockedExchange(&device->CaptureAttempted, 1);
    KeQuerySystemTimePrecise(&started);
    if (InterlockedCompareExchange(
            &device->Removing, 0, 0) == 0 &&
        InterlockedCompareExchange(
            &device->ActivePinCount, 0, 0) != 0) {
        status = AvRunRearCapture(device);
        if (NT_SUCCESS(status)) {
            InterlockedExchange(
                &device->CaptureSuccessAttempt, attempt);
            InterlockedIncrement(&device->CaptureCompletedCount);
        } else {
            InterlockedIncrement(&device->CaptureFailureCount);
        }
    }
    KeQuerySystemTimePrecise(&finished);
    durationUs = (LONG)((finished.QuadPart - started.QuadPart) / 10LL);
    InterlockedExchange(&device->CaptureLastDurationUs, durationUs);
    maximumUs = InterlockedCompareExchange(
        &device->CaptureMaximumDurationUs, 0, 0);
    while (durationUs > maximumUs) {
        LONG observed = InterlockedCompareExchange(
            &device->CaptureMaximumDurationUs,
            durationUs,
            maximumUs);
        if (observed == maximumUs) {
            break;
        }
        maximumUs = observed;
    }

    InterlockedExchange(&device->CaptureStatus, (LONG)status);
    delay.QuadPart =
        -((LONGLONG)AV_REAR_REFRESH_GUARD_MS * 10000LL);
    (VOID)KeDelayExecutionThread(KernelMode, FALSE, &delay);
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
    pinContext->OutputRgb24 =
        Pin->ConnectionFormat != NULL &&
        IsEqualGUIDAligned(&Pin->ConnectionFormat->SubFormat,
                           &AvSubtypeRgb24) ? TRUE : FALSE;

    WDF_TIMER_CONFIG_INIT_PERIODIC(
        &timerConfig, AvTimerCallback, AV_TIMER_PERIOD_MS);
    timerConfig.AutomaticSerialization = FALSE;
    timerConfig.UseHighResolutionTimer = WdfTrue;
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
    if (InterlockedIncrement(&deviceContext->ActivePinCount) == 1) {
        /* Never expose the previous session's final frame on a new open. */
        InterlockedExchange(&deviceContext->PublishedReal, 0);
        InterlockedExchange(&deviceContext->CaptureAttempted, 0);
    }
    if (deviceContext->BackendIdleTimer != NULL) {
        (VOID)WdfTimerStop(deviceContext->BackendIdleTimer, TRUE);
    }
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
        if (InterlockedDecrement(
                &pinContext->Device->ActivePinCount) == 0 &&
            pinContext->Device->BackendIdleTimer != NULL) {
            WdfTimerStart(
                pinContext->Device->BackendIdleTimer,
                WDF_REL_TIMEOUT_IN_MS(
                    AV_REAR_BACKEND_IDLE_HOLD_MS));
        }
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
    BOOLEAN outputRgb24;
    ULONG requiredBytes;
    USHORT requiredBits;

    UNREFERENCED_PARAMETER(OldFormat);
    UNREFERENCED_PARAMETER(OldAttributes);
    UNREFERENCED_PARAMETER(Attributes);

    UNREFERENCED_PARAMETER(DataRange);
    if (Pin == NULL || Pin->ConnectionFormat == NULL ||
        Pin->ConnectionFormat->FormatSize <
            sizeof(KS_DATAFORMAT_VIDEOINFOHEADER) ||
        !IsEqualGUIDAligned(&Pin->ConnectionFormat->MajorFormat,
                            &KSDATAFORMAT_TYPE_VIDEO) ||
        (!IsEqualGUIDAligned(&Pin->ConnectionFormat->SubFormat,
                             &AvSubtypeRgb32) &&
         !IsEqualGUIDAligned(&Pin->ConnectionFormat->SubFormat,
                             &AvSubtypeRgb24)) ||
        !IsEqualGUIDAligned(&Pin->ConnectionFormat->Specifier,
                            &KSDATAFORMAT_SPECIFIER_VIDEOINFO)) {
        return STATUS_NO_MATCH;
    }
    connection =
        (PKS_DATAFORMAT_VIDEOINFOHEADER)Pin->ConnectionFormat;
    outputRgb24 =
        IsEqualGUIDAligned(&Pin->ConnectionFormat->SubFormat,
                           &AvSubtypeRgb24);
    requiredBytes =
        outputRgb24 ? AV_RGB24_FRAME_BYTES : AV_RGB_FRAME_BYTES;
    requiredBits = outputRgb24 ? 24 : 32;
    if (connection->VideoInfoHeader.bmiHeader.biSize !=
            sizeof(KS_BITMAPINFOHEADER) ||
        connection->VideoInfoHeader.bmiHeader.biWidth !=
            (LONG)AV_OUTPUT_WIDTH ||
        connection->VideoInfoHeader.bmiHeader.biHeight !=
            (LONG)AV_OUTPUT_HEIGHT ||
        connection->VideoInfoHeader.bmiHeader.biPlanes != 1 ||
        connection->VideoInfoHeader.bmiHeader.biBitCount != requiredBits ||
        connection->VideoInfoHeader.bmiHeader.biCompression != KS_BI_RGB ||
        connection->VideoInfoHeader.bmiHeader.biSizeImage <
            requiredBytes) {
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
            InterlockedExchange(
                &context->LastDeliveredGeneration,
                InterlockedCompareExchange(
                    &context->Device->PublishedGeneration, 0, 0));
            if (context->Device->CaptureWorkItem != NULL &&
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
    LONG deliveryTick;

    if (pinContext != NULL && pinContext->Device != NULL) {
        InterlockedIncrement(&pinContext->Device->TimerTickCount);
    }
    if (pinContext != NULL &&
        InterlockedCompareExchange(&pinContext->Running, 0, 0) != 0 &&
        InterlockedCompareExchange(&pinContext->FrameReady, 0, 0) != 0) {
        deliveryTick = InterlockedIncrement(&pinContext->DeliveryTick);
        if (deliveryTick >= AV_DELIVERY_TICKS) {
            InterlockedExchange(&pinContext->DeliveryTick, 0);
            KsPinAttemptProcessing(pinContext->Pin, TRUE);
        }
        if (pinContext->Device != NULL &&
            pinContext->Device->CaptureWorkItem != NULL &&
            InterlockedCompareExchange(
                &pinContext->Device->Removing, 0, 0) == 0 &&
            InterlockedCompareExchange(
                &pinContext->Device->CaptureQueued, 1, 0) == 0) {
            WdfWorkItemEnqueue(
                pinContext->Device->CaptureWorkItem);
        }
    }
}

static NTSTATUS
AvPinProcess(_In_ PKSPIN Pin)
{
    PAV_PIN_CONTEXT pinContext = (PAV_PIN_CONTEXT)Pin->Context;
    PAV_DEVICE_CONTEXT device;
    PULONG rgbBuffer;
    AV_GPU_RGB_LEASE lease;
    PKSSTREAM_POINTER streamPointer;
    LARGE_INTEGER performanceFrequency;
    LARGE_INTEGER performanceTime;
    ULONGLONG presentationTime;
    ULONG outputBytes;

    if (pinContext == NULL ||
        InterlockedCompareExchange(&pinContext->Running, 0, 0) == 0 ||
        InterlockedCompareExchange(&pinContext->FrameReady, 0, 0) == 0) {
        return STATUS_PENDING;
    }
    device = pinContext->Device;
    if (device == NULL) {
        return STATUS_PENDING;
    }
    if (InterlockedCompareExchange(&device->PublishedReal, 0, 0) == 0) {
        return STATUS_PENDING;
    }
    if (InterlockedCompareExchange(
            &device->PublishedGeneration, 0, 0) <=
        InterlockedCompareExchange(
            &pinContext->LastDeliveredGeneration, 0, 0)) {
        return STATUS_PENDING;
    }
    outputBytes = pinContext->OutputRgb24 ?
        AV_RGB24_FRAME_BYTES : AV_RGB_FRAME_BYTES;
    InterlockedIncrement(&device->ProcessCallCount);

    streamPointer = KsPinGetLeadingEdgeStreamPointer(
        Pin, KSSTREAM_POINTER_STATE_LOCKED);
    if (streamPointer == NULL) {
        InterlockedIncrement(&device->NoStreamPointerCount);
        return STATUS_PENDING;
    }
    if (streamPointer->StreamHeader == NULL ||
        streamPointer->StreamHeader->Data == NULL ||
        streamPointer->StreamHeader->FrameExtent <
            outputBytes ||
        streamPointer->OffsetOut.Remaining < outputBytes) {
        InterlockedIncrement(&device->BufferTooSmallCount);
        KsStreamPointerUnlock(streamPointer, FALSE);
        return STATUS_BUFFER_TOO_SMALL;
    }

    if (!AvGpuAcquireRgb(device, InterlockedCompareExchange(
            &pinContext->LastDeliveredGeneration, 0, 0), &lease)) {
        KsStreamPointerUnlock(streamPointer, FALSE);
        return STATUS_PENDING;
    }
    rgbBuffer = lease.Pixels;
    if (pinContext->OutputRgb24) {
        PUCHAR destination = (PUCHAR)streamPointer->StreamHeader->Data;
        ULONG pixelIndex;

        for (pixelIndex = 0;
             pixelIndex < AV_OUTPUT_WIDTH * AV_OUTPUT_HEIGHT;
             ++pixelIndex) {
            ULONG pixel = rgbBuffer[pixelIndex];
            *destination++ = (UCHAR)pixel;
            *destination++ = (UCHAR)(pixel >> 8);
            *destination++ = (UCHAR)(pixel >> 16);
        }
    } else {
        RtlCopyMemory(
            streamPointer->StreamHeader->Data,
            rgbBuffer,
            AV_RGB_FRAME_BYTES);
    }
    InterlockedExchange(
        &device->LastDeliveredFirstPixel, (LONG)rgbBuffer[0]);
    InterlockedExchange(
        &device->LastDeliveredCenterPixel,
        (LONG)rgbBuffer[
            (AV_OUTPUT_HEIGHT / 2UL) * AV_OUTPUT_WIDTH +
            (AV_OUTPUT_WIDTH / 2UL)]);
    AvGpuReleaseRgb(device);
    streamPointer->StreamHeader->DataUsed = outputBytes;
    streamPointer->StreamHeader->Duration = AV_FRAME_INTERVAL;
    streamPointer->StreamHeader->OptionsFlags =
        KSSTREAM_HEADER_OPTIONSF_TIMEVALID |
        KSSTREAM_HEADER_OPTIONSF_DURATIONVALID |
        KSSTREAM_HEADER_OPTIONSF_SPLICEPOINT;
    performanceTime =
        KeQueryPerformanceCounter(&performanceFrequency);
    presentationTime = KSCONVERT_PERFORMANCE_TIME(
        performanceFrequency.QuadPart,
        performanceTime);
    streamPointer->StreamHeader->PresentationTime.Time =
        (LONGLONG)presentationTime;
    streamPointer->StreamHeader->PresentationTime.Numerator = 1;
    streamPointer->StreamHeader->PresentationTime.Denominator = 1;
    InterlockedExchange(
        &device->LastDeliveredGeneration,
        lease.Generation);
    InterlockedExchange(
        &device->LastFrameExtent,
        (LONG)streamPointer->StreamHeader->FrameExtent);
    InterlockedExchange(
        &device->LastFrameDataUsed,
        (LONG)streamPointer->StreamHeader->DataUsed);
    InterlockedExchange(
        &device->LastFrameOptions,
        (LONG)streamPointer->StreamHeader->OptionsFlags);
    InterlockedExchange(
        &device->LastOffsetRemaining,
        (LONG)streamPointer->OffsetOut.Remaining);
    InterlockedExchange(
        &device->LastHeaderDataMatchesOffset,
        streamPointer->StreamHeader->Data ==
            streamPointer->OffsetOut.Data ? 1 : 0);
    InterlockedExchange(
        &device->LastPresentationLow, (LONG)presentationTime);
    InterlockedExchange(
        &device->LastPresentationHigh,
        (LONG)(presentationTime >> 32));
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

    InterlockedExchange(
        &pinContext->LastDeliveredGeneration,
        lease.Generation);
    InterlockedIncrement(&device->FrameDeliveredCount);
    KsStreamPointerUnlock(streamPointer, TRUE);
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
