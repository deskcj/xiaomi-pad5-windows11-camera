//
// NabuCamHwRear.c
//
// Physical KMDF owner for the rear OV13B10.  Hardware is touched only by the
// explicit private capture IOCTL.  The capture implementation is the proven
// Stage39 WDF common-buffer/VCM path compiled as a library.
//

#include <ntddk.h>
#include <wdf.h>
#include "NabuCamRearIoctl.h"
#include "..\NabuCamAvStreamRear\HwCapture.h"
#include "..\NabuCamAvStreamRear\RearIntegrationEncoding.h"

#define NABU_REAR_HW_BUILD_TAG 20261188UL
#define NABU_REAR_FRAME_POOL_TAG 'fRBN'
#define NABU_REAR_AE_UPDATE_FRAMES 3UL
#define NABU_REAR_AE_FAST_FRAMES 24UL
#define NABU_REAR_AE_FAST_UPDATE_FRAMES 3UL
#define NABU_REAR_STARTUP_SKIP_FRAMES 18UL
#define NABU_REAR_WARM_START_SKIP_FRAMES 3UL
#define NABU_REAR_AE_FAST_RATIO_MIN_Q10 512UL
#define NABU_REAR_AE_FAST_RATIO_MAX_Q10 2048UL
/*
 * Android OV13B10 AEC targets luma 52 in bright preview.  Inverting the
 * decoded factory gamma15 curve and the exact BLS12 offset (960/16 = 60)
 * gives a sensor-domain mean of 120, not the old hand-tuned 650.
 */
#define NABU_REAR_AE_TARGET_MEAN 120UL
#define NABU_REAR_AE_BLACK_LEVEL 60UL
#define NABU_REAR_AE_EXPOSURE_INITIAL NABU_REAR_INTEGRATION_INITIAL_CODE
#define NABU_REAR_AE_EXPOSURE_MIN 4UL
/*
 * VTS - 64.  The sensor runs at 2400 lines per frame (see
 * NabuOwnOv13b10Mode2104x1560 in HwCapture.c) and needs a readout reserve
 * inside that period; keep this in step with NABU_REAR_EXPOSURE_MAX_LINES,
 * which is where the request is validated.
 */
#define NABU_REAR_AE_EXPOSURE_MAX 2336UL
#define NABU_REAR_AE_GAIN_INITIAL 256UL
#define NABU_REAR_AE_GAIN_MIN 256UL
/* OV13B10 factory gain function uses Q8 gain and caps at 15.5x. */
#define NABU_REAR_AE_GAIN_MAX 3968UL

typedef struct _NABU_REAR_HW_STATE {
    volatile LONG DevicePrepared;
    volatile LONG DeviceInD0;
    volatile LONG CommandInProgress;
    volatile LONG CaptureCount;
    volatile LONG LastStatus;
    volatile LONG OutputBytes;
    volatile LONG D0StopIdleStatus;
    volatile LONG D0ResumeIdleCalled;
    WDFWORKITEM PersistentWorkItem;
    WDFWAITLOCK FrameLock;
    WDFMEMORY LatestFrameMemory;
    PVOID LatestFrame;
    WDFMEMORY ProducerFrameMemory;
    PVOID ProducerFrame;
    volatile LONG PersistentSessionState;
    volatile LONG PersistentStartCount;
    volatile LONG PersistentGetCount;
    volatile LONG PersistentStopCount;
    volatile LONG PersistentFrameCount;
    volatile LONG PersistentGetMissCount;
    volatile LONG PersistentLastStatus;
    volatile LONG LatestFrameBytes;
    volatile LONG LatestFrameGeneration;
    volatile LONG LastReturnedFrameGeneration;
    PNABU_OWN_STAGE23_CONTEXT CaptureContext;
    volatile LONG AutoExposureFrameCount;
    volatile LONG AutoExposureUpdateCount;
    volatile LONG AutoExposureMean;
    volatile LONG AutoExposureP99;
    volatile LONG AutoExposureEncodingVersion;
    volatile LONG AutoExposureExposureLines; /* direct command-code units */
    volatile LONG AutoExposureGain;
    volatile LONG AutoExposureLastStatus;
    volatile LONG AutoExposureWarmStart;
} NABU_REAR_HW_STATE, *PNABU_REAR_HW_STATE;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(
    NABU_REAR_HW_STATE,
    NabuRearGetHwState);

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD NabuRearEvtDeviceAdd;
EVT_WDF_DEVICE_PREPARE_HARDWARE NabuRearEvtPrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE NabuRearEvtReleaseHardware;
EVT_WDF_DEVICE_D0_ENTRY NabuRearEvtD0Entry;
EVT_WDF_DEVICE_D0_EXIT NabuRearEvtD0Exit;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL NabuRearEvtIoDeviceControl;
EVT_WDF_WORKITEM NabuRearPersistentWorkItem;

static NTSTATUS
NabuRearStopPersistentSession(_In_ WDFDEVICE Device);

static ULONG
NabuRearReadLong(_In_ volatile LONG *Value)
{
    return (ULONG)InterlockedCompareExchange(Value, 0, 0);
}

static VOID
NabuRearWriteValue(_In_ WDFKEY Key,
                   _In_z_ PCWSTR Name,
                   _In_ ULONG Value)
{
    UNICODE_STRING valueName;

    RtlInitUnicodeString(&valueName, Name);
    (VOID)WdfRegistryAssignULong(Key, &valueName, Value);
}

