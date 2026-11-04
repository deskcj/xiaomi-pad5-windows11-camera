//
// NabuCamAvStreamFrontVirtual.c
//
// Root-enumerated AVStream frontend.  Hardware ownership and the proven
// Stage70 capture remain in NabuCamHwFront; this miniport only requests a RAW
// frame, converts it, validates it, and publishes it to Windows Camera.
//

#define AVSTREAM_BUILD 1

#include <ntddk.h>
#include <wdf.h>
#include <wdfminiport.h>
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
#include "HwCaptureFront.h"
#include "NabuCamOwnIoctl.h"
#include "AndroidFactoryTuningFront.h"

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
#define AV_TARGET_FPS         30UL
#define AV_TIMER_PERIOD_MS    8UL
#define AV_FRAME_INTERVAL     (10000000ULL / AV_TARGET_FPS)
#define AV_REFRESH_TICKS      4L
#define AV_REFRESH_FAIL_LIMIT 3L
#define AV_REFRESH_RECOVERY_TICKS 30L
#define AV_GAIN_ONE           1024UL
#define AV_RAW_LUT_SIZE       1024UL
#define AV_FRONT_ANDROID_LTM_DARK_END 151UL
#define AV_FRONT_ANDROID_LTM_BRIGHT_START 225UL
/* Android AEC/ADRC convergence speed is 0.15 per frame (154 in Q10). */
#define AV_FRONT_ANDROID_LTM_STEP_Q10 154UL
#define AV_FRONT_DENOISE_FULL_P95 48UL
#define AV_FRONT_DENOISE_OFF_P95 144UL
#define AV_DENOISE_EDGE_THRESHOLD 18UL
#define AV_DENOISE_SPATIAL_EDGE_RAW8 20UL
#define AV_BACKEND_IDLE_HOLD_MS 500UL
#define AV_CONVERSION_THREAD_PRIORITY 12
/*
 * The native Bayer detail reconstruction is the optional part of the conversion, and this
 * is the budget that decides whether a frame still gets it.  It used to be 45 ms, but a
 * conversion here measures ~58 ms without the pass, so the gate was below normal operation
 * and every single frame silently skipped it.  90 ms keeps it on: measured on the same
 * scene, detail in versus out is +9% edge energy for +3% noise (top 5% gradient 65.9 ->
 * 71.8 on a 1280x960 frame) at a cost of ~21 ms per frame, which is worth it on this
 * camera.  It still sheds one recovery frame after a real spike.
 */
#define AV_CONVERSION_OVERLOAD_US 90000L
/*
 * Luma refinement runs on the 640x480 colour grid, before the 2x output pass.
 * A 5-tap local mean doubles as the noise smoother and as the unsharp blur, so
 * a flat area is pulled towards its own mean and the high-pass there is zero:
 * sharpening can only act where the denoiser already left a real edge.
 * AV_LUMA_EDGE_LUMA8 sets where edge protection starts; the ramp runs to 2x it.
 */
#define AV_LUMA_EDGE_LUMA8       32UL
#define AV_LUMA_MOTION_EDGE_LUMA8 16UL
#define AV_LUMA_DETAIL_CLAMP     24L
#define AV_STATIC_RGB32       \
    0xe436eb7e, 0x524f, 0x11ce, 0x9f, 0x53, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70
#define AV_STATIC_RGB24       \
    0xe436eb7d, 0x524f, 0x11ce, 0x9f, 0x53, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70

typedef struct _AV_DEVICE_CONTEXT {
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
    volatile LONG ConversionCompleted;
    volatile LONG LastCaptureStatus;
    volatile LONG RawMinimum;
    volatile LONG RawMaximum;
    volatile LONG RawMean;
    volatile LONG RawNonZero;
    volatile LONG RawSampleCount;
    volatile LONG RawFirst0;
    volatile LONG RawFirst1;
    volatile LONG RgbNonBlack;
    volatile LONG RgbMinimum;
    volatile LONG RgbMaximum;
    volatile LONG RgbMean;
    volatile LONG RgbFirst;
    volatile LONG RgbCenter;
    volatile LONG DynamicBlack;
    volatile LONG DynamicWhite;
    volatile LONG PercentileBlack;
    volatile LONG PercentileWhite;
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
    volatile LONG AndroidLtmInitialized;
    volatile LONG AndroidLtmStrengthQ10;
    volatile LONG AndroidLtmP95;
    volatile LONG DenoiseStrengthQ10;
    volatile LONG DenoiseTemporalAccepted;
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
    volatile LONG PublishedReal;
    volatile LONG CaptureAttempted;
    volatile LONG ActivePinCount;
    volatile LONG PinStateChangeCount;
    volatile LONG LastKsStateFrom;
    volatile LONG LastKsStateTo;
    volatile LONG BackendAcquireCount;
    volatile LONG LastBackendAcquireStatus;
    volatile LONG SetPowerCount;
    volatile LONG LastPowerFrom;
    volatile LONG LastPowerTo;
    volatile LONG DevicePowerState;
    volatile LONG LastCaptureState;
    volatile LONG SetDataFormatCount;
    volatile LONG LastSetDataFormatStatus;
    volatile LONG LastFormatWidth;
    volatile LONG LastFormatHeight;
    volatile LONG LastFormatIntervalLow;
    volatile LONG LastFormatIntervalHigh;
    volatile LONG PinCreateCount;
    volatile LONG PinCloseCount;
    volatile LONG RunCount;
    volatile LONG NonRunCount;
    volatile LONG TimerTickCount;
    volatile LONG HardwareCaptureInProgress;
    volatile LONG RefreshQueueCount;
    volatile LONG RefreshStartCount;
    volatile LONG RefreshSuccessCount;
    volatile LONG RefreshFailureCount;
    volatile LONG RefreshSkipCount;
    volatile LONG LastRefreshStatus;
    volatile LONG LastCaptureDurationUs;
    volatile LONG MaximumCaptureDurationUs;
    volatile LONG LastConversionDurationUs;
    volatile LONG MaximumConversionDurationUs;
    volatile LONG AnalysisDurationUs;
    volatile LONG ColorDurationUs;
    volatile LONG RefineDurationUs;
    volatile LONG UpscaleDurationUs;
    volatile LONG ProcessCallCount;
    volatile LONG FrameDeliveredCount;
    volatile LONG LastFrameExtent;
    volatile LONG LastFrameDataUsed;
    volatile LONG LastFrameOptions;
    volatile LONG LastOffsetRemaining;
    volatile LONG LastHeaderDataMatchesOffset;
    volatile LONG LastPresentationLow;
    volatile LONG LastPresentationHigh;
    volatile LONG NoStreamPointerCount;
    volatile LONG BufferTooSmallCount;
    volatile LONG PinCreateProcessId;
    volatile LONG PinCreateThreadId;
    volatile LONG AcquireProcessId;
    volatile LONG AcquireThreadId;
    volatile LONG RunProcessId;
    volatile LONG RunThreadId;
    volatile LONG CaptureProcessId;
    volatile LONG CaptureThreadId;
    volatile LONG CaptureIrql;
    volatile LONG InterfaceEnumerateStatus;
    volatile LONG InterfacePhysicalLocationStatus;
    volatile LONG InterfacePanelIdStatus;
    volatile LONG InterfacePropertyCount;
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
    volatile LONG PublishedGeneration;
    volatile LONG CaptureState;
    volatile LONG RefreshQueued;
    volatile LONG RefreshFailureStreak;
    WDFTIMER BackendIdleTimer;
    volatile LONG BackendIdleScheduleCount;
    volatile LONG BackendIdleCancelCount;
    volatile LONG BackendIdleFireCount;
    volatile LONG BackendIdleReleaseCount;
    volatile LONG BackendIdleSkipCount;
    volatile LONG Removing;
    volatile LONG Cleaned;
} AV_DEVICE_CONTEXT, *PAV_DEVICE_CONTEXT;

typedef struct _AV_PIN_CONTEXT {
    PKSPIN Pin;
    PAV_DEVICE_CONTEXT Device;
    WDFTIMER Timer;
    volatile LONG Running;
    volatile LONG FrameReady;
    volatile LONG RefreshTick;
    volatile LONG LastDeliveredGeneration;
    BOOLEAN OutputRgb24;
    ULONGLONG FrameNumber;
} AV_PIN_CONTEXT, *PAV_PIN_CONTEXT;

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
EVT_WDF_TIMER AvBackendIdleTimerCallback;
EVT_WDF_WORKITEM AvCaptureWorkItem;
EVT_WDF_WORKITEM AvConversionWorkItem;

static NTSTATUS AvDeviceAdd(PKSDEVICE Device);
static NTSTATUS AvDeviceStart(PKSDEVICE Device, PIRP Irp,
                              PCM_RESOURCE_LIST Translated,
                              PCM_RESOURCE_LIST Untranslated);
static VOID AvDeviceRemove(PKSDEVICE Device, PIRP Irp);
static VOID AvDeviceSurpriseRemoval(PKSDEVICE Device, PIRP Irp);
static VOID AvDeviceSetPower(PKSDEVICE Device, PIRP Irp,
                             DEVICE_POWER_STATE To,
                             DEVICE_POWER_STATE From);
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
static VOID AvFrontRawConsumer(PVOID ConsumerContext, const USHORT *Raw,
                               ULONG Width, ULONG Height);
static VOID AvFrontConvertRaw(PVOID ConsumerContext, const USHORT *Raw,
                             ULONG Width, ULONG Height);
static VOID AvProcessConversionQueue(PAV_DEVICE_CONTEXT Device);
static VOID AvConversionThread(PVOID StartContext);
static NTSTATUS AvRunCapture(PAV_DEVICE_CONTEXT Device);
static NTSTATUS AvPublishInterfaceProperties(PAV_DEVICE_CONTEXT Device);

static const DEVPROPKEY AvDevpkeyDevicePanelId = {
    {
        0x8dbc9c86, 0x97a9, 0x4bff,
        { 0x9b, 0xc6, 0xbf, 0xe9, 0x5d, 0x3e, 0x6d, 0xad }
    },
    2
};

