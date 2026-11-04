#pragma once
//
// HwCapture.h - Interface to Stage39 hardware capture backend.
// Full context struct + accessor + exported function declarations.
//

#include <ntddk.h>
#include <wdf.h>
#include "Platform.h"
#include "Csiphy.h"
#include "Spmi.h"

// ============================================================================
// Constants (must match HwCapture.c)
// ============================================================================

#define NABU_REAR_WIDTH             2104UL
#define NABU_REAR_HEIGHT            1560UL
#define NABU_REAR_STRIDE            (NABU_REAR_WIDTH * sizeof(USHORT))
#define NABU_REAR_FRAME_BYTES       (NABU_REAR_STRIDE * NABU_REAR_HEIGHT)
#define NABU_OWN_DMA_BUFFER_BYTES   0x01000000UL
#define NABU_OWN_ARM_REAL_CAPTURE   39UL
#define NABU_SMMU_MAX_CONTEXT_BANKS 12UL
#define NABU_OWN_SCFG_WORD_COUNT    40UL

typedef VOID
(*NABU_REAR_PERSISTENT_PUBLISH)(
    _In_opt_ PVOID PublishContext,
    _In_reads_bytes_(FrameBytes) const UCHAR *Frame,
    _In_ ULONG FrameBytes);

// ============================================================================
// Context structure (full definition from Stage39)
// ============================================================================