static VOID
NabuRearWriteDiagnostics(_In_ WDFDEVICE Device)
{
    PNABU_REAR_HW_STATE state = NabuRearGetHwState(Device);
    PNABU_OWN_STAGE23_CONTEXT capture = NabuOwnGetContext(Device);
    WDFKEY key = NULL;

    if (!NT_SUCCESS(WdfDeviceOpenRegistryKey(
            Device,
            PLUGPLAY_REGKEY_DEVICE,
            KEY_SET_VALUE,
            WDF_NO_OBJECT_ATTRIBUTES,
            &key))) {
        return;
    }

    NabuRearWriteValue(key, L"NabuRearHw_BuildTag", NABU_REAR_HW_BUILD_TAG);
    NabuRearWriteValue(key, L"NabuRearHw_Stage", 40);
    NabuRearWriteValue(
        key, L"NabuRearHw_DevicePrepared",
        NabuRearReadLong(&state->DevicePrepared));
    NabuRearWriteValue(
        key, L"NabuRearHw_DeviceInD0",
        NabuRearReadLong(&state->DeviceInD0));
    NabuRearWriteValue(
        key, L"NabuRearHw_CommandInProgress",
        NabuRearReadLong(&state->CommandInProgress));
    NabuRearWriteValue(
        key, L"NabuRearHw_CaptureCount",
        NabuRearReadLong(&state->CaptureCount));
    NabuRearWriteValue(
        key, L"NabuRearHw_LastStatus",
        NabuRearReadLong(&state->LastStatus));
    NabuRearWriteValue(
        key, L"NabuRearHw_OutputBytes",
        NabuRearReadLong(&state->OutputBytes));
    NabuRearWriteValue(
        key, L"NabuRearHw_D0StopIdleStatus",
        NabuRearReadLong(&state->D0StopIdleStatus));
    NabuRearWriteValue(
        key, L"NabuRearHw_D0ResumeIdleCalled",
        NabuRearReadLong(&state->D0ResumeIdleCalled));
    NabuRearWriteValue(
        key, L"NabuRearHw_PersistentSessionState",
        NabuRearReadLong(&state->PersistentSessionState));
    NabuRearWriteValue(
        key, L"NabuRearHw_PersistentStartCount",
        NabuRearReadLong(&state->PersistentStartCount));
    NabuRearWriteValue(
        key, L"NabuRearHw_PersistentGetCount",
        NabuRearReadLong(&state->PersistentGetCount));
    NabuRearWriteValue(
        key, L"NabuRearHw_PersistentStopCount",
        NabuRearReadLong(&state->PersistentStopCount));
    NabuRearWriteValue(
        key, L"NabuRearHw_PersistentFrameCount",
        NabuRearReadLong(&state->PersistentFrameCount));
    NabuRearWriteValue(
        key, L"NabuRearHw_PersistentGetMissCount",
        NabuRearReadLong(&state->PersistentGetMissCount));
    NabuRearWriteValue(
        key, L"NabuRearHw_PersistentLastStatus",
        NabuRearReadLong(&state->PersistentLastStatus));
    NabuRearWriteValue(
        key, L"NabuRearHw_LatestFrameBytes",
        NabuRearReadLong(&state->LatestFrameBytes));
    NabuRearWriteValue(
        key, L"NabuRearHw_LatestFrameGeneration",
        NabuRearReadLong(&state->LatestFrameGeneration));
    NabuRearWriteValue(
        key, L"NabuRearHw_CaptureLoopFrameCount",
        NabuRearReadLong(&capture->PersistentFrameCount));
    NabuRearWriteValue(
        key, L"NabuRearHw_CaptureLoopFailureCount",
        NabuRearReadLong(&capture->PersistentFrameFailureCount));
    NabuRearWriteValue(
        key, L"NabuRearHw_CaptureLoopConsecutiveFailurePeak",
        NabuRearReadLong(&capture->PersistentConsecutiveFailurePeak));
    NabuRearWriteValue(
        key, L"NabuRearHw_CaptureLoopTerminalStatus",
        NabuRearReadLong(&capture->PersistentTerminalStatus));
    NabuRearWriteValue(
        key, L"NabuRearHw_AutoExposureFrameCount",
        NabuRearReadLong(&state->AutoExposureFrameCount));
    NabuRearWriteValue(
        key, L"NabuRearHw_AutoExposureUpdateCount",
        NabuRearReadLong(&state->AutoExposureUpdateCount));
    NabuRearWriteValue(
        key, L"NabuRearHw_AutoExposureMean",
        NabuRearReadLong(&state->AutoExposureMean));
    NabuRearWriteValue(
        key, L"NabuRearHw_AutoExposureP99",
        NabuRearReadLong(&state->AutoExposureP99));
    NabuRearWriteValue(
        key, L"NabuRearHw_AutoExposureEncodingVersion",
        NabuRearReadLong(&state->AutoExposureEncodingVersion));
    NabuRearWriteValue(
        key, L"NabuRearHw_AutoExposureIntegrationUnits",
        NABU_REAR_INTEGRATION_UNITS_DIRECT_CODE);
    NabuRearWriteValue(
        key, L"NabuRearHw_AutoExposureIntegrationCodeRequested",
        NabuRearReadLong(&state->AutoExposureExposureLines));
    NabuRearWriteValue(
        key, L"NabuRearHw_AutoExposureExposureLines",
        NabuRearReadLong(&state->AutoExposureExposureLines));
    NabuRearWriteValue(
        key, L"NabuRearHw_AutoExposureGain",
        NabuRearReadLong(&state->AutoExposureGain));
    NabuRearWriteValue(
        key, L"NabuRearHw_AutoExposureWarmStart",
        NabuRearReadLong(&state->AutoExposureWarmStart));
    NabuRearWriteValue(
        key, L"NabuRearHw_AutoExposureRequestedGeneration",
        NabuRearReadLong(
            &capture->AutoExposureRequestedGeneration));
    NabuRearWriteValue(
        key, L"NabuRearHw_AutoExposureAppliedGeneration",
        NabuRearReadLong(
            &capture->AutoExposureAppliedGeneration));
    NabuRearWriteValue(
        key, L"NabuRearHw_AutoExposureApplyStatus",
        NabuRearReadLong(&capture->AutoExposureApplyStatus));
    NabuRearWriteValue(
        key, L"NabuRearHw_DmaCommonBufferCreateStatus",
        capture->DmaCommonBufferCreateStatus);
    NabuRearWriteValue(
        key, L"NabuRearHw_DmaPoolFallbackUsed",
        capture->DmaPoolFallbackUsed);
    NabuRearWriteValue(
        key, L"NabuRearHw_ScfgStatus", capture->ScfgStatus);
    NabuRearWriteValue(
        key, L"NabuRearHw_ScfgConfig0", capture->ScfgConfig0);
    NabuRearWriteValue(
        key, L"NabuRearHw_ScfgConfig1", capture->ScfgConfig1);
    NabuRearWriteValue(
        key, L"NabuRearHw_ScfgConfig2", capture->ScfgConfig2);
    NabuRearWriteValue(
        key, L"NabuRearHw_CciConfigValid", capture->CciConfigValid);
    NabuRearWriteValue(
        key, L"NabuRearHw_CciMaster", capture->CciMaster);
    NabuRearWriteValue(
        key, L"NabuRearHw_CciFrequencyKHz", capture->CciFrequencyKHz);
    NabuRearWriteValue(
        key, L"NabuRearHw_CciSlaveAddress", capture->CciSlaveAddress);
    NabuRearWriteValue(
        key, L"NabuRearHw_CciExpectedId", capture->CciExpectedId);
    NabuRearWriteValue(
        key, L"NabuRearHw_VcmPhase", capture->VcmPhase);
    NabuRearWriteValue(
        key, L"NabuRearHw_VcmCompleted", capture->VcmCompleted);
    NabuRearWriteValue(
        key, L"NabuRearHw_VcmSetStateStatus",
        capture->VcmSetStateStatus);
    NabuRearWriteValue(
        key, L"NabuRearHw_VcmActivationCount",
        capture->VcmActivationCount);
    NabuRearWriteValue(
        key, L"NabuRearHw_VcmReserveStatus", capture->VcmReserveStatus);
    NabuRearWriteValue(
        key, L"NabuRearHw_VcmBackStatus", capture->VcmBackStatus);
    NabuRearWriteValue(
        key, L"NabuRearHw_IovaLow", capture->VcmDeviceAddressLow);
    NabuRearWriteValue(
        key, L"NabuRearHw_IovaHigh", capture->VcmDeviceAddressHigh);
    NabuRearWriteValue(
        key, L"NabuRearHw_RealAttempted", capture->RealAttempted);
    NabuRearWriteValue(
        key, L"NabuRearHw_RealCompleted", capture->RealCompleted);
    NabuRearWriteValue(
        key, L"NabuRearHw_RealPhase", capture->RealPhase);
    NabuRearWriteValue(
        key, L"NabuRearHw_RealPlatformOpenStatus",
        capture->RealPlatformOpenStatus);
    NabuRearWriteValue(
        key, L"NabuRearHw_RealAcquire1Status",
        capture->RealAcquire1Status);
    NabuRearWriteValue(
        key, L"NabuRearHw_RealPlatformPowerStatus",
        capture->RealPlatformPowerStatus);
    NabuRearWriteValue(
        key, L"NabuRearHw_RealPlatformPinStatus",
        capture->RealPlatformPinStatus);
    NabuRearWriteValue(
        key, L"NabuRearHw_RealInitMasterStatus",
        capture->RealInitMasterStatus);
    NabuRearWriteValue(
        key, L"NabuRearHw_RealAcquire2Status",
        capture->RealAcquire2Status);
    NabuRearWriteValue(
        key, L"NabuRearHw_RealPowerRailsReady",
        capture->RealPowerRailsReady);
    NabuRearWriteValue(
        key, L"NabuRearHw_RealSpmiInitStatus",
        capture->RealSpmiInitStatus);
    NabuRearWriteValue(
        key, L"NabuRearHw_RealVioWriteStatus",
        capture->RealVioWriteStatus);
    NabuRearWriteValue(
        key, L"NabuRearHw_RealVdigModeWriteStatus",
        capture->RealVdigModeWriteStatus);
    NabuRearWriteValue(
        key, L"NabuRearHw_RealVdigOutWriteStatus",
        capture->RealVdigOutWriteStatus);
    NabuRearWriteValue(
        key, L"NabuRearHw_RealVdigEnWriteStatus",
        capture->RealVdigEnWriteStatus);
    NabuRearWriteValue(
        key, L"NabuRearHw_RealClockTreeReady",
        capture->RealClockTreeReady);
    NabuRearWriteValue(
        key, L"NabuRearHw_RealChipIdStatus",
        capture->RealChipIdStatus);
    NabuRearWriteValue(
        key, L"NabuRearHw_RealChipId", capture->RealChipId);
    NabuRearWriteValue(
        key, L"NabuRearHw_RealCsidTotalPackets",
        capture->RealCsidTotalPackets);
    NabuRearWriteValue(
        key, L"NabuRearHw_RealBufferChanged",
        capture->RealBufferChanged);
    NabuRearWriteValue(
        key, L"NabuRearHw_RealBufferTailChanged",
        capture->RealBufferTailChanged);

    WdfRegistryClose(key);
}