static NTSTATUS
AvPublishInterfaceProperties(_Inout_ PAV_DEVICE_CONTEXT Device)
{
    static const WCHAR panelId[] =
        L"{00000000-0000-0000-ffff-ffffffffffff}\\0000\\5";
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
    pld.Panel = AcpiPldPanelFront;
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
            &AvDevpkeyDevicePanelId,
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
AvWriteDiagnostic(_In_opt_ HANDLE Key,
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
AvWriteCaptureDiagnostics(_In_ PAV_DEVICE_CONTEXT Device,
                          _In_ ULONG CaptureState)
{
    InterlockedExchange(&Device->LastCaptureState, (LONG)CaptureState);
    AvWriteDiagnostic(Device->DiagnosticKey, L"NabuAv_Build", 2264);
    AvWriteDiagnostic(Device->DiagnosticKey, L"NabuAv_AndroidTone", 97);
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_CaptureState", CaptureState);
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_CaptureStatus",
        (ULONG)InterlockedCompareExchange(
            &Device->LastCaptureStatus, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_ConversionCompleted",
        (ULONG)InterlockedCompareExchange(
            &Device->ConversionCompleted, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_RawMinimum",
        (ULONG)InterlockedCompareExchange(&Device->RawMinimum, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_RawMaximum",
        (ULONG)InterlockedCompareExchange(&Device->RawMaximum, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_RawMean",
        (ULONG)InterlockedCompareExchange(&Device->RawMean, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_RawNonZero",
        (ULONG)InterlockedCompareExchange(&Device->RawNonZero, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_RawSampleCount",
        (ULONG)InterlockedCompareExchange(
            &Device->RawSampleCount, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_RawFirst0",
        (ULONG)InterlockedCompareExchange(&Device->RawFirst0, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_RawFirst1",
        (ULONG)InterlockedCompareExchange(&Device->RawFirst1, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_RgbNonBlack",
        (ULONG)InterlockedCompareExchange(&Device->RgbNonBlack, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_RgbMinimum",
        (ULONG)InterlockedCompareExchange(&Device->RgbMinimum, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_RgbMaximum",
        (ULONG)InterlockedCompareExchange(&Device->RgbMaximum, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_RgbMean",
        (ULONG)InterlockedCompareExchange(&Device->RgbMean, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_RgbFirst",
        (ULONG)InterlockedCompareExchange(&Device->RgbFirst, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_RgbCenter",
        (ULONG)InterlockedCompareExchange(&Device->RgbCenter, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_DynamicBlack",
        (ULONG)InterlockedCompareExchange(&Device->DynamicBlack, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_DynamicWhite",
        (ULONG)InterlockedCompareExchange(&Device->DynamicWhite, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_PercentileBlack",
        (ULONG)InterlockedCompareExchange(
            &Device->PercentileBlack, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_PercentileWhite",
        (ULONG)InterlockedCompareExchange(
            &Device->PercentileWhite, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_StableBlack",
        (ULONG)InterlockedCompareExchange(&Device->StableBlack, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_StableWhite",
        (ULONG)InterlockedCompareExchange(&Device->StableWhite, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_StableGainR",
        (ULONG)InterlockedCompareExchange(&Device->StableGainR, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_StableGainB",
        (ULONG)InterlockedCompareExchange(&Device->StableGainB, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_StableFactoryGainR",
        (ULONG)InterlockedCompareExchange(
            &Device->StableFactoryGainR, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_StableFactoryGainB",
        (ULONG)InterlockedCompareExchange(
            &Device->StableFactoryGainB, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_StableAwbRefQ10",
        (ULONG)InterlockedCompareExchange(
            &Device->StableAwbRefQ10, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_StableCcmPosQ10",
        (ULONG)InterlockedCompareExchange(
            &Device->StableCcmPosQ10, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_AwbRgQ14",
        (ULONG)InterlockedCompareExchange(&Device->AwbRgQ14, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_AwbBgQ14",
        (ULONG)InterlockedCompareExchange(&Device->AwbBgQ14, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_ToneLiftPasses",
        (ULONG)InterlockedCompareExchange(
            &Device->ToneLiftPasses, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_ToneLiftQ10",
        (ULONG)InterlockedCompareExchange(
            &Device->StableLiftQ10, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_AndroidLtmStrengthQ10",
        (ULONG)InterlockedCompareExchange(
            &Device->AndroidLtmStrengthQ10, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_AndroidLtmP95",
        (ULONG)InterlockedCompareExchange(
            &Device->AndroidLtmP95, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_DenoiseStrengthQ10",
        (ULONG)InterlockedCompareExchange(
            &Device->DenoiseStrengthQ10, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_DenoiseTemporalAccepted",
        (ULONG)InterlockedCompareExchange(
            &Device->DenoiseTemporalAccepted, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_PublishedReal",
        (ULONG)InterlockedCompareExchange(&Device->PublishedReal, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_PublishedGeneration",
        (ULONG)InterlockedCompareExchange(
            &Device->PublishedGeneration, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_RawConversionBusy",
        (ULONG)InterlockedCompareExchange(
            &Device->RawConversionBusy, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_RawCopyAccepted",
        (ULONG)InterlockedCompareExchange(
            &Device->RawCopyAccepted, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_RawDroppedCount",
        (ULONG)InterlockedCompareExchange(
            &Device->RawDroppedCount, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_RawDuplicateCount",
        (ULONG)InterlockedCompareExchange(
            &Device->RawDuplicateCount, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_RawPendingValid",
        (ULONG)InterlockedCompareExchange(
            &Device->RawPendingValid, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_RawPendingQueuedCount",
        (ULONG)InterlockedCompareExchange(
            &Device->RawPendingQueuedCount, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_RawPendingDequeuedCount",
        (ULONG)InterlockedCompareExchange(
            &Device->RawPendingDequeuedCount, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_RawPendingOverwriteCount",
        (ULONG)InterlockedCompareExchange(
            &Device->RawPendingOverwriteCount, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_ConversionThreadRunning",
        (ULONG)InterlockedCompareExchange(
            &Device->ConversionThreadRunning, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_ConversionThreadPriority",
        (ULONG)InterlockedCompareExchange(
            &Device->ConversionThreadPriority, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_ConversionThreadSignalCount",
        (ULONG)InterlockedCompareExchange(
            &Device->ConversionThreadSignalCount, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_ConversionThreadWakeCount",
        (ULONG)InterlockedCompareExchange(
            &Device->ConversionThreadWakeCount, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_CaptureAttempted",
        (ULONG)InterlockedCompareExchange(&Device->CaptureAttempted, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_ActivePinCount",
        (ULONG)InterlockedCompareExchange(&Device->ActivePinCount, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_PinStateChangeCount",
        (ULONG)InterlockedCompareExchange(
            &Device->PinStateChangeCount, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_LastKsStateFrom",
        (ULONG)InterlockedCompareExchange(&Device->LastKsStateFrom, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_LastKsStateTo",
        (ULONG)InterlockedCompareExchange(&Device->LastKsStateTo, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_BackendAcquireCount",
        (ULONG)InterlockedCompareExchange(
            &Device->BackendAcquireCount, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_BackendAcquireStatus",
        (ULONG)InterlockedCompareExchange(
            &Device->LastBackendAcquireStatus, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_SetPowerCount",
        (ULONG)InterlockedCompareExchange(&Device->SetPowerCount, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_LastPowerFrom",
        (ULONG)InterlockedCompareExchange(&Device->LastPowerFrom, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_LastPowerTo",
        (ULONG)InterlockedCompareExchange(&Device->LastPowerTo, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_DevicePowerState",
        (ULONG)InterlockedCompareExchange(
            &Device->DevicePowerState, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_SetDataFormatCount",
        (ULONG)InterlockedCompareExchange(
            &Device->SetDataFormatCount, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_LastSetDataFormatStatus",
        (ULONG)InterlockedCompareExchange(
            &Device->LastSetDataFormatStatus, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_LastFormatWidth",
        (ULONG)InterlockedCompareExchange(
            &Device->LastFormatWidth, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_LastFormatHeight",
        (ULONG)InterlockedCompareExchange(
            &Device->LastFormatHeight, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_LastFormatIntervalLow",
        (ULONG)InterlockedCompareExchange(
            &Device->LastFormatIntervalLow, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_LastFormatIntervalHigh",
        (ULONG)InterlockedCompareExchange(
            &Device->LastFormatIntervalHigh, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_PinCreateCount",
        (ULONG)InterlockedCompareExchange(&Device->PinCreateCount, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_PinCloseCount",
        (ULONG)InterlockedCompareExchange(&Device->PinCloseCount, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_RunCount",
        (ULONG)InterlockedCompareExchange(&Device->RunCount, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_NonRunCount",
        (ULONG)InterlockedCompareExchange(&Device->NonRunCount, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_TimerTickCount",
        (ULONG)InterlockedCompareExchange(&Device->TimerTickCount, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_HardwareCaptureInProgress",
        (ULONG)InterlockedCompareExchange(
            &Device->HardwareCaptureInProgress, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_InterfaceEnumerateStatus",
        (ULONG)InterlockedCompareExchange(
            &Device->InterfaceEnumerateStatus, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey,
        L"NabuAv_InterfacePhysicalLocationStatus",
        (ULONG)InterlockedCompareExchange(
            &Device->InterfacePhysicalLocationStatus, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_InterfacePanelIdStatus",
        (ULONG)InterlockedCompareExchange(
            &Device->InterfacePanelIdStatus, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_InterfacePropertyCount",
        (ULONG)InterlockedCompareExchange(
            &Device->InterfacePropertyCount, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_RefreshQueueCount",
        (ULONG)InterlockedCompareExchange(
            &Device->RefreshQueueCount, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_RefreshStartCount",
        (ULONG)InterlockedCompareExchange(
            &Device->RefreshStartCount, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_RefreshSuccessCount",
        (ULONG)InterlockedCompareExchange(
            &Device->RefreshSuccessCount, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_RefreshFailureCount",
        (ULONG)InterlockedCompareExchange(
            &Device->RefreshFailureCount, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_RefreshSkipCount",
        (ULONG)InterlockedCompareExchange(
            &Device->RefreshSkipCount, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_LastRefreshStatus",
        (ULONG)InterlockedCompareExchange(
            &Device->LastRefreshStatus, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_LastCaptureDurationUs",
        (ULONG)InterlockedCompareExchange(
            &Device->LastCaptureDurationUs, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_MaximumCaptureDurationUs",
        (ULONG)InterlockedCompareExchange(
            &Device->MaximumCaptureDurationUs, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_LastConversionDurationUs",
        (ULONG)InterlockedCompareExchange(
            &Device->LastConversionDurationUs, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_MaximumConversionDurationUs",
        (ULONG)InterlockedCompareExchange(
            &Device->MaximumConversionDurationUs, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_AnalysisDurationUs",
        (ULONG)InterlockedCompareExchange(
            &Device->AnalysisDurationUs, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_ColorDurationUs",
        (ULONG)InterlockedCompareExchange(
            &Device->ColorDurationUs, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_RefineDurationUs",
        (ULONG)InterlockedCompareExchange(
            &Device->RefineDurationUs, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_UpscaleDurationUs",
        (ULONG)InterlockedCompareExchange(
            &Device->UpscaleDurationUs, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_ProcessCallCount",
        (ULONG)InterlockedCompareExchange(&Device->ProcessCallCount, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_FrameDeliveredCount",
        (ULONG)InterlockedCompareExchange(
            &Device->FrameDeliveredCount, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_LastFrameExtent",
        (ULONG)InterlockedCompareExchange(
            &Device->LastFrameExtent, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_LastFrameDataUsed",
        (ULONG)InterlockedCompareExchange(
            &Device->LastFrameDataUsed, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_LastFrameOptions",
        (ULONG)InterlockedCompareExchange(
            &Device->LastFrameOptions, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_LastOffsetRemaining",
        (ULONG)InterlockedCompareExchange(
            &Device->LastOffsetRemaining, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_LastHeaderDataMatchesOffset",
        (ULONG)InterlockedCompareExchange(
            &Device->LastHeaderDataMatchesOffset, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_LastPresentationLow",
        (ULONG)InterlockedCompareExchange(
            &Device->LastPresentationLow, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_LastPresentationHigh",
        (ULONG)InterlockedCompareExchange(
            &Device->LastPresentationHigh, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_NoStreamPointerCount",
        (ULONG)InterlockedCompareExchange(
            &Device->NoStreamPointerCount, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_BufferTooSmallCount",
        (ULONG)InterlockedCompareExchange(
            &Device->BufferTooSmallCount, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_PinCreateProcessId",
        (ULONG)InterlockedCompareExchange(
            &Device->PinCreateProcessId, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_PinCreateThreadId",
        (ULONG)InterlockedCompareExchange(
            &Device->PinCreateThreadId, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_AcquireProcessId",
        (ULONG)InterlockedCompareExchange(
            &Device->AcquireProcessId, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_AcquireThreadId",
        (ULONG)InterlockedCompareExchange(
            &Device->AcquireThreadId, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_RunProcessId",
        (ULONG)InterlockedCompareExchange(
            &Device->RunProcessId, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_RunThreadId",
        (ULONG)InterlockedCompareExchange(
            &Device->RunThreadId, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_CaptureProcessId",
        (ULONG)InterlockedCompareExchange(
            &Device->CaptureProcessId, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_CaptureThreadId",
        (ULONG)InterlockedCompareExchange(
            &Device->CaptureThreadId, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_CaptureIrql",
        (ULONG)InterlockedCompareExchange(
            &Device->CaptureIrql, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_BackendIdleScheduleCount",
        (ULONG)InterlockedCompareExchange(
            &Device->BackendIdleScheduleCount, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_BackendIdleCancelCount",
        (ULONG)InterlockedCompareExchange(
            &Device->BackendIdleCancelCount, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_BackendIdleFireCount",
        (ULONG)InterlockedCompareExchange(
            &Device->BackendIdleFireCount, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_BackendIdleReleaseCount",
        (ULONG)InterlockedCompareExchange(
            &Device->BackendIdleReleaseCount, 0, 0));
    AvWriteDiagnostic(
        Device->DiagnosticKey, L"NabuAv_BackendIdleSkipCount",
        (ULONG)InterlockedCompareExchange(
            &Device->BackendIdleSkipCount, 0, 0));
    if (Device->WdfDevice != NULL) {
        NabuFrontBackendWriteDiagnostics(
            Device->WdfDevice,
            AvWriteDiagnostic,
            Device->DiagnosticKey);
    }
}

static USHORT
AvAverage2(_In_ USHORT A, _In_ USHORT B)
{
    return (USHORT)(((ULONG)A + (ULONG)B + 1UL) / 2UL);
}

static ULONG
AvFrontAveragePixel2(_In_ ULONG A, _In_ ULONG B)
{
    return 0xFF000000UL |
        (((((A >> 16) & 0xFFUL) + ((B >> 16) & 0xFFUL) + 1UL) >> 1) << 16) |
        (((((A >> 8) & 0xFFUL) + ((B >> 8) & 0xFFUL) + 1UL) >> 1) << 8) |
        ((((A & 0xFFUL) + (B & 0xFFUL) + 1UL) >> 1));
}

static ULONG
AvFrontAveragePixel4(_In_ ULONG A, _In_ ULONG B,
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
AvFrontAddLumaDetail(_In_ ULONG Pixel, _In_ LONG Delta)
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
AvFrontApplyAndroidCac(_In_ ULONG Pixel, _In_ ULONG Reference)
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
    /* Exact front CAC22 spot thresholds from the OV8856 factory profile. */
    if (dy < 10 || (dcr <= 40 && dcb <= 40)) {
        return Pixel;
    }
    if (cr > refCr + 40) cr = refCr + 40;
    if (cr < refCr - 40) cr = refCr - 40;
    if (cb > refCb + 40) cb = refCb + 40;
    if (cb < refCb - 40) cb = refCb - 40;
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
AvFrontUpscaleWithBayerDetail(
    _Inout_ PAV_DEVICE_CONTEXT Device,
    _In_reads_(RawWidth * RawHeight) const USHORT *Raw,
    _In_ ULONG RawWidth,
    _In_ ULONG RawHeight,
    _In_reads_(AV_COLOR_WIDTH * AV_COLOR_HEIGHT) const ULONG *Base,
    _Out_writes_(AV_OUTPUT_WIDTH * AV_OUTPUT_HEIGHT) PULONG Output,
    _In_ BOOLEAN EnableRawDetail)
{
    ULONG detailStrengthQ8 = 0;
    ULONG detailThresholdRaw = 64UL;
    ULONG baseX;
    ULONG baseY;

    /* Recover promptly after a scheduler/conversion spike.  Normal frames
     * retain AV136 detail; only the frame following a >90 ms conversion
     * omits the optional native-detail reconstruction. */
    if (EnableRawDetail &&
        InterlockedCompareExchange(
            &Device->LastConversionDurationUs, 0, 0) <=
            AV_CONVERSION_OVERLOAD_US &&
        RawWidth == AV_OUTPUT_WIDTH &&
        RawHeight == AV_OUTPUT_HEIGHT) {
        ULONG p95 = (ULONG)InterlockedCompareExchange(
            &Device->AndroidLtmP95, 0, 0);

        /*
         * Reconstruct native 1280x960 green/luma detail after NR.  The old
         * fixed 64/1023 gate discarded skin, cloth and fan detail even in a
         * well-lit scene.  Keep a conservative gate in low light but never
         * switch the path off: a dim frame that loses all of its native detail
         * reads as painted, which is what the denoiser below then has to undo.
         */
        if (p95 >= AV_FRONT_DENOISE_OFF_P95) {
            detailStrengthQ8 = 320UL;
            detailThresholdRaw = 12UL;
        } else if (p95 > AV_FRONT_DENOISE_FULL_P95) {
            ULONG sceneQ10 =
                ((p95 - AV_FRONT_DENOISE_FULL_P95) * 1024UL + 48UL) /
                (AV_FRONT_DENOISE_OFF_P95 -
                 AV_FRONT_DENOISE_FULL_P95);

            detailStrengthQ8 =
                192UL + ((sceneQ10 * 128UL + 512UL) >> 10);
            detailThresholdRaw = 24UL -
                ((sceneQ10 * 12UL + 512UL) >> 10);
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

            p00 = AvFrontApplyAndroidCac(
                p00, AvFrontAveragePixel4(p00, pLeft, pUp, pUpLeft));
            /*
             * A decoded RGB sample is located at the centre of its 2x2 Bayer
             * cell. Centre the half-pixel chroma grid on the native luma grid.
             */
            ULONG topLeft = AvFrontAveragePixel4(
                p00, pLeft, pUp, pUpLeft);
            ULONG topRight = AvFrontAveragePixel2(p00, pUp);
            ULONG bottomLeft = AvFrontAveragePixel2(p00, pLeft);
            ULONG bottomRight = p00;

            if (detailStrengthQ8 != 0UL) {
                ULONG evenX = (AV_COLOR_WIDTH - 1UL - baseX) * 2UL;
                ULONG evenY = baseY * 2UL;
                ULONG green0 = Raw[evenY * RawWidth + evenX + 1UL];
                ULONG green1 = Raw[(evenY + 1UL) * RawWidth + evenX];
                ULONG greenAverage = (green0 + green1 + 1UL) >> 1;
                ULONG greenIndex;
                ULONG greenBLeft;
                ULONG greenBUp;
                ULONG greenRRight;
                ULONG greenRDown;
                ULONG greenB;
                ULONG greenR;
                LONG deltaTopLeft;
                LONG deltaTopRight;
                LONG deltaBottomLeft;
                LONG deltaBottomRight;

                /* Reconstruct the two missing native-resolution green sites. */
                greenBLeft = evenX >= 2UL ?
                    Raw[evenY * RawWidth + evenX - 1UL] : green0;
                greenBUp = evenY >= 2UL ?
                    Raw[(evenY - 1UL) * RawWidth + evenX] : green1;
                greenRRight = evenX + 2UL < RawWidth ?
                    Raw[(evenY + 1UL) * RawWidth + evenX + 2UL] : green1;
                greenRDown = evenY + 2UL < RawHeight ?
                    Raw[(evenY + 2UL) * RawWidth + evenX + 1UL] : green0;
                greenB = (green0 + green1 + greenBLeft + greenBUp + 2UL) >> 2;
                greenR = (green0 + green1 + greenRRight + greenRDown + 2UL) >> 2;

                /*
                 * The chroma base is 640x480, but the sensor luma is really
                 * 1280x960.  Restore that native green/luma plane at all four
                 * Bayer sites.  Horizontal mirror changes BGGR to GBRG in the
                 * delivered image: G0, B, R, G1.
                 */
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
                    rawDelta = (LONG)green0 - (LONG)greenAverage;
                    magnitude = rawDelta < 0 ? -rawDelta : rawDelta;
                    deltaTopLeft = magnitude > (LONG)detailThresholdRaw ?
                        ((rawDelta < 0 ? -(magnitude - (LONG)detailThresholdRaw) : magnitude - (LONG)detailThresholdRaw) * scaleQ8 * (LONG)detailStrengthQ8) >> 16 : 0;
                    rawDelta = (LONG)greenB - (LONG)greenAverage;
                    magnitude = rawDelta < 0 ? -rawDelta : rawDelta;
                    deltaTopRight = magnitude > (LONG)detailThresholdRaw ?
                        ((rawDelta < 0 ? -(magnitude - (LONG)detailThresholdRaw) : magnitude - (LONG)detailThresholdRaw) * scaleQ8 * (LONG)detailStrengthQ8) >> 16 : 0;
                    rawDelta = (LONG)greenR - (LONG)greenAverage;
                    magnitude = rawDelta < 0 ? -rawDelta : rawDelta;
                    deltaBottomLeft = magnitude > (LONG)detailThresholdRaw ?
                        ((rawDelta < 0 ? -(magnitude - (LONG)detailThresholdRaw) : magnitude - (LONG)detailThresholdRaw) * scaleQ8 * (LONG)detailStrengthQ8) >> 16 : 0;
                    rawDelta = (LONG)green1 - (LONG)greenAverage;
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
                    topLeft = AvFrontAddLumaDetail(topLeft, deltaTopLeft);
                }
                if (deltaTopRight != 0) {
                    topRight = AvFrontAddLumaDetail(topRight, deltaTopRight);
                }
                if (deltaBottomLeft != 0) {
                    bottomLeft = AvFrontAddLumaDetail(bottomLeft, deltaBottomLeft);
                }
                if (deltaBottomRight != 0) {
                    bottomRight = AvFrontAddLumaDetail(bottomRight, deltaBottomRight);
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

static VOID
AvInitializeFrontFactoryLsc(_Inout_ PAV_DEVICE_CONTEXT Device)
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
                ((ULONG)AvFactoryFrontLscR[base] *
                     (1024UL - weightQ10) +
                 (ULONG)AvFactoryFrontLscR[base + 1UL] *
                     weightQ10 + 512UL) >> 10);
            Device->FactoryLscG[row][x] = (USHORT)(
                ((ULONG)AvFactoryFrontLscG[base] *
                     (1024UL - weightQ10) +
                 (ULONG)AvFactoryFrontLscG[base + 1UL] *
                     weightQ10 + 512UL) >> 10);
            Device->FactoryLscB[row][x] = (USHORT)(
                ((ULONG)AvFactoryFrontLscB[base] *
                     (1024UL - weightQ10) +
                 (ULONG)AvFactoryFrontLscB[base + 1UL] *
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
    /*
     * AV119: The colour conversion works on one sample per 2x2 Bayer cell.
     * Resolve the 17x13 factory mesh to that exact grid once, instead of
     * doing six interpolating multiplies for every pixel of every frame.
     */
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
        ULONG detailDenominator = x > 64UL ? x - 64UL : 1UL;

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
            ((ULONG)AvFactoryFrontGamma[gammaIndex] *
                 (1024UL - fractionQ10) +
             (ULONG)AvFactoryFrontGamma[gammaIndex + 1UL] *
                 fractionQ10 + 512UL) >> 10;
        output10 = (output10 + 2UL) >> 2;
        Device->ToneLutR[x] =
            (UCHAR)(output10 > 255UL ? 255UL : output10);
    }
    KeMemoryBarrier();
    InterlockedExchange(&Device->FactoryTuningInitialized, 1);
}

static ULONG
AvFrontLinearize10(_In_ ULONG Value)
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
AvFrontFactoryLsc(
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
AvFrontFactoryGamma8(_In_ ULONG Value)
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
        ((ULONG)AvFactoryFrontGamma[index] *
             (1024UL - fractionQ10) +
         (ULONG)AvFactoryFrontGamma[index + 1UL] *
             fractionQ10 + 512UL) >> 10;
    output10 = (output10 + 2UL) >> 2;
    return (UCHAR)(output10 > 255UL ? 255UL : output10);
}

/*
 * AV99: reproduce the final luma adaptation measured from this tablet's
 * stock Android front-camera preview.  The points are paired Windows AV96
 * and Android quantiles from the same OV8856 scene:
 *   0->0, 27->100, 93->174, 151->209, 255->255.
 *
 * Apply the luma delta equally to RGB.  Qualcomm LTM saturation control
 * deliberately avoids multiplying nonlinear RGB chroma when shadows are
 * boosted; an additive luma delta keeps skin hue stable and avoids the pale
 * cyan face produced by the earlier per-channel stretch.
 */
static UCHAR
AvFrontAndroidTone8(_In_ ULONG Value)
{
    ULONG output;

    if (Value >= 255UL) {
        return 255U;
    }
    if (Value <= 27UL) {
        output = (Value * 100UL + 13UL) / 27UL;
    } else if (Value <= 93UL) {
        output = 100UL +
            ((Value - 27UL) * 74UL + 33UL) / 66UL;
    } else if (Value <= 151UL) {
        output = 174UL +
            ((Value - 93UL) * 35UL + 29UL) / 58UL;
    } else {
        output = 209UL +
            ((Value - 151UL) * 46UL + 52UL) / 104UL;
    }
    return (UCHAR)(output > 255UL ? 255UL : output);
}

static __forceinline ULONG
AvFrontPixelLuma(_In_ ULONG Pixel)
{
    ULONG red = (Pixel >> 16) & 0xFFUL;
    ULONG green = (Pixel >> 8) & 0xFFUL;
    ULONG blue = Pixel & 0xFFUL;

    return (54UL * red + 183UL * green + 19UL * blue + 128UL) >> 8;
}

/*
 * Luma noise reduction, temporal luma filtering and edge-protected unsharp on
 * the 640x480 colour grid, written into RefineBuffer for the 2x output pass.
 *
 * Every filter this pipeline had before worked on chroma only, so the grain
 * that dominates a dim frame passed straight through and nothing put detail
 * back.  The 5-tap local mean does double duty: it is what a flat pixel is
 * pulled towards and it is the blur the high-pass is taken against, so the two
 * cancel in noise-only areas and the sharpen can only fire on an edge the
 * denoiser chose to keep.
 */
static VOID
AvFrontRefineLuma(
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
            (USHORT)AvFrontPixelLuma(source[x]);

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
                nextRow[x] = (USHORT)AvFrontPixelLuma(below[x]);
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
            out[x] = AvFrontAddLumaDetail(
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

static VOID
AvFrontBuildAndroidDeltaLut(
    _Inout_ PAV_DEVICE_CONTEXT Device,
    _Out_writes_(256) UCHAR *DeltaLut)
{
    ULONG strengthQ10 = (ULONG)InterlockedCompareExchange(
        &Device->AndroidLtmStrengthQ10, 0, 0);
    ULONG i;

    /* A neutral half-strength first frame avoids a dark or bright flash. */
    if (InterlockedCompareExchange(
            &Device->AndroidLtmInitialized, 0, 0) == 0) {
        strengthQ10 = 512UL;
    }
    for (i = 0; i < 256UL; ++i) {
        ULONG full = AvFrontAndroidTone8(i);

        DeltaLut[i] = (UCHAR)(
            ((full - i) * strengthQ10 + 512UL) / 1024UL);
    }
}

static __forceinline ULONG
AvFrontApplyAndroidTonePixel(
    _In_ ULONG Pixel,
    _In_reads_(256) const UCHAR *DeltaLut)
{
    ULONG red = (Pixel >> 16) & 0xFFUL;
    ULONG green = (Pixel >> 8) & 0xFFUL;
    ULONG blue = Pixel & 0xFFUL;
    ULONG delta = DeltaLut[AvFrontPixelLuma(Pixel)];

    red += delta;
    green += delta;
    blue += delta;
    if (red > 255UL) red = 255UL;
    if (green > 255UL) green = 255UL;
    if (blue > 255UL) blue = 255UL;
    return 0xFF000000UL | (red << 16) | (green << 8) | blue;
}

static __forceinline ULONG
AvFrontApplyFastTemporalChroma(
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
AvFrontUpdateAndroidLtm(
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
    if (p95 <= AV_FRONT_ANDROID_LTM_DARK_END) {
        targetStrengthQ10 = 1024UL;
    } else if (p95 >= AV_FRONT_ANDROID_LTM_BRIGHT_START) {
        targetStrengthQ10 = 0UL;
    } else {
        targetStrengthQ10 =
            ((AV_FRONT_ANDROID_LTM_BRIGHT_START - p95) * 1024UL + 37UL) /
            (AV_FRONT_ANDROID_LTM_BRIGHT_START -
             AV_FRONT_ANDROID_LTM_DARK_END);
    }
    strengthQ10 = (ULONG)InterlockedCompareExchange(
        &Device->AndroidLtmStrengthQ10, 0, 0);
    if (InterlockedCompareExchange(
            &Device->AndroidLtmInitialized, 1, 0) == 0) {
        strengthQ10 = targetStrengthQ10;
    } else if (targetStrengthQ10 > strengthQ10 +
               AV_FRONT_ANDROID_LTM_STEP_Q10) {
        strengthQ10 += AV_FRONT_ANDROID_LTM_STEP_Q10;
    } else if (strengthQ10 > targetStrengthQ10 +
               AV_FRONT_ANDROID_LTM_STEP_Q10) {
        strengthQ10 -= AV_FRONT_ANDROID_LTM_STEP_Q10;
    } else {
        strengthQ10 = targetStrengthQ10;
    }
    InterlockedExchange(
        &Device->AndroidLtmStrengthQ10, (LONG)strengthQ10);
    InterlockedExchange(&Device->AndroidLtmP95, (LONG)p95);
}

/*
 * The stock Qualcomm preview path enables both ANR10 chroma filtering and
 * TF10 temporal filtering.  Keep luma from the current frame (so motion and
 * detail do not trail), but apply a small edge-aware spatial/temporal filter
 * to the two chroma differences in low light.  This specifically removes the
 * coloured Bayer-cell blocks which become visible after the Android LTM lift.
 */
static VOID
AvFrontApplyLowLightDenoise(_Inout_ PAV_DEVICE_CONTEXT Device)
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
    if (p95 <= AV_FRONT_DENOISE_FULL_P95) {
        targetStrengthQ10 = 1024UL;
    } else if (p95 >= AV_FRONT_DENOISE_OFF_P95) {
        targetStrengthQ10 = 0UL;
    } else {
        targetStrengthQ10 =
            ((AV_FRONT_DENOISE_OFF_P95 - p95) * 1024UL + 48UL) /
            (AV_FRONT_DENOISE_OFF_P95 - AV_FRONT_DENOISE_FULL_P95);
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
            ULONG centerY = AvFrontPixelLuma(center);
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
                ULONG pixelY = AvFrontPixelLuma(pixel);
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
                ULONG pixelY = AvFrontPixelLuma(pixel);
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

static ULONG
AvFrontCcmPositionForReference(_In_ ULONG Reference)
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
AvPackFrontFactoryPixel(
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

//
// The captured OV8856 frame is BGGR. Convert and scale directly to the
// 640x480 preview in the PASSIVE_LEVEL capture work item. RGB32 is a
// bottom-up DIB, hence the destination row reversal.
//
static VOID
AvBayerBggrToRgb32Scaled(
    _Inout_ PAV_DEVICE_CONTEXT Device,
    _In_reads_(RawWidth * RawHeight) const USHORT *Raw,
    _Out_writes_(OutputWidth * OutputHeight) PULONG Rgb,
    _In_ ULONG RawWidth,
    _In_ ULONG RawHeight,
    _In_ ULONG OutputWidth,
    _In_ ULONG OutputHeight,
    _Out_ PULONG DynamicBlack,
    _Out_ PULONG DynamicWhite)
{
    ULONGLONG sumR = 0, sumG = 0, sumB = 0;
    ULONGLONG countR = 0, countG = 0, countB = 0;
    ULONG gainR = AV_GAIN_ONE, gainB = AV_GAIN_ONE;
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
    ULONG black;
    ULONG white;
    ULONG sampleMean;
    ULONG normalizedMean;
    ULONG liftQ10;
    ULONG targetLiftQ10;
    ULONGLONG scaleR;
    ULONGLONG scaleG;
    ULONGLONG scaleB;
    ULONG sourceStepXQ16;
    ULONG sourceStepYQ16;
    ULONG meanR;
    ULONG meanG;
    ULONG meanB;
    ULONG rgQ14;
    ULONG bgQ14;
    ULONG bestReference = 0;
    ULONGLONG bestDistance = MAXULONGLONG;
    ULONG reference;
    ULONG targetCcmPositionQ10;
    ULONG ccmPositionQ10;
    ULONG lowerMatrix;
    ULONG matrixWeightQ10;
    LONG matrix[9];
    ULONG x, y;
    ULONG denoiseDivisor = 0;
    ULONG denoiseSpatialAccepted = 0;
    ULONG denoiseTemporalAccepted = 0;
    BOOLEAN packedPreview = RawWidth == 640UL && RawHeight == 960UL;
    LARGE_INTEGER stageFrequency;
    LARGE_INTEGER stageStarted;
    LARGE_INTEGER stageStopped;
    ULONGLONG stageTicks;
    ULONGLONG stageUs;

    stageStarted = KeQueryPerformanceCounter(&stageFrequency);
    AvInitializeFrontFactoryLsc(Device);
    RtlZeroMemory(histogram, sizeof(histogram));
    RtlZeroMemory(androidHistogram, sizeof(androidHistogram));
    AvFrontBuildAndroidDeltaLut(Device, androidDeltaLut);
    if (InterlockedCompareExchange(&Device->PublishedReal, 0, 0) != 0) {
        ULONG p95 = (ULONG)InterlockedCompareExchange(
            &Device->AndroidLtmP95, 0, 0);

        if (p95 <= AV_FRONT_DENOISE_FULL_P95) {
            denoiseDivisor = 2UL;
            InterlockedExchange(&Device->DenoiseStrengthQ10, 1024);
        } else if (p95 < AV_FRONT_DENOISE_OFF_P95) {
            denoiseDivisor = 4UL;
            InterlockedExchange(&Device->DenoiseStrengthQ10, 512);
        } else {
            /*
             * Android ANR10 does not switch chroma filtering off in bright
             * scenes.  Keep its mild 3:1 two-cell chroma blend active while
             * retaining the exact green-edge guard, so Bayer colour speckle
             * is removed without softening green/luma detail.
             */
            denoiseDivisor = 4UL;
            InterlockedExchange(&Device->DenoiseStrengthQ10, 512);
        }
    }

    if (packedPreview) {
        const ULONG *packed = (const ULONG *)Raw;

        for (y = 0; y < AV_COLOR_HEIGHT; y += 8UL) {
            for (x = 0; x < AV_COLOR_WIDTH; x += 8UL) {
                ULONG value = packed[y * AV_COLOR_WIDTH + x];
                ULONG b = (value & 0xFFUL) << 2;
                ULONG g0 = ((value >> 8) & 0xFFUL) << 2;
                ULONG g1 = ((value >> 16) & 0xFFUL) << 2;
                ULONG r = ((value >> 24) & 0xFFUL) << 2;

                if (b < rawMinimum) rawMinimum = b;
                if (g0 < rawMinimum) rawMinimum = g0;
                if (g1 < rawMinimum) rawMinimum = g1;
                if (r < rawMinimum) rawMinimum = r;
                if (b > rawMaximum) rawMaximum = b;
                if (g0 > rawMaximum) rawMaximum = g0;
                if (g1 > rawMaximum) rawMaximum = g1;
                if (r > rawMaximum) rawMaximum = r;
                if (b != 0) rawNonZero += 1;
                if (g0 != 0) rawNonZero += 1;
                if (g1 != 0) rawNonZero += 1;
                if (r != 0) rawNonZero += 1;
                sumB += b;
                sumG += g0;
                sumG += g1;
                sumR += r;
                histogram[b >> 2] += 1;
                histogram[g0 >> 2] += 1;
                histogram[g1 >> 2] += 1;
                histogram[r >> 2] += 1;
                countB += 1;
                countG += 2;
                countR += 1;
            }
        }
    } else {
        for (y = 0; y + 1UL < RawHeight; y += 8UL) {
            for (x = 0; x + 1UL < RawWidth; x += 8UL) {
                ULONG b = Raw[y * RawWidth + x];
                ULONG g0 = Raw[y * RawWidth + x + 1UL];
                ULONG g1 = Raw[(y + 1UL) * RawWidth + x];
                ULONG r = Raw[(y + 1UL) * RawWidth + x + 1UL];

                if (b < rawMinimum) rawMinimum = b;
                if (g0 < rawMinimum) rawMinimum = g0;
                if (g1 < rawMinimum) rawMinimum = g1;
                if (r < rawMinimum) rawMinimum = r;
                if (b > rawMaximum) rawMaximum = b;
                if (g0 > rawMaximum) rawMaximum = g0;
                if (g1 > rawMaximum) rawMaximum = g1;
                if (r > rawMaximum) rawMaximum = r;
                if (b != 0) rawNonZero += 1;
                if (g0 != 0) rawNonZero += 1;
                if (g1 != 0) rawNonZero += 1;
                if (r != 0) rawNonZero += 1;
                sumB += b;
                sumG += g0;
                sumG += g1;
                sumR += r;
                histogram[(b >> 2) > 255UL ? 255UL : (b >> 2)] += 1;
                histogram[(g0 >> 2) > 255UL ? 255UL : (g0 >> 2)] += 1;
                histogram[(g1 >> 2) > 255UL ? 255UL : (g1 >> 2)] += 1;
                histogram[(r >> 2) > 255UL ? 255UL : (r >> 2)] += 1;
                countB += 1;
                countG += 2;
                countR += 1;
            }
        }
    }
    InterlockedExchange(&Device->RawMinimum, (LONG)rawMinimum);
    InterlockedExchange(&Device->RawMaximum, (LONG)rawMaximum);
    sampleMean = (ULONG)((sumR + sumG + sumB) /
        (countR + countG + countB));
    InterlockedExchange(&Device->RawMean, (LONG)sampleMean);
    InterlockedExchange(&Device->RawNonZero, (LONG)rawNonZero);
    InterlockedExchange(
        &Device->RawSampleCount,
        (LONG)(countR + countG + countB));
    InterlockedExchange(
        &Device->RawFirst0, ((const volatile LONG *)Raw)[0]);
    InterlockedExchange(
        &Device->RawFirst1, ((const volatile LONG *)Raw)[1]);
    sampleTotal = (ULONG)(countR + countG + countB);
    lowTarget = sampleTotal / 200UL;
    if (lowTarget == 0UL) {
        lowTarget = 1UL;
    }
    /* Preserve the small face/window highlight population. */
    highTarget = (sampleTotal * 999UL + 999UL) / 1000UL;
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
    black = black > 8UL ? black - 8UL : 0UL;
    white = percentileHigh * 4UL + 19UL;
    if (white > 1023UL) {
        white = 1023UL;
    }
    if (white <= black + 32UL) {
        white = black + 32UL;
        if (white > 1023UL) {
            black = 991UL;
            white = 1023UL;
        }
    }
    InterlockedExchange(&Device->PercentileBlack, (LONG)(percentileLow * 4UL));
    InterlockedExchange(&Device->PercentileWhite, (LONG)(percentileHigh * 4UL + 3UL));
    *DynamicBlack = black;
    *DynamicWhite = white;

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

    if (InterlockedCompareExchange(
            &Device->ToneInitialized, 1, 0) == 0) {
        InterlockedExchange(&Device->StableBlack, (LONG)black);
        InterlockedExchange(&Device->StableWhite, (LONG)white);
        InterlockedExchange(&Device->StableGainR, (LONG)gainR);
        InterlockedExchange(&Device->StableGainB, (LONG)gainB);
    } else {
        ULONG previousBlack =
            (ULONG)InterlockedCompareExchange(
                &Device->StableBlack, 0, 0);
        ULONG previousWhite =
            (ULONG)InterlockedCompareExchange(
                &Device->StableWhite, 0, 0);
        ULONG previousGainR =
            (ULONG)InterlockedCompareExchange(
                &Device->StableGainR, 0, 0);
        ULONG previousGainB =
            (ULONG)InterlockedCompareExchange(
                &Device->StableGainB, 0, 0);

        black = (previousBlack * 7UL + black + 4UL) / 8UL;
        if (white > previousWhite) {
            white = (previousWhite * 3UL + white + 2UL) / 4UL;
        } else {
            white = (previousWhite * 15UL + white + 8UL) / 16UL;
        }
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
    *DynamicBlack = black;
    *DynamicWhite = white;
    normalizedMean = sampleMean > black ?
        (ULONG)(((ULONGLONG)(sampleMean - black) * 255ULL) /
            (ULONGLONG)(white - black)) :
        0UL;
    /*
     * Use a continuous, rate-limited shadow lift.  The earlier integer
     * one/two/three-pass switch produced a large brightness discontinuity
     * whenever the normalized RAW mean crossed 32 or 64.  Q10 interpolation
     * keeps the same monotonic highlight-safe curve without the visible
     * exposure pumping.  Limit motion to 1/64 pass per physical frame.
     */
    if (normalizedMean <= 24UL) {
        targetLiftQ10 = 2560UL;
    } else if (normalizedMean >= 72UL) {
        targetLiftQ10 = 1024UL;
    } else {
        targetLiftQ10 = 1024UL +
            ((72UL - normalizedMean) * 1536UL + 24UL) / 48UL;
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
    scaleG = (255ULL << 16) / (ULONGLONG)(white - black);
    scaleR = (scaleG * gainR) / AV_GAIN_ONE;
    scaleB = (scaleG * gainB) / AV_GAIN_ONE;
    sourceStepXQ16 = packedPreview ? 0UL :
        (ULONG)(((ULONGLONG)(RawWidth - 2UL) << 16) /
            AV_COLOR_WIDTH);
    sourceStepYQ16 = packedPreview ? 0UL :
        (ULONG)(((ULONGLONG)(RawHeight - 2UL) << 16) /
            AV_COLOR_HEIGHT);
    /*
     * Android factory AWB uses a calibrated neutral locus instead of
     * gray-world. Match the measured RAW R/G and B/G ratios to the exact
     * OV8856 reference points, then rate-limit both AWB and CCM movement.
     */
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
        rgQ14 = AvFactoryFrontAwbRefQ14[2][0];
        bgQ14 = AvFactoryFrontAwbRefQ14[2][1];
    }
    InterlockedExchange(&Device->AwbRgQ14, (LONG)rgQ14);
    InterlockedExchange(&Device->AwbBgQ14, (LONG)bgQ14);
    bestDistance = (ULONGLONG)-1;
    for (reference = 0;
         reference < AV_FACTORY_AWB_REFS;
         ++reference) {
        LONGLONG deltaR =
            (LONGLONG)rgQ14 -
            AvFactoryFrontAwbRefQ14[reference][0];
        LONGLONG deltaB =
            (LONGLONG)bgQ14 -
            AvFactoryFrontAwbRefQ14[reference][1];
        ULONGLONG distance =
            (ULONGLONG)(deltaR * deltaR) +
            (ULONGLONG)(deltaB * deltaB);

        if (distance < bestDistance) {
            bestDistance = distance;
            bestReference = reference;
        }
    }
    gainR = AvFactoryFrontMwbGainQ10[bestReference][0];
    gainB = AvFactoryFrontMwbGainQ10[bestReference][1];
    /* Same-scene Android neutral-axis calibration (front OV8856). */
    gainR = (gainR * 967UL + 500UL) / 1000UL;
    gainB = (gainB * 927UL + 500UL) / 1000UL;
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
        AvFrontCcmPositionForReference(bestReference);
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
            ((LONGLONG)AvFactoryFrontCcmQ12[lowerMatrix][x] *
                 (1024UL - matrixWeightQ10) +
             (LONGLONG)AvFactoryFrontCcmQ12[lowerMatrix + 1UL][x] *
                 matrixWeightQ10) >> 10);
    }
    black = AV_FACTORY_BLACK_LEVEL;
    white = 1023UL;
    *DynamicBlack = black;
    *DynamicWhite = white;
    InterlockedExchange(
        &Device->PercentileBlack, AV_FACTORY_BLACK_LEVEL);
    InterlockedExchange(&Device->PercentileWhite, 1023);
    InterlockedExchange(&Device->StableBlack, AV_FACTORY_BLACK_LEVEL);
    InterlockedExchange(&Device->StableWhite, 1023);
    InterlockedExchange(&Device->StableGainR, (LONG)gainR);
    InterlockedExchange(&Device->StableGainB, (LONG)gainB);
    InterlockedExchange(&Device->StableLiftQ10, 0);
    InterlockedExchange(&Device->ToneLiftPasses, 0);
    InterlockedExchange(
        &Device->StableAwbRefQ10, (LONG)(bestReference << 10));
    for (x = 0; x < AV_COLOR_WIDTH; ++x) {
        Device->SourceXMap[x] = (USHORT)(
            ((ULONGLONG)x * sourceStepXQ16) >> 16);
        Device->SourceXMap[x] &= (USHORT)~1U;
    }
    for (y = 0; y < AV_COLOR_HEIGHT; ++y) {
        Device->SourceYMap[y] = (USHORT)(
            ((ULONGLONG)y * sourceStepYQ16) >> 16);
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

    if (packedPreview) {
        const ULONG *packed = (const ULONG *)Raw;

        for (y = 0; y < AV_COLOR_HEIGHT; ++y) {
            PULONG destination = Device->NoiseBuffer +
                (y * AV_COLOR_WIDTH);
            for (x = 0; x < AV_COLOR_WIDTH; ++x) {
                ULONG basePixel;
                ULONG value = packed[y * AV_COLOR_WIDTH + x];
                ULONG rawB = value & 0xFFUL;
                ULONG g0 = ((value >> 8) & 0xFFUL) << 2;
                ULONG g1 = ((value >> 16) & 0xFFUL) << 2;
                ULONG rawR = (value >> 24) & 0xFFUL;
                ULONG gIndex = (g0 + g1 + 1UL) >> 1;
                ULONG rIndex;
                ULONG bIndex;

                /*
                 * Qualcomm ANR10 on this tablet uses a two-pixel chroma
                 * bilateral filter and an 8-bit Y-derivative threshold of
                 * 20.  Fuse its cheapest useful equivalent into the Bayer
                 * decode: preserve this cell's green/luma and blend only R/B
                 * with the adjacent cell when the local green edge is below
                 * that exact Android threshold.  No previous-frame RGB read
                 * and no second full-frame pass are needed.
                 */
                if (denoiseDivisor != 0UL && x + 1UL < AV_COLOR_WIDTH) {
                    ULONG nextValue = packed[y * AV_COLOR_WIDTH + x + 1UL];
                    ULONG nextG0 = (nextValue >> 8) & 0xFFUL;
                    ULONG nextG1 = (nextValue >> 16) & 0xFFUL;
                    ULONG currentG8 = ((g0 + g1 + 4UL) >> 3);
                    ULONG nextG8 = (nextG0 + nextG1 + 1UL) >> 1;
                    ULONG greenDelta = currentG8 > nextG8 ?
                        currentG8 - nextG8 : nextG8 - currentG8;

                    if (greenDelta <= AV_DENOISE_SPATIAL_EDGE_RAW8) {
                        ULONG nextB = nextValue & 0xFFUL;
                        ULONG nextR = (nextValue >> 24) & 0xFFUL;

                        if (denoiseDivisor == 2UL) {
                            rawR = (rawR + nextR + 1UL) >> 1;
                            rawB = (rawB + nextB + 1UL) >> 1;
                        } else {
                            rawR = (rawR * 3UL + nextR + 2UL) >> 2;
                            rawB = (rawB * 3UL + nextB + 2UL) >> 2;
                        }
                        denoiseSpatialAccepted += 1UL;
                    }
                }
                rIndex = rawR << 2;
                bIndex = rawB << 2;

                basePixel = AvPackFrontFactoryPixel(
                    Device,
                    rIndex,
                    gIndex,
                    bIndex,
                    y * AV_COLOR_WIDTH + x,
                    gainR,
                    gainB,
                    matrix);
                if ((x & 63UL) == 0UL) {
                    androidHistogram[AvFrontPixelLuma(basePixel)] += 1UL;
                    androidSampleCount += 1UL;
                }
                basePixel = AvFrontApplyAndroidTonePixel(
                    basePixel, androidDeltaLut);
                basePixel = AvFrontApplyFastTemporalChroma(
                    Device,
                    basePixel,
                    (y * 2UL + 1UL) * AV_OUTPUT_WIDTH +
                        ((AV_COLOR_WIDTH - 1UL - x) * 2UL + 1UL),
                    denoiseDivisor,
                    &denoiseTemporalAccepted);
                destination[AV_COLOR_WIDTH - 1UL - x] = basePixel;
            }
        }
    } else {
        for (y = 0; y < AV_COLOR_HEIGHT; ++y) {
            ULONG sourceY = Device->SourceYMap[y];
            const USHORT *row0 = Raw + sourceY * RawWidth;
            const USHORT *row1 = row0 + RawWidth;
            PULONG destination = Device->NoiseBuffer +
                (y * AV_COLOR_WIDTH);

            for (x = 0; x < AV_COLOR_WIDTH; ++x) {
                ULONG basePixel;
                ULONG sourceX = Device->SourceXMap[x];
                USHORT r, g, b;
                ULONG rIndex;
                ULONG gIndex;
                ULONG bIndex;
                b = row0[sourceX];
                g = AvAverage2(
                    row0[sourceX + 1UL], row1[sourceX]);
                r = row1[sourceX + 1UL];
                /*
                 * Interpolate R/B to the centre of this BGGR Bayer cell.
                 * Using the two corner photosites directly shifts red and
                 * blue in opposite directions and creates cyan/magenta rims.
                 */
                if (sourceX >= 2UL && sourceY >= 2UL &&
                    sourceX + 2UL < RawWidth &&
                    sourceY + 2UL < RawHeight) {
                    const USHORT *rowUpR = Raw +
                        (sourceY - 1UL) * RawWidth;
                    const USHORT *rowDownB = Raw +
                        (sourceY + 2UL) * RawWidth;
                    r = (USHORT)(((ULONG)r + row1[sourceX - 1UL] +
                        rowUpR[sourceX + 1UL] +
                        rowUpR[sourceX - 1UL] + 2UL) >> 2);
                    b = (USHORT)(((ULONG)b + row0[sourceX + 2UL] +
                        rowDownB[sourceX] +
                        rowDownB[sourceX + 2UL] + 2UL) >> 2);
                }
                if (denoiseDivisor != 0UL &&
                    sourceX + 3UL < RawWidth) {
                    ULONG nextG = AvAverage2(
                        row0[sourceX + 3UL], row1[sourceX + 2UL]);
                    ULONG greenDelta = (ULONG)g > nextG ?
                        (ULONG)g - nextG : nextG - (ULONG)g;

                    if (greenDelta <=
                        AV_DENOISE_SPATIAL_EDGE_RAW8 * 4UL) {
                        ULONG nextB = row0[sourceX + 2UL];
                        ULONG nextR = row1[sourceX + 3UL];

                        if (denoiseDivisor == 2UL) {
                            r = (USHORT)(((ULONG)r + nextR + 1UL) >> 1);
                            b = (USHORT)(((ULONG)b + nextB + 1UL) >> 1);
                        } else {
                            r = (USHORT)(((ULONG)r * 3UL + nextR + 2UL) >> 2);
                            b = (USHORT)(((ULONG)b * 3UL + nextB + 2UL) >> 2);
                        }
                        denoiseSpatialAccepted += 1UL;
                    }
                }
                rIndex = r < AV_RAW_LUT_SIZE ?
                    (ULONG)r : AV_RAW_LUT_SIZE - 1UL;
                gIndex = g < AV_RAW_LUT_SIZE ?
                    (ULONG)g : AV_RAW_LUT_SIZE - 1UL;
                bIndex = b < AV_RAW_LUT_SIZE ?
                    (ULONG)b : AV_RAW_LUT_SIZE - 1UL;
                basePixel = AvPackFrontFactoryPixel(
                    Device,
                    rIndex,
                    gIndex,
                    bIndex,
                    y * AV_COLOR_WIDTH + x,
                    gainR,
                    gainB,
                    matrix);
                if ((x & 63UL) == 0UL) {
                    androidHistogram[AvFrontPixelLuma(basePixel)] += 1UL;
                    androidSampleCount += 1UL;
                }
                basePixel = AvFrontApplyAndroidTonePixel(
                    basePixel, androidDeltaLut);
                basePixel = AvFrontApplyFastTemporalChroma(
                    Device,
                    basePixel,
                    (y * 2UL + 1UL) * AV_OUTPUT_WIDTH +
                        ((AV_COLOR_WIDTH - 1UL - x) * 2UL + 1UL),
                    denoiseDivisor,
                    &denoiseTemporalAccepted);
                destination[AV_COLOR_WIDTH - 1UL - x] = basePixel;
            }
        }
    }
    AvFrontUpdateAndroidLtm(
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

        if (p95 <= AV_FRONT_DENOISE_FULL_P95) {
            noiseQ10 = 1024UL;
        } else if (p95 >= AV_FRONT_DENOISE_OFF_P95) {
            noiseQ10 = 0UL;
        } else {
            noiseQ10 =
                ((AV_FRONT_DENOISE_OFF_P95 - p95) * 1024UL + 48UL) /
                (AV_FRONT_DENOISE_OFF_P95 - AV_FRONT_DENOISE_FULL_P95);
        }
        AvFrontRefineLuma(Device, noiseQ10);
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
    AvFrontUpscaleWithBayerDetail(
        Device,
        Raw,
        RawWidth,
        RawHeight,
        Device->RefineBuffer != NULL ?
            Device->RefineBuffer : Device->NoiseBuffer,
        Rgb,
        TRUE);
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
}

static VOID
AvFrontConvertRaw(_In_opt_ PVOID ConsumerContext,
                  _In_reads_(Width * Height) const USHORT *Raw,
                  _In_ ULONG Width,
                  _In_ ULONG Height)
{
    PAV_DEVICE_CONTEXT device = (PAV_DEVICE_CONTEXT)ConsumerContext;
    ULONG rgbNonBlack = 0;
    ULONG rgbMinimum = 255;
    ULONG rgbMaximum = 0;
    ULONGLONG rgbSum = 0;
    ULONG rgbSampleCount = 0;
    ULONG dynamicBlack = 0;
    ULONG dynamicWhite = 0;
    ULONG x;
    ULONG y;
    LARGE_INTEGER frequency;
    LARGE_INTEGER started;
    LARGE_INTEGER stopped;
    ULONGLONG elapsedTicks;
    ULONGLONG elapsedUs;
    LONG previousMaximum;

    if (device != NULL && device->CaptureBuffer != NULL &&
        ((Width == NABU_FRONT_WIDTH && Height == NABU_FRONT_HEIGHT) ||
         (Width == NABU_FRONT_PREVIEW_RAW_WIDTH &&
          Height == NABU_FRONT_PREVIEW_RAW_HEIGHT))) {
        started = KeQueryPerformanceCounter(&frequency);
        AvBayerBggrToRgb32Scaled(
            device,
            Raw,
            device->CaptureBuffer,
            Width,
            Height,
            AV_OUTPUT_WIDTH,
            AV_OUTPUT_HEIGHT,
            &dynamicBlack,
            &dynamicWhite);
        stopped = KeQueryPerformanceCounter(NULL);
        elapsedTicks =
            (ULONGLONG)(stopped.QuadPart - started.QuadPart);
        elapsedUs = frequency.QuadPart > 0 ?
            (elapsedTicks * 1000000ULL) /
                (ULONGLONG)frequency.QuadPart :
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

        for (y = 0; y < AV_OUTPUT_HEIGHT; y += 4UL) {
            for (x = 0; x < AV_OUTPUT_WIDTH; x += 4UL) {
                ULONG pixel = device->CaptureBuffer[
                    y * AV_OUTPUT_WIDTH + x];
                ULONG r = (pixel >> 16) & 0xFFUL;
                ULONG g = (pixel >> 8) & 0xFFUL;
                ULONG b = pixel & 0xFFUL;
                ULONG luminance = (r + (g * 2UL) + b) / 4UL;

                if ((pixel & 0x00FFFFFFUL) != 0) {
                    rgbNonBlack += 1;
                }
                if (luminance < rgbMinimum) rgbMinimum = luminance;
                if (luminance > rgbMaximum) rgbMaximum = luminance;
                rgbSum += luminance;
                rgbSampleCount += 1;
            }
        }
        InterlockedExchange(&device->RgbNonBlack, (LONG)rgbNonBlack);
        InterlockedExchange(&device->RgbMinimum, (LONG)rgbMinimum);
        InterlockedExchange(&device->RgbMaximum, (LONG)rgbMaximum);
        InterlockedExchange(
            &device->RgbMean,
            rgbSampleCount != 0 ?
                (LONG)(rgbSum / rgbSampleCount) : 0);
        InterlockedExchange(
            &device->RgbFirst,
            (LONG)device->CaptureBuffer[0]);
        InterlockedExchange(
            &device->RgbCenter,
            (LONG)device->CaptureBuffer[
                (AV_OUTPUT_HEIGHT / 2UL) * AV_OUTPUT_WIDTH +
                (AV_OUTPUT_WIDTH / 2UL)]);
        InterlockedExchange(
            &device->DynamicBlack, (LONG)dynamicBlack);
        InterlockedExchange(
            &device->DynamicWhite, (LONG)dynamicWhite);
        InterlockedExchange(&device->ConversionCompleted, 1);
    }
}

static ULONG
AvFrontRawSignature(
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

static VOID
AvFrontRawConsumer(_In_opt_ PVOID ConsumerContext,
                   _In_reads_(Width * Height) const USHORT *Raw,
                   _In_ ULONG Width,
                   _In_ ULONG Height)
{
    PAV_DEVICE_CONTEXT device = (PAV_DEVICE_CONTEXT)ConsumerContext;
    ULONG pixelCount;
    ULONG signature;
    BOOLEAN enqueueConversion = FALSE;

    if (device == NULL || Raw == NULL || device->RawBuffer == NULL ||
        device->PendingRawBuffer == NULL ||
        (device->ConversionWorkItem == NULL &&
         device->ConversionThreadHandle == NULL) ||
        InterlockedCompareExchange(&device->Removing, 0, 0) != 0 ||
        !((Width == NABU_FRONT_WIDTH && Height == NABU_FRONT_HEIGHT) ||
          (Width == NABU_FRONT_PREVIEW_RAW_WIDTH &&
           Height == NABU_FRONT_PREVIEW_RAW_HEIGHT))) {
        return;
    }
    pixelCount = Width * Height;
    signature = AvFrontRawSignature(Raw, pixelCount);
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
AvProcessConversionQueue(_Inout_ PAV_DEVICE_CONTEXT device)
{
    PULONG oldBuffer;
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
        InterlockedExchange(&device->RawMinimum, 0);
        InterlockedExchange(&device->RawMaximum, 0);
        InterlockedExchange(&device->RawMean, 0);
        InterlockedExchange(&device->RawNonZero, 0);
        InterlockedExchange(&device->RawSampleCount, 0);
        InterlockedExchange(&device->RawFirst0, 0);
        InterlockedExchange(&device->RawFirst1, 0);
        InterlockedExchange(&device->RgbNonBlack, 0);
        InterlockedExchange(&device->RgbMinimum, 0);
        InterlockedExchange(&device->RgbMaximum, 0);
        InterlockedExchange(&device->RgbMean, 0);
        InterlockedExchange(&device->RgbFirst, 0);
        InterlockedExchange(&device->RgbCenter, 0);
        InterlockedExchange(&device->DynamicBlack, 0);
        InterlockedExchange(&device->DynamicWhite, 0);

        AvFrontConvertRaw(device, device->RawBuffer, width, height);
        if (InterlockedCompareExchange(
                &device->ConversionCompleted, 0, 0) != 0 &&
            InterlockedCompareExchange(&device->RawNonZero, 0, 0) > 100 &&
            InterlockedCompareExchange(&device->RawMaximum, 0, 0) >
                InterlockedCompareExchange(&device->RawMinimum, 0, 0) &&
            InterlockedCompareExchange(&device->RgbNonBlack, 0, 0) > 100) {
            oldBuffer = (PULONG)InterlockedExchangePointer(
                (PVOID volatile *)&device->RgbBuffer,
                device->CaptureBuffer);
            device->CaptureBuffer = oldBuffer;
            InterlockedIncrement(&device->PublishedGeneration);
            InterlockedExchange(&device->PublishedReal, 1);
            InterlockedExchange(&device->CaptureState, 2);
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
AvConversionWorkItem(_In_ WDFWORKITEM WorkItem)
{
    PAV_WORK_CONTEXT workContext = AvGetWorkContext(WorkItem);

    if (workContext == NULL || workContext->DeviceContext == NULL) {
        return;
    }
    AvProcessConversionQueue(
        (PAV_DEVICE_CONTEXT)workContext->DeviceContext);
}

static VOID
AvConversionThread(_In_ PVOID StartContext)
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
        AvProcessConversionQueue(device);
    }
    InterlockedExchange(&device->ConversionThreadRunning, 0);
    KeSetEvent(&device->ConversionExitEvent, IO_NO_INCREMENT, FALSE);
    PsTerminateSystemThread(STATUS_SUCCESS);
}

static const GUID AvFilterReferenceGuid =
    { 0x1c737e6b, 0xd690, 0x4662,
      { 0x89, 0xad, 0xeb, 0xd7, 0x18, 0xc4, 0x8b, 0x7c } };
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
    AvDeviceSetPower,
    NULL
};

static const KSDEVICE_DESCRIPTOR AvDeviceDescriptor = {
    &AvDeviceDispatch,
    RTL_NUMBER_OF(AvFilterDescriptors),
    AvFilterDescriptors,
    KSDEVICE_DESCRIPTOR_VERSION
};

VOID
AvBackendIdleTimerCallback(_In_ WDFTIMER Timer)
{
    PAV_IDLE_TIMER_CONTEXT timerContext =
        AvGetIdleTimerContext(Timer);
    PAV_DEVICE_CONTEXT device =
        timerContext != NULL ?
            (PAV_DEVICE_CONTEXT)timerContext->DeviceContext :
            NULL;

    if (device == NULL) {
        return;
    }
    InterlockedIncrement(&device->BackendIdleFireCount);
    if (InterlockedCompareExchange(&device->Removing, 0, 0) != 0 ||
        InterlockedCompareExchange(&device->ActivePinCount, 0, 0) != 0 ||
        InterlockedCompareExchange(
            &device->HardwareCaptureInProgress, 0, 0) != 0 ||
        device->WdfDevice == NULL) {
        InterlockedIncrement(&device->BackendIdleSkipCount);
        return;
    }

    InterlockedIncrement(&device->BackendIdleReleaseCount);
    NabuFrontBackendRelease(device->WdfDevice);
    AvWriteCaptureDiagnostics(
        device,
        (ULONG)InterlockedCompareExchange(
            &device->LastCaptureState, 0, 0));
}

static NTSTATUS
AvDeviceAdd(_In_ PKSDEVICE Device)
{
    PAV_DEVICE_CONTEXT avContext = NULL;
    PAV_IDLE_TIMER_CONTEXT idleTimerContext;
    WDFDEVICE wdfDevice = NULL;
    WDF_TIMER_CONFIG idleTimerConfig;
    WDF_OBJECT_ATTRIBUTES idleTimerAttributes;
    WDF_WORKITEM_CONFIG workConfig;
    WDF_OBJECT_ATTRIBUTES workAttributes;
    PAV_WORK_CONTEXT workContext;
    HANDLE diagnosticKey = NULL;
    HANDLE conversionThread = NULL;
    OBJECT_ATTRIBUTES threadAttributes;
    NTSTATUS threadStatus;
    NTSTATUS status;

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
    InterlockedExchange(
        &avContext->DevicePowerState,
        (LONG)Device->DevicePowerState);
    InterlockedExchange(
        &avContext->LastBackendAcquireStatus,
        (LONG)STATUS_NOT_SUPPORTED);
    status = IoOpenDeviceRegistryKey(
        Device->PhysicalDeviceObject,
        PLUGPLAY_REGKEY_DEVICE,
        KEY_SET_VALUE,
        &diagnosticKey);
    if (NT_SUCCESS(status)) {
        avContext->DiagnosticKey = diagnosticKey;
    }
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

    status = NabuFrontBackendInitialize(wdfDevice);
    if (!NT_SUCCESS(status)) {
        WdfObjectDelete(wdfDevice);
        if (avContext->DiagnosticKey != NULL) {
            ZwClose(avContext->DiagnosticKey);
        }
        ExFreePoolWithTag(avContext, AV_POOL_TAG);
        return status;
    }
    /*
     * A root frontend has no ACPI _PLD of its own. Mirror the physical
     * sensor's location and container properties before user mode consumes
     * the camera interface, so Windows can apply its internal-front-camera
     * orientation policy.
     */
    (VOID)NabuFrontBackendPublishPhysicalProperties(
        wdfDevice, Device->PhysicalDeviceObject);

    avContext->RgbBuffer = (PULONG)ExAllocatePool2(
        POOL_FLAG_NON_PAGED, AV_RGB_FRAME_BYTES, AV_POOL_TAG);
    if (avContext->RgbBuffer == NULL) {
        NabuFrontBackendCleanup(wdfDevice);
        WdfObjectDelete(wdfDevice);
        if (avContext->DiagnosticKey != NULL) {
            ZwClose(avContext->DiagnosticKey);
        }
        ExFreePoolWithTag(avContext, AV_POOL_TAG);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    avContext->CaptureBuffer = (PULONG)ExAllocatePool2(
        POOL_FLAG_NON_PAGED, AV_RGB_FRAME_BYTES, AV_POOL_TAG);
    if (avContext->CaptureBuffer == NULL) {
        ExFreePoolWithTag(avContext->RgbBuffer, AV_POOL_TAG);
        avContext->RgbBuffer = NULL;
        NabuFrontBackendCleanup(wdfDevice);
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
        NabuFrontBackendCleanup(wdfDevice);
        WdfObjectDelete(wdfDevice);
        if (avContext->DiagnosticKey != NULL) {
            ZwClose(avContext->DiagnosticKey);
        }
        ExFreePoolWithTag(avContext, AV_POOL_TAG);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    /*
     * The luma refinement pass needs a second colour grid, because the upscale
     * reads the refined copy while the native-detail path keeps the original,
     * plus the previous frame's refined luma for the temporal tap.  Neither is
     * required: without both the pipeline simply runs without the new pass.
     */
    avContext->RefineBuffer = (PULONG)ExAllocatePool2(
        POOL_FLAG_NON_PAGED,
        AV_COLOR_WIDTH * AV_COLOR_HEIGHT * sizeof(ULONG),
        AV_POOL_TAG);
    avContext->LumaHistory = (PUSHORT)ExAllocatePool2(
        POOL_FLAG_NON_PAGED,
        AV_COLOR_WIDTH * AV_COLOR_HEIGHT * sizeof(USHORT),
        AV_POOL_TAG);
    if (avContext->LumaHistory == NULL) {
        ExFreePoolWithTag(avContext->RefineBuffer, AV_POOL_TAG);
        avContext->RefineBuffer = NULL;
    } else {
        RtlZeroMemory(
            avContext->LumaHistory,
            AV_COLOR_WIDTH * AV_COLOR_HEIGHT * sizeof(USHORT));
    }
    avContext->RawBuffer = (PUSHORT)ExAllocatePool2(
        POOL_FLAG_NON_PAGED, NABU_FRONT_FRAME_BYTES, AV_POOL_TAG);
    if (avContext->RawBuffer == NULL) {
        ExFreePoolWithTag(avContext->RefineBuffer, AV_POOL_TAG);
        avContext->RefineBuffer = NULL;
        ExFreePoolWithTag(avContext->LumaHistory, AV_POOL_TAG);
        avContext->LumaHistory = NULL;
        ExFreePoolWithTag(avContext->NoiseBuffer, AV_POOL_TAG);
        avContext->NoiseBuffer = NULL;
        ExFreePoolWithTag(avContext->CaptureBuffer, AV_POOL_TAG);
        avContext->CaptureBuffer = NULL;
        ExFreePoolWithTag(avContext->RgbBuffer, AV_POOL_TAG);
        avContext->RgbBuffer = NULL;
        NabuFrontBackendCleanup(wdfDevice);
        WdfObjectDelete(wdfDevice);
        if (avContext->DiagnosticKey != NULL) {
            ZwClose(avContext->DiagnosticKey);
        }
        ExFreePoolWithTag(avContext, AV_POOL_TAG);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    avContext->PendingRawBuffer = (PUSHORT)ExAllocatePool2(
        POOL_FLAG_NON_PAGED, NABU_FRONT_FRAME_BYTES, AV_POOL_TAG);
    if (avContext->PendingRawBuffer == NULL) {
        ExFreePoolWithTag(avContext->RawBuffer, AV_POOL_TAG);
        avContext->RawBuffer = NULL;
        ExFreePoolWithTag(avContext->RefineBuffer, AV_POOL_TAG);
        avContext->RefineBuffer = NULL;
        ExFreePoolWithTag(avContext->LumaHistory, AV_POOL_TAG);
        avContext->LumaHistory = NULL;
        ExFreePoolWithTag(avContext->NoiseBuffer, AV_POOL_TAG);
        avContext->NoiseBuffer = NULL;
        ExFreePoolWithTag(avContext->CaptureBuffer, AV_POOL_TAG);
        avContext->CaptureBuffer = NULL;
        ExFreePoolWithTag(avContext->RgbBuffer, AV_POOL_TAG);
        avContext->RgbBuffer = NULL;
        NabuFrontBackendCleanup(wdfDevice);
        WdfObjectDelete(wdfDevice);
        if (avContext->DiagnosticKey != NULL) {
            ZwClose(avContext->DiagnosticKey);
        }
        ExFreePoolWithTag(avContext, AV_POOL_TAG);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    ExInitializeFastMutex(&avContext->RawQueueLock);
    /*
     * Keep startup neutral until the first validated sensor frame arrives.
     * The former diagnostic colour bars leaked into Camera during cold open.
     */
    {
        ULONG pixel;
        for (pixel = 0; pixel < AV_OUTPUT_WIDTH * AV_OUTPUT_HEIGHT; ++pixel) {
            avContext->RgbBuffer[pixel] = 0xFF000000UL;
            avContext->CaptureBuffer[pixel] = 0xFF000000UL;
            avContext->NoiseBuffer[pixel] = 0xFF000000UL;
        }
    }
    WDF_TIMER_CONFIG_INIT(
        &idleTimerConfig, AvBackendIdleTimerCallback);
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
        ExFreePoolWithTag(avContext->RefineBuffer, AV_POOL_TAG);
        avContext->RefineBuffer = NULL;
        ExFreePoolWithTag(avContext->LumaHistory, AV_POOL_TAG);
        avContext->LumaHistory = NULL;
        ExFreePoolWithTag(avContext->NoiseBuffer, AV_POOL_TAG);
        avContext->NoiseBuffer = NULL;
        ExFreePoolWithTag(avContext->CaptureBuffer, AV_POOL_TAG);
        avContext->CaptureBuffer = NULL;
        ExFreePoolWithTag(avContext->RgbBuffer, AV_POOL_TAG);
        avContext->RgbBuffer = NULL;
        NabuFrontBackendCleanup(wdfDevice);
        WdfObjectDelete(wdfDevice);
        if (avContext->DiagnosticKey != NULL) {
            ZwClose(avContext->DiagnosticKey);
        }
        ExFreePoolWithTag(avContext, AV_POOL_TAG);
        return status;
    }
    idleTimerContext =
        AvGetIdleTimerContext(avContext->BackendIdleTimer);
    idleTimerContext->DeviceContext = avContext;
    WDF_WORKITEM_CONFIG_INIT(&workConfig, AvCaptureWorkItem);
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
        ExFreePoolWithTag(avContext->PendingRawBuffer, AV_POOL_TAG);
        avContext->PendingRawBuffer = NULL;
        ExFreePoolWithTag(avContext->RawBuffer, AV_POOL_TAG);
        avContext->RawBuffer = NULL;
        ExFreePoolWithTag(avContext->RefineBuffer, AV_POOL_TAG);
        avContext->RefineBuffer = NULL;
        ExFreePoolWithTag(avContext->LumaHistory, AV_POOL_TAG);
        avContext->LumaHistory = NULL;
        ExFreePoolWithTag(avContext->NoiseBuffer, AV_POOL_TAG);
        avContext->NoiseBuffer = NULL;
        ExFreePoolWithTag(avContext->CaptureBuffer, AV_POOL_TAG);
        avContext->CaptureBuffer = NULL;
        ExFreePoolWithTag(avContext->RgbBuffer, AV_POOL_TAG);
        avContext->RgbBuffer = NULL;
        NabuFrontBackendCleanup(wdfDevice);
        WdfObjectDelete(wdfDevice);
        if (avContext->DiagnosticKey != NULL) {
            ZwClose(avContext->DiagnosticKey);
        }
        ExFreePoolWithTag(avContext, AV_POOL_TAG);
        return status;
    }
    workContext = AvGetWorkContext(avContext->CaptureWorkItem);
    workContext->DeviceContext = avContext;
    WDF_WORKITEM_CONFIG_INIT(&workConfig, AvConversionWorkItem);
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
        ExFreePoolWithTag(avContext->PendingRawBuffer, AV_POOL_TAG);
        avContext->PendingRawBuffer = NULL;
        ExFreePoolWithTag(avContext->RawBuffer, AV_POOL_TAG);
        avContext->RawBuffer = NULL;
        ExFreePoolWithTag(avContext->RefineBuffer, AV_POOL_TAG);
        avContext->RefineBuffer = NULL;
        ExFreePoolWithTag(avContext->LumaHistory, AV_POOL_TAG);
        avContext->LumaHistory = NULL;
        ExFreePoolWithTag(avContext->NoiseBuffer, AV_POOL_TAG);
        avContext->NoiseBuffer = NULL;
        ExFreePoolWithTag(avContext->CaptureBuffer, AV_POOL_TAG);
        avContext->CaptureBuffer = NULL;
        ExFreePoolWithTag(avContext->RgbBuffer, AV_POOL_TAG);
        avContext->RgbBuffer = NULL;
        NabuFrontBackendCleanup(wdfDevice);
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
        AvConversionThread,
        avContext);
    if (NT_SUCCESS(threadStatus)) {
        avContext->ConversionThreadHandle = conversionThread;
    } else {
        InterlockedExchange(&avContext->ConversionThreadRunning, 0);
    }
    InterlockedExchange(
        &avContext->LastCaptureStatus, (LONG)STATUS_NOT_SUPPORTED);
    AvWriteCaptureDiagnostics(avContext, 0);

    avContext->WdfDevice = wdfDevice;
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
    (VOID)AvPublishInterfaceProperties(context);
    AvWriteCaptureDiagnostics(
        context,
        (ULONG)InterlockedCompareExchange(
            &context->LastCaptureState, 0, 0));
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
    if (context->RefineBuffer != NULL) {
        ExFreePoolWithTag(context->RefineBuffer, AV_POOL_TAG);
        context->RefineBuffer = NULL;
    }
    if (context->LumaHistory != NULL) {
        ExFreePoolWithTag(context->LumaHistory, AV_POOL_TAG);
        context->LumaHistory = NULL;
    }
    if (context->NoiseBuffer != NULL) {
        ExFreePoolWithTag(context->NoiseBuffer, AV_POOL_TAG);
        context->NoiseBuffer = NULL;
    }
    if (context->WdfDevice != NULL) {
        NabuFrontBackendCleanup(context->WdfDevice);
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

static VOID
AvDeviceSetPower(_In_ PKSDEVICE Device,
                 _In_ PIRP Irp,
                 _In_ DEVICE_POWER_STATE To,
                 _In_ DEVICE_POWER_STATE From)
{
    PAV_DEVICE_CONTEXT context =
        Device != NULL ? (PAV_DEVICE_CONTEXT)Device->Context : NULL;

    UNREFERENCED_PARAMETER(Irp);
    if (context == NULL) {
        return;
    }
    InterlockedIncrement(&context->SetPowerCount);
    InterlockedExchange(&context->LastPowerFrom, (LONG)From);
    InterlockedExchange(&context->LastPowerTo, (LONG)To);
    InterlockedExchange(&context->DevicePowerState, (LONG)To);
    AvWriteCaptureDiagnostics(
        context,
        (ULONG)InterlockedCompareExchange(
            &context->LastCaptureState, 0, 0));
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
    InterlockedExchange(
        &deviceContext->PinCreateProcessId,
        (LONG)HandleToULong(PsGetCurrentProcessId()));
    InterlockedExchange(
        &deviceContext->PinCreateThreadId,
        (LONG)HandleToULong(PsGetCurrentThreadId()));
    InterlockedIncrement(&deviceContext->PinCreateCount);
    if (InterlockedIncrement(&deviceContext->ActivePinCount) == 1) {
        /* Never expose the previous session's final frame on a new open. */
        InterlockedExchange(&deviceContext->PublishedReal, 0);
        InterlockedExchange(&deviceContext->CaptureAttempted, 0);
        InterlockedExchange(
            &deviceContext->LastCaptureStatus,
            (LONG)STATUS_NOT_SUPPORTED);
    }
    if (deviceContext->BackendIdleTimer != NULL) {
        if (WdfTimerStop(
                deviceContext->BackendIdleTimer, TRUE)) {
            InterlockedIncrement(
                &deviceContext->BackendIdleCancelCount);
        }
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
    }
    if (pinContext->Timer != NULL) {
        PAV_TIMER_CONTEXT timerContext =
            AvGetTimerContext(pinContext->Timer);

        timerContext->PinContext = NULL;
        WdfObjectDelete(pinContext->Timer);
        pinContext->Timer = NULL;
    }
    if (pinContext->Device != NULL) {
        PAV_DEVICE_CONTEXT device = pinContext->Device;
        ULONG captureState = (ULONG)InterlockedCompareExchange(
            &device->LastCaptureState, 0, 0);

        InterlockedIncrement(&device->PinCloseCount);
        if (InterlockedDecrement(&device->ActivePinCount) == 0 &&
            device->BackendIdleTimer != NULL) {
            InterlockedIncrement(
                &device->BackendIdleScheduleCount);
            WdfTimerStart(
                device->BackendIdleTimer,
                WDF_REL_TIMEOUT_IN_MS(
                    AV_BACKEND_IDLE_HOLD_MS));
        }
        AvWriteCaptureDiagnostics(device, captureState);
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
    PAV_PIN_CONTEXT pinContext =
        Pin != NULL ? (PAV_PIN_CONTEXT)Pin->Context : NULL;
    PAV_DEVICE_CONTEXT device =
        pinContext != NULL ? pinContext->Device : NULL;
    PKS_DATAFORMAT_VIDEOINFOHEADER connection;
    BOOLEAN outputRgb24;
    ULONG requiredBytes;
    USHORT requiredBits;
    NTSTATUS status = STATUS_NO_MATCH;

    UNREFERENCED_PARAMETER(OldFormat);
    UNREFERENCED_PARAMETER(OldAttributes);
    UNREFERENCED_PARAMETER(Attributes);

    UNREFERENCED_PARAMETER(DataRange);
    if (device != NULL) {
        InterlockedIncrement(&device->SetDataFormatCount);
    }
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
        if (device != NULL) {
            InterlockedExchange(
                &device->LastSetDataFormatStatus, (LONG)status);
        }
        return status;
    }
    connection =
        (PKS_DATAFORMAT_VIDEOINFOHEADER)Pin->ConnectionFormat;
    outputRgb24 =
        IsEqualGUIDAligned(&Pin->ConnectionFormat->SubFormat,
                           &AvSubtypeRgb24);
    requiredBytes =
        outputRgb24 ? AV_RGB24_FRAME_BYTES : AV_RGB_FRAME_BYTES;
    requiredBits = outputRgb24 ? 24 : 32;
    if (device != NULL) {
        InterlockedExchange(
            &device->LastFormatWidth,
            connection->VideoInfoHeader.bmiHeader.biWidth);
        InterlockedExchange(
            &device->LastFormatHeight,
            connection->VideoInfoHeader.bmiHeader.biHeight);
        InterlockedExchange(
            &device->LastFormatIntervalLow,
            (LONG)connection->VideoInfoHeader.AvgTimePerFrame);
        InterlockedExchange(
            &device->LastFormatIntervalHigh,
            (LONG)(
                (ULONGLONG)
                    connection->VideoInfoHeader.AvgTimePerFrame >>
                32));
    }
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
        if (device != NULL) {
            InterlockedExchange(
                &device->LastSetDataFormatStatus, (LONG)status);
        }
        return status;
    }
    status = STATUS_SUCCESS;
    if (device != NULL) {
        InterlockedExchange(
            &device->LastSetDataFormatStatus, (LONG)status);
    }
    return status;
}

static NTSTATUS
AvPinSetState(_In_ PKSPIN Pin, _In_ KSSTATE To, _In_ KSSTATE From)
{
    PAV_PIN_CONTEXT context = (PAV_PIN_CONTEXT)Pin->Context;

    if (context == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }
    if (context->Device != NULL) {
        InterlockedIncrement(&context->Device->PinStateChangeCount);
        InterlockedExchange(
            &context->Device->LastKsStateFrom, (LONG)From);
        InterlockedExchange(
            &context->Device->LastKsStateTo, (LONG)To);
    }
    if (To != KSSTATE_RUN) {
        if (context->Device != NULL) {
            InterlockedIncrement(&context->Device->NonRunCount);
        }
        InterlockedExchange(&context->Running, 0);
        if (context->Timer != NULL) {
            WdfTimerStop(
                context->Timer,
                KeGetCurrentIrql() == PASSIVE_LEVEL ? TRUE : FALSE);
        }
    } else {
        if (context->Device != NULL) {
            InterlockedIncrement(&context->Device->RunCount);
            InterlockedExchange(
                &context->Device->RunProcessId,
                (LONG)HandleToULong(PsGetCurrentProcessId()));
            InterlockedExchange(
                &context->Device->RunThreadId,
                (LONG)HandleToULong(PsGetCurrentThreadId()));
            InterlockedExchange(
                &context->Device->CaptureAttempted, 1);
            InterlockedExchange(
                &context->LastDeliveredGeneration,
                InterlockedCompareExchange(
                    &context->Device->PublishedGeneration, 0, 0));
        }
        InterlockedExchange(&context->RefreshTick, 0);
        InterlockedExchange(&context->FrameReady, 1);
        InterlockedExchange(&context->Running, 1);
        WdfTimerStart(context->Timer, WDF_REL_TIMEOUT_IN_MS(1));
        if (context->Device != NULL &&
            context->Device->CaptureWorkItem != NULL &&
            InterlockedCompareExchange(
                &context->Device->RefreshQueued, 1, 0) == 0) {
            InterlockedIncrement(
                &context->Device->RefreshQueueCount);
            WdfWorkItemEnqueue(
                context->Device->CaptureWorkItem);
        }
    }
    return STATUS_SUCCESS;
}

static NTSTATUS
AvRunCapture(_In_ PAV_DEVICE_CONTEXT device)
{
    LARGE_INTEGER frequency;
    LARGE_INTEGER started;
    LARGE_INTEGER stopped;
    ULONGLONG elapsedTicks;
    ULONGLONG elapsedUs;
    LONG previousMaximum;
    NTSTATUS status;

    if (device == NULL || KeGetCurrentIrql() != PASSIVE_LEVEL) {
        return STATUS_INVALID_DEVICE_STATE;
    }
    if (InterlockedCompareExchange(
            &device->ActivePinCount, 0, 0) == 0) {
        InterlockedExchange(&device->CaptureState, 4);
        InterlockedExchange(
            &device->LastCaptureStatus, (LONG)STATUS_CANCELLED);
        return STATUS_CANCELLED;
    }
    if (InterlockedCompareExchange(&device->Removing, 0, 0) != 0) {
        InterlockedExchange(&device->CaptureState, 3);
        return STATUS_DELETE_PENDING;
    }
    if (InterlockedCompareExchange(
            &device->HardwareCaptureInProgress, 1, 0) != 0) {
        InterlockedExchange(
            &device->LastCaptureStatus, (LONG)STATUS_DEVICE_BUSY);
        AvWriteCaptureDiagnostics(
            device,
            (ULONG)InterlockedCompareExchange(
                &device->CaptureState, 0, 0));
        return STATUS_DEVICE_BUSY;
    }
    if (InterlockedCompareExchange(
            &device->ActivePinCount, 0, 0) == 0) {
        InterlockedExchange(
            &device->HardwareCaptureInProgress, 0);
        InterlockedExchange(&device->CaptureState, 4);
        InterlockedExchange(
            &device->LastCaptureStatus, (LONG)STATUS_CANCELLED);
        return STATUS_CANCELLED;
    }
    InterlockedExchange(
        &device->CaptureProcessId,
        (LONG)HandleToULong(PsGetCurrentProcessId()));
    InterlockedExchange(
        &device->CaptureThreadId,
        (LONG)HandleToULong(PsGetCurrentThreadId()));
    InterlockedExchange(
        &device->CaptureIrql,
        (LONG)KeGetCurrentIrql());

    InterlockedExchange(&device->RawCopyAccepted, 0);
    /*
     * Keep the last accepted RGB frame visible while a refresh is in
     * progress.  A failed refresh must never replace a known-good picture
     * with the synthetic/black fallback.
     */
    InterlockedExchange(
        &device->LastCaptureStatus, (LONG)STATUS_PENDING);
    started = KeQueryPerformanceCounter(&frequency);
    status = NabuFrontBackendCapture(
        device->WdfDevice,
        AvFrontRawConsumer,
        device);
    stopped = KeQueryPerformanceCounter(NULL);
    elapsedTicks = (ULONGLONG)(stopped.QuadPart - started.QuadPart);
    elapsedUs = frequency.QuadPart > 0 ?
        (elapsedTicks * 1000000ULL) /
            (ULONGLONG)frequency.QuadPart :
        0;
    if (elapsedUs > MAXLONG) {
        elapsedUs = MAXLONG;
    }
    InterlockedExchange(
        &device->LastCaptureDurationUs, (LONG)elapsedUs);
    previousMaximum = InterlockedCompareExchange(
        &device->MaximumCaptureDurationUs, 0, 0);
    while ((LONG)elapsedUs > previousMaximum) {
        LONG observed = InterlockedCompareExchange(
            &device->MaximumCaptureDurationUs,
            (LONG)elapsedUs,
            previousMaximum);
        if (observed == previousMaximum) {
            break;
        }
        previousMaximum = observed;
    }
    if (NT_SUCCESS(status)) {
        InterlockedExchange(&device->CaptureState, 2);
    } else {
        InterlockedExchange(&device->CaptureState, 3);
    }
    InterlockedExchange(&device->LastCaptureStatus, (LONG)status);
    InterlockedExchange(&device->HardwareCaptureInProgress, 0);
    if (!NT_SUCCESS(status)) {
        AvWriteCaptureDiagnostics(
            device,
            (ULONG)InterlockedCompareExchange(
                &device->CaptureState, 0, 0));
    }
    return status;
}

VOID
AvCaptureWorkItem(_In_ WDFWORKITEM WorkItem)
{
    PAV_WORK_CONTEXT workContext = AvGetWorkContext(WorkItem);
    PAV_DEVICE_CONTEXT device;
    NTSTATUS status;

    if (workContext == NULL || workContext->DeviceContext == NULL) {
        return;
    }
    device = (PAV_DEVICE_CONTEXT)workContext->DeviceContext;
    if (device != NULL &&
        InterlockedCompareExchange(&device->Removing, 0, 0) == 0 &&
        InterlockedCompareExchange(&device->ActivePinCount, 0, 0) != 0) {
        InterlockedIncrement(&device->RefreshStartCount);
        InterlockedExchange(&device->CaptureState, 1);
        status = AvRunCapture(device);
        InterlockedExchange(&device->LastRefreshStatus, (LONG)status);
        if (NT_SUCCESS(status)) {
            InterlockedIncrement(&device->RefreshSuccessCount);
            InterlockedExchange(&device->RefreshFailureStreak, 0);
        } else {
            InterlockedIncrement(&device->RefreshFailureCount);
            InterlockedIncrement(&device->RefreshFailureStreak);
            AvWriteCaptureDiagnostics(
                device,
                (ULONG)InterlockedCompareExchange(
                    &device->CaptureState, 0, 0));
        }
    }
    InterlockedExchange(&device->RefreshQueued, 0);
    if (InterlockedCompareExchange(&device->Removing, 0, 0) == 0 &&
        InterlockedCompareExchange(&device->ActivePinCount, 0, 0) == 0 &&
        InterlockedCompareExchange(
            &device->HardwareCaptureInProgress, 0, 0) == 0 &&
        device->WdfDevice != NULL &&
        device->BackendIdleTimer != NULL) {
        /*
         * Close and capture-work callbacks used to issue two concurrent STOP
         * requests.  Route all idle release through the cancellable timer so
         * a quick reopen can retain the live sensor session safely.
         */
        InterlockedIncrement(&device->BackendIdleScheduleCount);
        WdfTimerStart(
            device->BackendIdleTimer,
            WDF_REL_TIMEOUT_IN_MS(AV_BACKEND_IDLE_HOLD_MS));
    }
}

VOID
AvTimerCallback(_In_ WDFTIMER Timer)
{
    PAV_TIMER_CONTEXT timerContext = AvGetTimerContext(Timer);
    PAV_PIN_CONTEXT pinContext = timerContext->PinContext;
    PAV_DEVICE_CONTEXT device;
    LONG refreshTick;

    if (pinContext == NULL || pinContext->Device == NULL) {
        return;
    }
    device = pinContext->Device;
    InterlockedIncrement(&device->TimerTickCount);
    refreshTick = InterlockedIncrement(&pinContext->RefreshTick);
    if (refreshTick >= AV_REFRESH_TICKS) {
        InterlockedExchange(&pinContext->RefreshTick, 0);
        if (InterlockedCompareExchange(
                &pinContext->Running, 0, 0) != 0 &&
            InterlockedCompareExchange(
                &pinContext->FrameReady, 0, 0) != 0) {
            KsPinAttemptProcessing(pinContext->Pin, TRUE);
        }
    }
    if (InterlockedCompareExchange(&pinContext->Running, 0, 0) == 0 ||
        InterlockedCompareExchange(&device->CaptureAttempted, 0, 0) == 0) {
        return;
    }

    if (InterlockedCompareExchange(
            &device->CaptureState, 0, 0) == 1 ||
        device->CaptureWorkItem == NULL ||
        InterlockedCompareExchange(
            &device->RefreshQueued, 1, 0) != 0) {
        InterlockedIncrement(&device->RefreshSkipCount);
        return;
    }
    if (InterlockedCompareExchange(
            &device->RefreshFailureStreak, 0, 0) >=
            AV_REFRESH_FAIL_LIMIT &&
        (InterlockedCompareExchange(
            &device->TimerTickCount, 0, 0) %
            AV_REFRESH_RECOVERY_TICKS) != 0) {
        InterlockedExchange(&device->RefreshQueued, 0);
        InterlockedIncrement(&device->RefreshSkipCount);
        return;
    }
    InterlockedIncrement(&device->RefreshQueueCount);
    WdfWorkItemEnqueue(device->CaptureWorkItem);
}

static NTSTATUS
AvPinProcess(_In_ PKSPIN Pin)
{
    PAV_PIN_CONTEXT pinContext = (PAV_PIN_CONTEXT)Pin->Context;
    PAV_DEVICE_CONTEXT device;
    PULONG rgbBuffer;
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
    InterlockedIncrement(&device->ProcessCallCount);
    rgbBuffer = (PULONG)InterlockedCompareExchangePointer(
        (PVOID volatile *)&device->RgbBuffer, NULL, NULL);
    if (rgbBuffer == NULL) {
        return STATUS_PENDING;
    }
    outputBytes = pinContext->OutputRgb24 ?
        AV_RGB24_FRAME_BYTES : AV_RGB_FRAME_BYTES;

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
        streamPointer->OffsetOut.Remaining <
            outputBytes) {
        InterlockedIncrement(&device->BufferTooSmallCount);
        KsStreamPointerUnlock(streamPointer, FALSE);
        return STATUS_BUFFER_TOO_SMALL;
    }

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
        &device->LastPresentationLow,
        (LONG)presentationTime);
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

    KsStreamPointerUnlock(streamPointer, TRUE);
    InterlockedExchange(
        &pinContext->LastDeliveredGeneration,
        InterlockedCompareExchange(
            &device->PublishedGeneration, 0, 0));
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