typedef struct _NABU_OWN_STAGE23_CONTEXT {
    WDFDEVICE Device;
    ULONG PrepareCount;
    ULONG D0EntryCount;
    ULONG ArmRequested;
    ULONG SafetyRejectedArm;
    ULONG HardwareTouched;

    WDFDMAENABLER DmaEnabler;
    WDFCOMMONBUFFER DmaCommonBuffer;
    PVOID DmaPoolBuffer;
    ULONG DmaEnablerCreateStatus;
    ULONG DmaBufferAttempted;
    ULONG DmaBufferCreateStatus;
    ULONG DmaCommonBufferCreateStatus;
    ULONG DmaPoolFallbackUsed;
    ULONG DmaBufferReleased;
    ULONG DmaBufferAlreadyPresent;
    ULONG DmaPhase;
    ULONG DmaBufferLength;
    ULONG DmaLogicalAddressLow;
    ULONG DmaLogicalAddressHigh;
    ULONG DmaVirtualAddressLow;
    ULONG DmaPhysicalAddressLow;
    ULONG DmaPhysicalAddressHigh;
    ULONG DmaLogicalDiffersFromPhysical;

    /*
     * Cached bounce copy of the RAW slot that is being compacted.  Only the
     * page-list route needs it - see NabuOwnCaptureRearPersistentFrame.
     */
    PVOID FrameCachedScratch;
    ULONG FrameCachedScratchStatus;
    ULONG FrameCachedScratchUsed;

    /*
     * Per-cycle cost split, in microseconds.  Without these the rear loop
     * looks like one opaque ~125 ms step and the optical frame period cannot
     * be told apart from the CPU compaction.
     */
    ULONG PublishUsLast;
    ULONG PublishUsMax;
    ULONG PublishUsTotal;
    ULONG WaitUsLast;
    ULONG WaitUsMax;
    ULONG WaitUsTotal;
    LONGLONG FramePeriodAnchorTicks;
    ULONG FramePeriodUsLast;
    ULONG FramePeriodUsMin;
    ULONG FramePeriodUsMax;
    ULONG WaitSleepPolls;
    ULONG WaitStallPolls;
    ULONG TimerResolution100Ns;
    ULONG CycleCount;

    ULONG VcmModeRequested;
    ULONG VcmPhase;
    ULONG SmmuEnumerateStatus;
    ULONG SmmuDeviceFound;
    ULONG SmmuOpenStatus;
    ULONG SmmuQueryStatus;
    ULONG SmmuInterfaceSize;
    ULONG SmmuInterfaceVersion;
    ULONG SmmuGetConfigStatus;
    ULONG SmmuConfigSelector;
    ULONG SmmuContextCountRaw;
    ULONG SmmuContextCountAccepted;
    ULONG SmmuContextIndex[NABU_SMMU_MAX_CONTEXT_BANKS];
    ULONG SmmuContextHandleLow[NABU_SMMU_MAX_CONTEXT_BANKS];
    ULONG SmmuContextHandleHigh[NABU_SMMU_MAX_CONTEXT_BANKS];
    ULONG VcmCreateAttempted;
    ULONG VcmCreateStatus;
    ULONG VcmRegionHandleLow;
    ULONG VcmAssociationAttempted;
    ULONG VcmAssociationCount;
    ULONG VcmAssociationFailIndex;
    ULONG VcmSetStateStatus;
    ULONG VcmActivationCount;
    ULONG VcmActivationFailIndex;
    ULONG VcmReserveAttempted;
    ULONG VcmReserveStatus;
    ULONG VcmGuardReserveAttempted;
    ULONG VcmGuardReserveStatus;
    ULONG VcmGuardAddressLow;
    ULONG VcmGuardAddressHigh;
    ULONG VcmGuardReleaseStatus;
    ULONG VcmBackAttempted;
    ULONG VcmBackStatus;
    ULONG VcmGetDevAddrAttempted;
    ULONG VcmDeviceAddressLow;
    ULONG VcmDeviceAddressHigh;
    ULONG VcmDeviceDiffersFromPhysical;
    ULONG VcmUnbackStatus;
    ULONG VcmReleaseStatus;
    ULONG VcmDeactivateStatus;
    ULONG VcmSetStateOffStatus;
    ULONG VcmDeassocStatus;
    ULONG VcmFreeStatus;
    ULONG VcmCompleted;

    ULONG TpgPhase;
    ULONG TpgAttempted;
    ULONG TpgCompleted;
    ULONG TpgPlatformOpenStatus;
    ULONG TpgPlatformAcquireStatus;
    ULONG TpgPlatformPowerStatus;
    ULONG TpgPlatformReleaseStatus;
    ULONG TpgRtBandwidthStatus;
    ULONG TpgRtBandwidthReleaseStatus;
    ULONG TpgIfeGdscBefore;
    ULONG TpgIfeGdscAfter;
    ULONG TpgIfeGdscReady;
    ULONG TpgIfeClockCbcr;
    ULONG TpgIfeAxiCbcr;
    ULONG TpgIfeCsidCbcr;
    ULONG TpgCamnocAxiCbcr;
    ULONG TpgVfeResetStatus;
    ULONG TpgVfeResetPolls;
    ULONG TpgIovaLow;
    ULONG TpgIovaHigh;
    ULONG TpgWmCfg;
    ULONG TpgWmImageBefore;
    ULONG TpgWmImageAfter;
    ULONG TpgVfeBusIrq0;
    ULONG TpgVfeBusIrq1;
    ULONG TpgVfeBusIrq2;
    ULONG TpgVfeBusDebug0;
    ULONG TpgCsidRdiIrq;
    ULONG TpgCsidTotalPackets;
    ULONG TpgBufferChanged;
    ULONG TpgBufferSample0;
    ULONG TpgBufferSample1;
    ULONG TpgBufferSample2;
    ULONG TpgBufferSample3;
    ULONG TpgFileStatus;
    ULONG TpgFileBytes;

    ULONG RealPhase;
    ULONG RealAttempted;
    ULONG RealCompleted;
    ULONG RealPlatformOpenStatus;
    ULONG RealAcquire1Status;
    ULONG RealPlatformPowerStatus;
    ULONG RealPlatformPinStatus;
    ULONG RealInitMasterStatus;
    ULONG RealAcquire2Status;
    ULONG RealDeinitMasterStatus;
    ULONG RealRelease1Status;
    ULONG RealRelease2Status;
    ULONG RealRtBandwidthStatus;
    ULONG RealRtBandwidthReleaseStatus;
    ULONG RealPll0LockOk;
    ULONG RealPll3ModeBefore;
    ULONG RealPll3LBefore;
    ULONG RealPll3AlphaBefore;
    ULONG RealPll3UserBefore;
    ULONG RealPll3OpmodeBefore;
    ULONG RealPll3ModeAfter;
    ULONG RealPll3LAfter;
    ULONG RealPll3AlphaAfter;
    ULONG RealPll3UserAfter;
    ULONG RealPll3OpmodeAfter;
    ULONG RealPll3LockOk;
    ULONG RealIfeClkCmdAfter;
    ULONG RealIfeClkCfgAfter;
    ULONG RealIfeClkCbcrAfter;
    ULONG RealMclk0Cmd;
    ULONG RealMclk0Cfg;
    ULONG RealMclk0Cbcr;
    ULONG RealClockTreeReady;
    ULONG RealPowerRailsAttempted;
    ULONG RealPowerRailsReady;
    ULONG RealSpmiInitStatus;
    ULONG RealSpmiHwVersion;
    ULONG RealSpmiApidCount;
    ULONG RealVioFindStatus;
    ULONG RealVioApid;
    ULONG RealVioWriteEe;
    ULONG RealVioType;
    ULONG RealVioVsetMv;
    ULONG RealVioEnBefore;
    ULONG RealVioEnAfter;
    ULONG RealVioStatusBefore;
    ULONG RealVioStatusAfter;
    ULONG RealVioWriteStatus;
    ULONG RealVdigFindStatus;
    ULONG RealVdigApid;
    ULONG RealVdigWriteEe;
    ULONG RealVdigType;
    ULONG RealVdigSubtype;
    ULONG RealVdigModeBefore;
    ULONG RealVdigModeAfter;
    ULONG RealVdigOutBefore;
    ULONG RealVdigOutAfter;
    ULONG RealVdigEnBefore;
    ULONG RealVdigEnAfter;
    ULONG RealVdigModeWriteStatus;
    ULONG RealVdigOutWriteStatus;
    ULONG RealVdigEnWriteStatus;
    ULONG RealBobFindStatus;
    ULONG RealBobApid;
    ULONG RealBobWriteEe;
    ULONG RealBobType;
    ULONG RealBobEn;
    ULONG RealBobStatus;
    ULONG RealChipIdStatus;
    ULONG RealChipId;
    ULONG RealInitStatus;
    ULONG RealInitFailIndex;
    ULONG RealInitFailReg;
    ULONG RealModeStatus;
    ULONG RealModeFailIndex;
    ULONG RealModeFailReg;
    ULONG RealStreamWriteStatus;
    ULONG RealStreamReadStatus;
    ULONG RealStreamReadback;
    ULONG RealStreamStopStatus;
    ULONG RealCsiphyHwVersion;
    ULONG RealCsiphyIrq5;
    ULONG RealCsiphyIrq8;
    ULONG RealCsidRxCfg0;
    ULONG RealCsidRxCfg1;
    ULONG RealCsidRdiCfg0;
    ULONG RealCsidRdiIrq;
    ULONG RealCsidTotalPackets;
    ULONG RealWaitPolls;
    ULONG RealWaitExitReason;
    ULONG RealWaitPacketsFirst;
    ULONG RealWaitPacketsLast;
    ULONG RealWaitCsidIrqLast;
    ULONG RealWaitVfeTopIrqLast;
    ULONG RealWaitBusIrqLast;
    ULONG RealIovaLow;
    ULONG RealIovaHigh;
    ULONG RealWmCfg;
    ULONG RealWmStatus0;
    ULONG RealWmStatus1;
    ULONG RealWmImageBefore;
    ULONG RealWmImageAfter;
    ULONG RealVfeTopIrq0;
    ULONG RealVfeTopIrq1;
    ULONG RealVfeBusIrq0;
    ULONG RealVfeBusIrq1;
    ULONG RealVfeBusIrq2;
    ULONG RealVfeBusDebug0;
    ULONG RealBufferChanged;
    ULONG RealBufferSample0;
    ULONG RealBufferSample1;
    ULONG RealBufferSample2;
    ULONG RealBufferSample3;
    ULONG RealFrameBytesExpected;
    ULONG RealBufferTailChanged;
    ULONG RealBufferTailSample0;
    ULONG RealBufferTailSample1;
    ULONG RealFileStatus;
    ULONG RealFileBytes;

    ULONG ScfgAttempted;
    ULONG ScfgStatus;
    ULONG ScfgBytesReturned;
    ULONG ScfgOutputSignature;
    ULONG ScfgOutputLength;
    ULONG ScfgOutputCount;
    ULONG ScfgPackageType;
    ULONG ScfgPackageDataLength;
    ULONG ScfgWords[NABU_OWN_SCFG_WORD_COUNT];
    ULONG ScfgTailByte0;
    ULONG ScfgTailByte1;
    ULONG ScfgConfig0;
    ULONG ScfgConfig1;
    ULONG ScfgConfig2;

    ULONG CciConfigValid;
    ULONG CciMaster;
    ULONG CciFrequencyKHz;
    ULONG CciSlaveAddress;
    ULONG CciAddressType;
    ULONG CciDataLength;
    ULONG CciRegister;
    ULONG CciExpectedId;
    ULONG CciAttempted;
    ULONG CciGuardRejected;
    ULONG CciPhase;
    ULONG CciPlatformOpenStatus;
    ULONG CciAcquire1Status;
    ULONG CciInitMasterStatus;
    ULONG CciAcquire2Status;
    ULONG CciStopIdleStatus;
    ULONG CciStopIdleSkippedForD0;
    ULONG CciD0EntryConfirmed;
    ULONG CciReadStatus;
    ULONG CciReadAttemptCount;
    ULONG CciRxByte0;
    ULONG CciRxByte1;
    ULONG CciChipId;
    ULONG CciIdMatchesExpected;
    ULONG CciDeinitStatus;
    ULONG CciRelease1Status;
    ULONG CciResumeIdleCalled;
    ULONG CciRelease2Status;

    volatile LONG PersistentMode;
    volatile LONG PersistentStopRequested;
    volatile LONG PersistentFrameCount;
    volatile LONG PersistentFrameFailureCount;
    volatile LONG PersistentConsecutiveFailurePeak;
    volatile LONG PersistentTerminalStatus;
    volatile LONG AutoExposureRequestedExposure;
    volatile LONG AutoExposureRequestedGain;
    volatile LONG AutoExposureRequestedGeneration;
    volatile LONG AutoExposureAppliedGeneration;
    volatile LONG AutoExposureApplyStatus;
    PVOID PersistentPublishContext;
    NABU_REAR_PERSISTENT_PUBLISH PersistentPublish;
} NABU_OWN_STAGE23_CONTEXT, *PNABU_OWN_STAGE23_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(NABU_OWN_STAGE23_CONTEXT, NabuOwnGetContext);

// ============================================================================
// Exported functions from HwCapture.c
// ============================================================================

VOID NabuOwnCreateDmaBuffer(_Inout_ PNABU_OWN_STAGE23_CONTEXT Context);
VOID NabuOwnQueryScfg(_Inout_ PNABU_OWN_STAGE23_CONTEXT Context);
PVOID NabuOwnGetDmaVirtualAddress(_In_ PNABU_OWN_STAGE23_CONTEXT Context);
VOID NabuOwnReleaseDmaBuffer(_Inout_ PNABU_OWN_STAGE23_CONTEXT Context);
VOID NabuOwnRunVcmProbe(_Inout_ PNABU_OWN_STAGE23_CONTEXT Context);
VOID NabuOwnRunRealCapture(
    _Inout_ PNABU_OWN_STAGE23_CONTEXT Context,
    _Inout_updates_bytes_(NABU_OWN_DMA_BUFFER_BYTES) PUCHAR Buffer,
    _In_ ULONGLONG DeviceAddress);
VOID NabuOwnWriteDiagnostics(_In_ PNABU_OWN_STAGE23_CONTEXT Context);