static VOID
NabuRearResetCaptureState(_Inout_ PNABU_OWN_STAGE23_CONTEXT Context)
{
    Context->DmaBufferAlreadyPresent = 0;
    Context->DmaBufferReleased = 0;
    Context->DmaBufferAttempted = 0;
    Context->DmaBufferCreateStatus = (ULONG)STATUS_NOT_SUPPORTED;
    Context->DmaCommonBufferCreateStatus = (ULONG)STATUS_NOT_SUPPORTED;
    Context->DmaPoolFallbackUsed = 0;
    Context->VcmModeRequested = 5;
    Context->VcmPhase = 0;
    Context->VcmCompleted = 0;
    Context->VcmSetStateStatus = (ULONG)STATUS_NOT_SUPPORTED;
    Context->VcmActivationCount = 0;
    Context->VcmActivationFailIndex = MAXULONG;
    Context->VcmReserveStatus = (ULONG)STATUS_NOT_SUPPORTED;
    Context->VcmBackStatus = (ULONG)STATUS_NOT_SUPPORTED;
    Context->VcmDeviceAddressLow = 0;
    Context->VcmDeviceAddressHigh = 0;
    Context->RealAttempted = 0;
    Context->RealCompleted = 0;
    Context->RealPhase = 0;
    Context->RealChipId = 0;
    Context->RealCsidTotalPackets = 0;
    Context->RealBufferChanged = 0;
    Context->RealBufferTailChanged = 0;
}

static NTSTATUS
NabuRearCaptureOneFrame(_In_ WDFDEVICE Device,
                        _Out_writes_bytes_(OutputLength) PVOID Output,
                        _In_ SIZE_T OutputLength,
                        _Out_ PULONG BytesWritten)
{
    PNABU_REAR_HW_STATE state = NabuRearGetHwState(Device);
    PNABU_OWN_STAGE23_CONTEXT capture = NabuOwnGetContext(Device);
    PVOID raw;
    NTSTATUS status;
    BOOLEAN idleHeld = FALSE;

    *BytesWritten = 0;
    if (OutputLength < NABU_REAR_FRAME_BYTES) {
        return STATUS_BUFFER_TOO_SMALL;
    }
    if (KeGetCurrentIrql() != PASSIVE_LEVEL) {
        return STATUS_INVALID_DEVICE_STATE;
    }
    if (NabuRearReadLong(&state->DevicePrepared) == 0) {
        return STATUS_DEVICE_NOT_READY;
    }

    status = WdfDeviceStopIdle(Device, TRUE);
    InterlockedExchange(&state->D0StopIdleStatus, (LONG)status);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    idleHeld = TRUE;

    NabuRearResetCaptureState(capture);
    NabuOwnCreateDmaBuffer(capture);
    raw = NabuOwnGetDmaVirtualAddress(capture);
    if (raw == NULL) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto Finished;
    }

    NabuOwnRunVcmProbe(capture);
    if (capture->VcmCompleted == 0 ||
        capture->RealCompleted == 0 ||
        capture->RealChipId != 0x0D42UL ||
        capture->RealBufferChanged == 0 ||
        capture->RealBufferTailChanged == 0) {
        status = STATUS_IO_DEVICE_ERROR;
        goto Finished;
    }

    RtlCopyMemory(Output, raw, NABU_REAR_FRAME_BYTES);
    KeMemoryBarrier();
    *BytesWritten = NABU_REAR_FRAME_BYTES;
    status = STATUS_SUCCESS;

Finished:
    NabuOwnReleaseDmaBuffer(capture);
    if (idleHeld) {
        WdfDeviceResumeIdle(Device);
        InterlockedExchange(&state->D0ResumeIdleCalled, 1);
    }
    return status;
}

static VOID
NabuRearCompactPreviewRaw(
    _In_reads_(NABU_REAR_WIDTH * NABU_REAR_HEIGHT) const USHORT *Raw,
    _Out_writes_(NABU_REAR_PREVIEW_RAW_WIDTH *
                 NABU_REAR_PREVIEW_RAW_HEIGHT) USHORT *Preview)
{
    ULONG cellX;
    ULONG cellY;
    ULONG sourceXQ12[640];

    /*
     * Android's VFE scaler low-passes each Bayer plane before reducing it.
     * The former nearest-cell picker aliased diagonal edges into the large
     * stair steps visible in Camera.  Bilinear interpolation is performed in
     * 2x2 Bayer-cell space, so GRBG phase is preserved exactly.
     */
    for (cellX = 0; cellX < RTL_NUMBER_OF(sourceXQ12); ++cellX) {
        sourceXQ12[cellX] = (ULONG)(
            ((ULONGLONG)cellX *
             (((NABU_REAR_WIDTH / 2UL) - 1UL) << 12)) / 639ULL);
    }
    for (cellY = 0; cellY < 480UL; ++cellY) {
        ULONG sourceYQ12 = (ULONG)(
            ((ULONGLONG)cellY *
             (((NABU_REAR_HEIGHT / 2UL) - 1UL) << 12)) / 479ULL);
        ULONG sourceCellY = sourceYQ12 >> 12;
        ULONG nextCellY = sourceCellY + 1UL < NABU_REAR_HEIGHT / 2UL ?
            sourceCellY + 1UL : sourceCellY;
        ULONG weightY = sourceYQ12 & 4095UL;
        ULONG phaseY;

        for (phaseY = 0; phaseY < 2UL; ++phaseY) {
            const USHORT *row0 = Raw +
                (sourceCellY * 2UL + phaseY) * NABU_REAR_WIDTH;
            const USHORT *row1 = Raw +
                (nextCellY * 2UL + phaseY) * NABU_REAR_WIDTH;
            USHORT *destination = Preview +
                (cellY * 2UL + phaseY) * NABU_REAR_PREVIEW_RAW_WIDTH;

            for (cellX = 0; cellX < 640UL; ++cellX) {
                ULONG sourceCellX = sourceXQ12[cellX] >> 12;
                ULONG nextCellX = sourceCellX + 1UL < NABU_REAR_WIDTH / 2UL ?
                    sourceCellX + 1UL : sourceCellX;
                ULONG weightX = sourceXQ12[cellX] & 4095UL;
                ULONG phaseX;

                for (phaseX = 0; phaseX < 2UL; ++phaseX) {
                    ULONG x0 = sourceCellX * 2UL + phaseX;
                    ULONG x1 = nextCellX * 2UL + phaseX;
                    LONG top = (LONG)row0[x0] +
                        ((((LONG)row0[x1] - (LONG)row0[x0]) *
                          (LONG)weightX) >> 12);
                    LONG bottom = (LONG)row1[x0] +
                        ((((LONG)row1[x1] - (LONG)row1[x0]) *
                          (LONG)weightX) >> 12);
                    LONG value = top +
                        (((bottom - top) * (LONG)weightY) >> 12);

                    destination[cellX * 2UL + phaseX] =
                        (USHORT)(value <= 1023 ? value : 1023);
                }
            }
        }
    }
}

static VOID
NabuRearUpdateAutoExposure(
    _Inout_ PNABU_REAR_HW_STATE State,
    _In_reads_(NABU_REAR_PREVIEW_RAW_WIDTH *
               NABU_REAR_PREVIEW_RAW_HEIGHT) const USHORT *Preview)
{
    ULONG histogram[256];
    ULONGLONG sum = 0;
    ULONGLONG product;
    ULONGLONG nextProduct;
    ULONG samples = 0;
    ULONG cumulative = 0;
    ULONG highTarget;
    ULONG mean;
    ULONG p99 = 1023UL;
    ULONG ratioQ10;
    ULONG highlightRatioQ10;
    ULONG exposure;
    ULONG gain;
    ULONG nextExposure;
    ULONG nextGain;
    ULONG x;
    ULONG y;
    ULONG frame;
    BOOLEAN rapidAdjustment;
    PNABU_OWN_STAGE23_CONTEXT capture;

    if (State == NULL || Preview == NULL) {
        return;
    }
    frame = (ULONG)InterlockedIncrement(
        &State->AutoExposureFrameCount);
    capture = State->CaptureContext;
    /* Freeze the controller itself, not just its sensor writes. The frame
     * count still advances so warmup and delivery remain bounded. */
    if (capture != NULL && InterlockedCompareExchange(
            &capture->AutoExposureFreeze, 0, 0) != 0) return;
    RtlZeroMemory(histogram, sizeof(histogram));
    for (y = 0; y < NABU_REAR_PREVIEW_RAW_HEIGHT; y += 16UL) {
        for (x = 0; x < NABU_REAR_PREVIEW_RAW_WIDTH; x += 16UL) {
            ULONG value = Preview[y * NABU_REAR_PREVIEW_RAW_WIDTH + x];

            if (value > 1023UL) {
                value = 1023UL;
            }
            histogram[value >> 2] += 1;
            sum += value;
            samples += 1;
        }
    }
    if (samples == 0UL) {
        return;
    }
    mean = (ULONG)(sum / samples);
    highTarget = (samples * 995UL + 999UL) / 1000UL;
    for (x = 0; x < 256UL; ++x) {
        cumulative += histogram[x];
        if (cumulative >= highTarget) {
            p99 = x * 4UL + 3UL;
            break;
        }
    }
    InterlockedExchange(&State->AutoExposureMean, (LONG)mean);
    InterlockedExchange(&State->AutoExposureP99, (LONG)p99);
    rapidAdjustment = frame <= NABU_REAR_AE_FAST_FRAMES ||
        mean < 96UL || mean > 150UL;
    /* Android frameSkip=2: one update, then two frames for sensor latency. */
    if (rapidAdjustment &&
        ((frame - 1UL) % NABU_REAR_AE_FAST_UPDATE_FRAMES) != 0UL) {
        return;
    }
    if (!rapidAdjustment &&
        (frame % NABU_REAR_AE_UPDATE_FRAMES) != 0UL) {
        return;
    }

    exposure = NabuRearReadLong(&State->AutoExposureExposureLines);
    gain = NabuRearReadLong(&State->AutoExposureGain);
    if (exposure < NABU_REAR_AE_EXPOSURE_MIN ||
        exposure > NABU_REAR_AE_EXPOSURE_MAX) {
        exposure = NABU_REAR_AE_EXPOSURE_INITIAL;
    }
    if (gain < NABU_REAR_AE_GAIN_MIN ||
        gain > NABU_REAR_AE_GAIN_MAX) {
        gain = NABU_REAR_AE_GAIN_INITIAL;
    }
    if (mean <= NABU_REAR_AE_BLACK_LEVEL) {
        ratioQ10 = rapidAdjustment
            ? NABU_REAR_AE_FAST_RATIO_MAX_Q10
            : 1152UL;
    } else {
        ULONG signal = mean - NABU_REAR_AE_BLACK_LEVEL;

        ratioQ10 =
            ((NABU_REAR_AE_TARGET_MEAN - NABU_REAR_AE_BLACK_LEVEL) *
                 1024UL + signal / 2UL) / signal;
    }
    if (p99 > 900UL && mean > NABU_REAR_AE_TARGET_MEAN) {
        highlightRatioQ10 = (900UL * 1024UL) / p99;
        if (highlightRatioQ10 < ratioQ10) {
            ratioQ10 = highlightRatioQ10;
        }
    }
    if (mean >= 112UL && mean <= 128UL && p99 <= 960UL) {
        ratioQ10 = 1024UL;
    }
    if (rapidAdjustment) {
        if (ratioQ10 < NABU_REAR_AE_FAST_RATIO_MIN_Q10) {
            ratioQ10 = NABU_REAR_AE_FAST_RATIO_MIN_Q10;
        } else if (ratioQ10 > NABU_REAR_AE_FAST_RATIO_MAX_Q10) {
            ratioQ10 = NABU_REAR_AE_FAST_RATIO_MAX_Q10;
        }
    } else if (ratioQ10 < 768UL) {
        ratioQ10 = 768UL;
    } else if (ratioQ10 > 1152UL) {
        ratioQ10 = 1152UL;
    }
    product = (ULONGLONG)exposure * gain;
    nextProduct = (product * ratioQ10 + 512ULL) >> 10;
    nextExposure = (ULONG)((nextProduct + 128ULL) / 256ULL);
    if (nextExposure < NABU_REAR_AE_EXPOSURE_MIN) {
        nextExposure = NABU_REAR_AE_EXPOSURE_MIN;
    } else if (nextExposure > NABU_REAR_AE_EXPOSURE_MAX) {
        nextExposure = NABU_REAR_AE_EXPOSURE_MAX;
    }
    nextGain = (ULONG)((nextProduct + nextExposure / 2UL) /
        nextExposure);
    if (nextGain < NABU_REAR_AE_GAIN_MIN) {
        nextGain = NABU_REAR_AE_GAIN_MIN;
    } else if (nextGain > NABU_REAR_AE_GAIN_MAX) {
        nextGain = NABU_REAR_AE_GAIN_MAX;
    }
    if (nextExposure > exposure - 4UL &&
        nextExposure < exposure + 4UL &&
        nextGain > gain - 2UL && nextGain < gain + 2UL) {
        return;
    }

    capture = State->CaptureContext;
    if (capture == NULL) {
        return;
    }
    InterlockedExchange(
        &capture->AutoExposureRequestedExposure,
        (LONG)nextExposure);
    InterlockedExchange(
        &capture->AutoExposureRequestedGain,
        (LONG)nextGain);
    InterlockedIncrement(
        &capture->AutoExposureRequestedGeneration);
    InterlockedExchange(
        &State->AutoExposureExposureLines,
        (LONG)nextExposure);
    InterlockedExchange(
        &State->AutoExposureGain, (LONG)nextGain);
    InterlockedIncrement(&State->AutoExposureUpdateCount);
}

static VOID
NabuRearPublishPersistentFrame(
    _In_opt_ PVOID PublishContext,
    _In_reads_bytes_(FrameBytes) const UCHAR *Frame,
    _In_ ULONG FrameBytes)
{
    PNABU_REAR_HW_STATE state =
        (PNABU_REAR_HW_STATE)PublishContext;
    PVOID oldLatest;
    ULONG startupSkipFrames;

    if (state == NULL || Frame == NULL ||
        FrameBytes != NABU_REAR_FRAME_BYTES ||
        state->LatestFrame == NULL || state->ProducerFrame == NULL ||
        state->FrameLock == NULL) {
        return;
    }

    NabuRearCompactPreviewRaw(
        (const USHORT *)Frame,
        (USHORT *)state->ProducerFrame);
    NabuRearUpdateAutoExposure(
        state,
        (const USHORT *)state->ProducerFrame);
    startupSkipFrames =
        NabuRearReadLong(&state->AutoExposureWarmStart) != 0
            ? NABU_REAR_WARM_START_SKIP_FRAMES
            : NABU_REAR_STARTUP_SKIP_FRAMES;
    /* Match Android frameSkipInit behavior and never expose sensor warm-up. */
    if (NabuRearReadLong(&state->AutoExposureFrameCount) <=
        startupSkipFrames) {
        return;
    }
    WdfWaitLockAcquire(state->FrameLock, NULL);
    KeMemoryBarrier();
    oldLatest = state->LatestFrame;
    state->LatestFrame = state->ProducerFrame;
    state->ProducerFrame = oldLatest;
    InterlockedExchange(
        &state->LatestFrameBytes, (LONG)NABU_REAR_PREVIEW_RAW_BYTES);
    InterlockedIncrement(&state->LatestFrameGeneration);
    InterlockedIncrement(&state->PersistentFrameCount);
    InterlockedIncrement(&state->CaptureCount);
    WdfWaitLockRelease(state->FrameLock);
    InterlockedExchange(&state->PersistentSessionState, 2);
}

VOID
NabuRearPersistentWorkItem(_In_ WDFWORKITEM WorkItem)
{
    WDFDEVICE device =
        (WDFDEVICE)WdfWorkItemGetParentObject(WorkItem);
    PNABU_REAR_HW_STATE state = NabuRearGetHwState(device);
    PNABU_OWN_STAGE23_CONTEXT capture = NabuOwnGetContext(device);
    PVOID raw;
    NTSTATUS status;
    BOOLEAN idleHeld = FALSE;

    if (NabuRearReadLong(&state->DevicePrepared) == 0 ||
        NabuRearReadLong(&state->DeviceInD0) == 0 ||
        InterlockedCompareExchange(
            &capture->PersistentStopRequested, 0, 0) != 0) {
        status = STATUS_DEVICE_NOT_READY;
        goto Finished;
    }

    status = WdfDeviceStopIdle(device, TRUE);
    InterlockedExchange(&state->D0StopIdleStatus, (LONG)status);
    if (!NT_SUCCESS(status)) {
        goto Finished;
    }
    idleHeld = TRUE;

    NabuRearResetCaptureState(capture);
    NabuOwnCreateDmaBuffer(capture);
    raw = NabuOwnGetDmaVirtualAddress(capture);
    if (raw == NULL) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto Finished;
    }

    InterlockedExchange(&capture->PersistentMode, 1);
    state->CaptureContext = capture;
    capture->PersistentPublishContext = state;
    capture->PersistentPublish = NabuRearPublishPersistentFrame;
    NabuOwnRunVcmProbe(capture);
    status = (NTSTATUS)NabuRearReadLong(
        &capture->PersistentTerminalStatus);
    if (NT_SUCCESS(status)) {
        status = NabuRearReadLong(&state->PersistentFrameCount) != 0 &&
                 capture->RealChipId == 0x0D42UL
            ? STATUS_SUCCESS
            : STATUS_IO_DEVICE_ERROR;
    }

Finished:
    state->CaptureContext = NULL;
    capture->PersistentPublish = NULL;
    capture->PersistentPublishContext = NULL;
    InterlockedExchange(&capture->PersistentMode, 0);
    NabuOwnReleaseDmaBuffer(capture);
    if (idleHeld) {
        WdfDeviceResumeIdle(device);
        InterlockedExchange(&state->D0ResumeIdleCalled, 1);
    }
    InterlockedExchange(&state->PersistentLastStatus, (LONG)status);
    InterlockedExchange(&state->LastStatus, (LONG)status);
    InterlockedExchange(&state->PersistentSessionState, 0);
    NabuRearWriteDiagnostics(device);
}

static NTSTATUS
NabuRearStartPersistentSession(_In_ WDFDEVICE Device)
{
    PNABU_REAR_HW_STATE state = NabuRearGetHwState(Device);
    PNABU_OWN_STAGE23_CONTEXT capture = NabuOwnGetContext(Device);
    LONG oldState;
    ULONG startupExposure;
    ULONG startupGain;
    BOOLEAN warmStart;
    int validIntegrationStart;
    int trustedAppliedStart;

    if (NabuRearReadLong(&state->DevicePrepared) == 0 ||
        NabuRearReadLong(&state->DeviceInD0) == 0) {
        return STATUS_DEVICE_NOT_READY;
    }
    oldState = InterlockedCompareExchange(
        &state->PersistentSessionState, 1, 0);
    if (oldState != 0) {
        return STATUS_SUCCESS;
    }

    /* Versioned in-memory state only; no old-unit warm-start migration. */
    startupExposure = NabuRearReadLong(
        &state->AutoExposureExposureLines);
    trustedAppliedStart = NabuRearPriorIntegrationTrusted(
        NabuRearReadLong(&capture->AutoExposureRequestedGeneration),
        NabuRearReadLong(&capture->AutoExposureAppliedGeneration),
        NabuRearReadLong(&capture->AutoExposureApplyStatus),
        NabuRearReadLong(&state->PersistentLastStatus),
        NabuRearReadLong(&state->PersistentFrameCount),
        NabuRearReadLong(&capture->PersistentTerminalStatus));
    startupExposure = NabuRearSelectIntegrationStart(
        NabuRearReadLong(&state->AutoExposureEncodingVersion),
        startupExposure, trustedAppliedStart, &validIntegrationStart);
    startupGain = NabuRearReadLong(&state->AutoExposureGain);
    warmStart = validIntegrationStart &&
        startupGain >= NABU_REAR_AE_GAIN_MIN &&
        startupGain <= NABU_REAR_AE_GAIN_MAX;
    if (startupExposure < NABU_REAR_AE_EXPOSURE_MIN ||
        startupExposure > NABU_REAR_AE_EXPOSURE_MAX) {
        startupExposure = NABU_REAR_AE_EXPOSURE_INITIAL;
    }
    if (startupGain < NABU_REAR_AE_GAIN_MIN ||
        startupGain > NABU_REAR_AE_GAIN_MAX) {
        startupGain = NABU_REAR_AE_GAIN_INITIAL;
    }

    InterlockedIncrement(&state->PersistentStartCount);
    InterlockedExchange(
        &state->PersistentLastStatus, (LONG)STATUS_PENDING);
    InterlockedExchange(&state->PersistentFrameCount, 0);
    InterlockedExchange(&state->PersistentGetMissCount, 0);
    InterlockedExchange(&state->LatestFrameBytes, 0);
    InterlockedExchange(&state->LatestFrameGeneration, 0);
    InterlockedExchange(&state->LastReturnedFrameGeneration, 0);
    InterlockedExchange(&capture->PersistentStopRequested, 0);
    InterlockedExchange(&state->AutoExposureFrameCount, 0);
    InterlockedExchange(&state->AutoExposureUpdateCount, 0);
    InterlockedExchange(&state->AutoExposureMean, 0);
    InterlockedExchange(&state->AutoExposureP99, 0);
    InterlockedExchange(
        &state->AutoExposureWarmStart, warmStart ? 1 : 0);
    InterlockedExchange(
        &state->AutoExposureEncodingVersion,
        NABU_REAR_INTEGRATION_ENCODING_VERSION);
    InterlockedExchange(
        &state->AutoExposureExposureLines,
        (LONG)startupExposure);
    InterlockedExchange(
        &state->AutoExposureGain,
        (LONG)startupGain);
    InterlockedExchange(
        &state->AutoExposureLastStatus,
        (LONG)STATUS_SUCCESS);
    InterlockedExchange(
        &capture->AutoExposureRequestedExposure,
        (LONG)startupExposure);
    InterlockedExchange(
        &capture->AutoExposureRequestedGain,
        (LONG)startupGain);
    InterlockedExchange(
        &capture->AutoExposureRequestedGeneration, 1);
    InterlockedExchange(
        &capture->AutoExposureAppliedGeneration, 0);
    InterlockedExchange(
        &capture->AutoExposureApplyStatus,
        (LONG)STATUS_SUCCESS);
    InterlockedExchange(&capture->PersistentFrameCount, 0);
    InterlockedExchange(&capture->PersistentFrameFailureCount, 0);
    InterlockedExchange(
        &capture->PersistentConsecutiveFailurePeak, 0);
    InterlockedExchange(
        &capture->PersistentTerminalStatus, (LONG)STATUS_SUCCESS);
    WdfWorkItemEnqueue(state->PersistentWorkItem);
    return STATUS_SUCCESS;
}

static NTSTATUS
NabuRearGetPersistentFrame(
    _In_ WDFDEVICE Device,
    _Out_writes_bytes_(OutputLength) PVOID Output,
    _In_ SIZE_T OutputLength,
    _Out_ PULONG BytesWritten)
{
    PNABU_REAR_HW_STATE state = NabuRearGetHwState(Device);
    NTSTATUS status = STATUS_DEVICE_NOT_READY;

    *BytesWritten = 0;
    InterlockedIncrement(&state->PersistentGetCount);
    if (Output == NULL || OutputLength < NABU_REAR_PREVIEW_RAW_BYTES) {
        return STATUS_BUFFER_TOO_SMALL;
    }

    WdfWaitLockAcquire(state->FrameLock, NULL);
    if (NabuRearReadLong(&state->PersistentSessionState) != 0 &&
        NabuRearReadLong(&state->LatestFrameGeneration) != 0 &&
        NabuRearReadLong(&state->LatestFrameBytes) ==
            NABU_REAR_PREVIEW_RAW_BYTES) {
        LONG generation =
            NabuRearReadLong(&state->LatestFrameGeneration);
        RtlCopyMemory(
            Output, state->LatestFrame, NABU_REAR_PREVIEW_RAW_BYTES);
        KeMemoryBarrier();
        InterlockedExchange(
            &state->LastReturnedFrameGeneration, generation);
        *BytesWritten = NABU_REAR_PREVIEW_RAW_BYTES;
        status = STATUS_SUCCESS;
    }
    WdfWaitLockRelease(state->FrameLock);
    if (!NT_SUCCESS(status)) {
        InterlockedIncrement(&state->PersistentGetMissCount);
    }
    return status;
}

static NTSTATUS
NabuRearStopPersistentSession(_In_ WDFDEVICE Device)
{
    PNABU_REAR_HW_STATE state = NabuRearGetHwState(Device);
    PNABU_OWN_STAGE23_CONTEXT capture = NabuOwnGetContext(Device);
    NTSTATUS status;

    if (NabuRearReadLong(&state->PersistentSessionState) == 0) {
        return STATUS_SUCCESS;
    }
    InterlockedIncrement(&state->PersistentStopCount);
    InterlockedExchange(&capture->PersistentStopRequested, 1);
    WdfWorkItemFlush(state->PersistentWorkItem);
    status = (NTSTATUS)NabuRearReadLong(&state->PersistentLastStatus);
    if (NabuRearReadLong(&state->PersistentFrameCount) != 0) {
        status = STATUS_SUCCESS;
    }
    InterlockedExchange(&state->PersistentSessionState, 0);
    InterlockedExchange(&state->PersistentLastStatus, (LONG)status);
    return status;
}

VOID
NabuRearEvtIoDeviceControl(_In_ WDFQUEUE Queue,
                           _In_ WDFREQUEST Request,
                           _In_ size_t OutputBufferLength,
                           _In_ size_t InputBufferLength,
                           _In_ ULONG IoControlCode)
{
    WDFDEVICE device = WdfIoQueueGetDevice(Queue);
    PNABU_REAR_HW_STATE state = NabuRearGetHwState(device);
    PVOID output = NULL;
    ULONG bytesWritten = 0;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(InputBufferLength);
    InterlockedExchange(&state->CommandInProgress, 1);
    InterlockedExchange(&state->OutputBytes, 0);
    InterlockedExchange(&state->D0ResumeIdleCalled, 0);
    switch (IoControlCode) {
    case IOCTL_NABU_CAM_HW_REAR_SESSION_START:
        status = NabuRearStartPersistentSession(device);
        break;
    case IOCTL_NABU_CAM_HW_REAR_SESSION_GET_FRAME:
        status = WdfRequestRetrieveOutputBuffer(
            Request, NABU_REAR_PREVIEW_RAW_BYTES, &output, NULL);
        if (NT_SUCCESS(status)) {
            status = NabuRearGetPersistentFrame(
                device, output, OutputBufferLength, &bytesWritten);
        }
        break;
    case IOCTL_NABU_CAM_HW_REAR_SESSION_STOP:
        status = NabuRearStopPersistentSession(device);
        break;
    case IOCTL_NABU_CAM_HW_CAPTURE_REAR_RAW_BUFFER:
        if (NabuRearReadLong(&state->PersistentSessionState) != 0) {
            status = STATUS_DEVICE_BUSY;
            break;
        }
        InterlockedIncrement(&state->CaptureCount);
        status = WdfRequestRetrieveOutputBuffer(
            Request, NABU_REAR_FRAME_BYTES, &output, NULL);
        if (NT_SUCCESS(status)) {
            status = NabuRearCaptureOneFrame(
                device, output, OutputBufferLength, &bytesWritten);
        }
        break;
    default:
        status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }
    InterlockedExchange(&state->LastStatus, (LONG)status);
    InterlockedExchange(&state->OutputBytes, (LONG)bytesWritten);
    InterlockedExchange(&state->CommandInProgress, 0);
    if (!NT_SUCCESS(status) ||
        IoControlCode != IOCTL_NABU_CAM_HW_REAR_SESSION_GET_FRAME) {
        NabuRearWriteDiagnostics(device);
    }
    WdfRequestCompleteWithInformation(Request, status, bytesWritten);
}

NTSTATUS
NabuRearEvtPrepareHardware(_In_ WDFDEVICE Device,
                           _In_ WDFCMRESLIST ResourcesRaw,
                           _In_ WDFCMRESLIST ResourcesTranslated)
{
    PNABU_REAR_HW_STATE state = NabuRearGetHwState(Device);
    PNABU_OWN_STAGE23_CONTEXT capture = NabuOwnGetContext(Device);

    UNREFERENCED_PARAMETER(ResourcesRaw);
    UNREFERENCED_PARAMETER(ResourcesTranslated);
    capture->PrepareCount += 1;
    NabuOwnQueryScfg(capture);
    InterlockedExchange(&state->DevicePrepared, 1);
    NabuRearWriteDiagnostics(Device);
    return STATUS_SUCCESS;
}

NTSTATUS
NabuRearEvtReleaseHardware(_In_ WDFDEVICE Device,
                           _In_ WDFCMRESLIST ResourcesTranslated)
{
    PNABU_REAR_HW_STATE state = NabuRearGetHwState(Device);

    UNREFERENCED_PARAMETER(ResourcesTranslated);
    (VOID)NabuRearStopPersistentSession(Device);
    InterlockedExchange(&state->DevicePrepared, 0);
    InterlockedExchange(&state->DeviceInD0, 0);
    NabuRearWriteDiagnostics(Device);
    return STATUS_SUCCESS;
}

NTSTATUS
NabuRearEvtD0Entry(_In_ WDFDEVICE Device,
                   _In_ WDF_POWER_DEVICE_STATE PreviousState)
{
    PNABU_REAR_HW_STATE state = NabuRearGetHwState(Device);
    PNABU_OWN_STAGE23_CONTEXT capture = NabuOwnGetContext(Device);

    UNREFERENCED_PARAMETER(PreviousState);
    capture->D0EntryCount += 1;
    InterlockedExchange(&state->DeviceInD0, 1);
    NabuRearWriteDiagnostics(Device);
    return STATUS_SUCCESS;
}

NTSTATUS
NabuRearEvtD0Exit(_In_ WDFDEVICE Device,
                  _In_ WDF_POWER_DEVICE_STATE TargetState)
{
    PNABU_REAR_HW_STATE state = NabuRearGetHwState(Device);

    UNREFERENCED_PARAMETER(TargetState);
    (VOID)NabuRearStopPersistentSession(Device);
    InterlockedExchange(&state->DeviceInD0, 0);
    NabuRearWriteDiagnostics(Device);
    return STATUS_SUCCESS;
}

NTSTATUS
NabuRearEvtDeviceAdd(_In_ WDFDRIVER Driver,
                     _Inout_ PWDFDEVICE_INIT DeviceInit)
{
    static const UNICODE_STRING symbolicLink =
        RTL_CONSTANT_STRING(
            L"\\DosDevices\\Global\\NabuCamHwRearControl");
    WDF_PNPPOWER_EVENT_CALLBACKS pnpCallbacks;
    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_OBJECT_ATTRIBUTES stateAttributes;
    WDF_DMA_ENABLER_CONFIG dmaConfig;
    WDF_IO_QUEUE_CONFIG queueConfig;
    WDF_WORKITEM_CONFIG workItemConfig;
    WDF_OBJECT_ATTRIBUTES workItemAttributes;
    WDF_OBJECT_ATTRIBUTES lockAttributes;
    WDF_OBJECT_ATTRIBUTES memoryAttributes;
    WDFDEVICE device;
    PNABU_OWN_STAGE23_CONTEXT capture;
    PNABU_REAR_HW_STATE state;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(Driver);
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpCallbacks);
    pnpCallbacks.EvtDevicePrepareHardware = NabuRearEvtPrepareHardware;
    pnpCallbacks.EvtDeviceReleaseHardware = NabuRearEvtReleaseHardware;
    pnpCallbacks.EvtDeviceD0Entry = NabuRearEvtD0Entry;
    pnpCallbacks.EvtDeviceD0Exit = NabuRearEvtD0Exit;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpCallbacks);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(
        &attributes,
        NABU_OWN_STAGE23_CONTEXT);
    attributes.ExecutionLevel = WdfExecutionLevelPassive;
    status = WdfDeviceCreate(&DeviceInit, &attributes, &device);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    capture = NabuOwnGetContext(device);
    RtlZeroMemory(capture, sizeof(*capture));
    capture->Device = device;
    capture->VcmModeRequested = 5;

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(
        &stateAttributes,
        NABU_REAR_HW_STATE);
    status = WdfObjectAllocateContext(
        device,
        &stateAttributes,
        (PVOID *)&state);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    RtlZeroMemory(state, sizeof(*state));
    InterlockedExchange(&state->LastStatus, (LONG)STATUS_NOT_SUPPORTED);
    InterlockedExchange(
        &state->D0StopIdleStatus,
        (LONG)STATUS_NOT_SUPPORTED);
    InterlockedExchange(
        &state->PersistentLastStatus,
        (LONG)STATUS_NOT_SUPPORTED);

    WDF_OBJECT_ATTRIBUTES_INIT(&lockAttributes);
    lockAttributes.ParentObject = device;
    status = WdfWaitLockCreate(&lockAttributes, &state->FrameLock);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    WDF_OBJECT_ATTRIBUTES_INIT(&memoryAttributes);
    memoryAttributes.ParentObject = device;
    status = WdfMemoryCreate(
        &memoryAttributes,
        NonPagedPoolNx,
        NABU_REAR_FRAME_POOL_TAG,
        NABU_REAR_PREVIEW_RAW_BYTES,
        &state->LatestFrameMemory,
        &state->LatestFrame);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    RtlZeroMemory(state->LatestFrame, NABU_REAR_PREVIEW_RAW_BYTES);
    WDF_OBJECT_ATTRIBUTES_INIT(&memoryAttributes);
    memoryAttributes.ParentObject = device;
    status = WdfMemoryCreate(
        &memoryAttributes,
        NonPagedPoolNx,
        NABU_REAR_FRAME_POOL_TAG,
        NABU_REAR_PREVIEW_RAW_BYTES,
        &state->ProducerFrameMemory,
        &state->ProducerFrame);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    RtlZeroMemory(state->ProducerFrame, NABU_REAR_PREVIEW_RAW_BYTES);
    WDF_WORKITEM_CONFIG_INIT(
        &workItemConfig, NabuRearPersistentWorkItem);
    workItemConfig.AutomaticSerialization = FALSE;
    WDF_OBJECT_ATTRIBUTES_INIT(&workItemAttributes);
    workItemAttributes.ParentObject = device;
    status = WdfWorkItemCreate(
        &workItemConfig,
        &workItemAttributes,
        &state->PersistentWorkItem);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    WDF_DMA_ENABLER_CONFIG_INIT(
        &dmaConfig,
        WdfDmaProfileScatterGather64Duplex,
        NABU_OWN_DMA_BUFFER_BYTES);
    status = WdfDmaEnablerCreate(
        device,
        &dmaConfig,
        WDF_NO_OBJECT_ATTRIBUTES,
        &capture->DmaEnabler);
    capture->DmaEnablerCreateStatus = (ULONG)status;
    if (!NT_SUCCESS(status)) {
        return status;
    }

    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(
        &queueConfig,
        WdfIoQueueDispatchSequential);
    queueConfig.EvtIoDeviceControl = NabuRearEvtIoDeviceControl;
    status = WdfIoQueueCreate(
        device,
        &queueConfig,
        WDF_NO_OBJECT_ATTRIBUTES,
        WDF_NO_HANDLE);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    status = WdfDeviceCreateSymbolicLink(
        device,
        (PUNICODE_STRING)&symbolicLink);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
DriverEntry(_In_ PDRIVER_OBJECT DriverObject,
            _In_ PUNICODE_STRING RegistryPath)
{
    WDF_DRIVER_CONFIG config;

    WDF_DRIVER_CONFIG_INIT(&config, NabuRearEvtDeviceAdd);
    return WdfDriverCreate(
        DriverObject,
        RegistryPath,
        WDF_NO_OBJECT_ATTRIBUTES,
        &config,
        WDF_NO_HANDLE);
}
