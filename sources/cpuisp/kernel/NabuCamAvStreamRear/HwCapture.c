//
// NabuCamOwnStage39.c
//
// Stage 39 of the independent Nabu camera driver.
//
// The normal load path only reads ACPI SCFG. An explicit one-shot Arm=39
// runs the proven Stage 30 sequence in D0Entry with the complete 8 MiB
// plain-16 buffer placed above the stock platform's low-IOVA allocations.
// Before MCLK/reset/CCI, it explicitly verifies and enables the rear sensor's
// PM8150L VIO (LDO1) and VDIG (GPIO3) rails through the SPMI arbiter.
//
#include <ntddk.h>
#include <wdf.h>
#include <acpiioct.h>
#include <ntstrsafe.h>
#include "Platform.h"
#include "Csiphy.h"
#include "Spmi.h"

#define NABU_OWN_STAGE39_BUILD_TAG       20261185UL
#define NABU_OWN_SCFG_OUTPUT_BYTES       0x1000UL
#define NABU_OWN_SCFG_WORD_COUNT         40UL
#define NABU_OWN_POOL_TAG                '91ON'
#define NABU_OWN_DMA_POOL_TAG            '72DN'
#define NABU_OWN_SCRATCH_POOL_TAG        '34SC'
#define NABU_OWN_ARM_REAL_CAPTURE        39UL
#define NABU_OWN_REAR_CCI_MASTER         0UL
#define NABU_OWN_CCI_READ_BYTES          0x40UL
#define NABU_OWN_DMA_BUFFER_BYTES        0x01000000UL
#define NABU_OWN_DMA_ALIGNMENT_MASK      0x00000fffUL
/* Temporary local-test switch: pretend the common buffer request failed so
 * the capture runs through the page-list route that a starved pad takes.
 */
#define NABU_OWN_FORCE_PAGE_LIST_TEST    0
/* MmAllocatePagesForMdlEx flags for the VCM page list: every page or none,
 * trim the working set if that is what it takes, and do not zero the pages
 * (the caller fills the pattern it probes for).
 */
#define NABU_VCM_PAGE_LIST_FLAGS         (MM_ALLOCATE_FULLY_REQUIRED | \
                                          MM_ALLOCATE_TRIM_IF_NECESSARY | \
                                          MM_DONT_ZERO_ALLOCATION)

#define NABU_SMMU_INTERFACE_BYTES        0x190UL
#define NABU_SMMU_DESIRED_ACCESS         0x001f01ffUL
#define NABU_SMMU_CONFIG_BYTES           0x128UL
#define NABU_SMMU_CONFIG_SELECTOR        1UL
#define NABU_SMMU_CONFIG_ENTRY_BYTES     0x18UL
#define NABU_SMMU_MAX_CONTEXT_BANKS      12UL
#define NABU_VCM_REGION_BYTES            0x80000000ULL
#define NABU_VCM_RESERVE_FLAGS           0x1e1UL
#define NABU_VCM_GUARD_BYTES             0x01000000ULL

#define NABU_CAMCC_PHYS                  0x0AD00000ULL
#define NABU_CAMCC_BYTES                 0x00010000UL
#define NABU_CSID0_PHYS                  0x0ACB3000ULL
#define NABU_CSID0_BYTES                 0x00001000UL
#define NABU_VFE0_PHYS                   0x0ACAF000ULL
#define NABU_VFE0_BYTES                  0x00004000UL
#define NABU_VFE_WM0                     0x00002200UL
#define NABU_CSIPHY0_PHYS                0x0AC65000ULL
#define NABU_CSIPHY0_BYTES               0x00002000UL
#define NABU_TLMM_NORTH_PHYS             0x03900000ULL
#define NABU_TLMM_NORTH_BYTES            0x00020000UL
#define NABU_TLMM_EAST_PHYS              0x03500000ULL
#define NABU_TLMM_EAST_BYTES             0x00020000UL
#define NABU_REAR_WIDTH                  2104UL
#define NABU_REAR_HEIGHT                 1560UL
#define NABU_REAR_STRIDE                 (NABU_REAR_WIDTH * sizeof(USHORT))
#define NABU_REAR_FRAME_BYTES            (NABU_REAR_STRIDE * NABU_REAR_HEIGHT)
#define NABU_REAR_FRAME_TIMEOUT_US       100000ULL
/*
 * Waiting for the next optical frame is the whole cost of an idle cycle, so it
 * has to be precise.  KeDelayExecutionThread cannot sleep finer than the system
 * timer resolution - on this pad a 1 ms request woke up ~10.5 ms later, which
 * is longer than the frame period itself and turned every cycle into two or
 * three missed frames.  The wait is therefore split: request a fine resolution
 * while streaming, sleep only until just before the predicted frame boundary,
 * then catch the boundary with short stalls.  The stall is bounded so a wrong
 * prediction can never burn a core for a whole timeout.
 */
#define NABU_REAR_TIMER_RESOLUTION_100NS 5000UL
#define NABU_REAR_WAIT_SLEEP_US          1000LL
#define NABU_REAR_WAIT_STALL_US          200UL
#define NABU_REAR_WAIT_STALL_BUDGET_US   8000ULL
#define NABU_REAR_WAIT_GUARD_US          3000UL
#define NABU_REAR_FRAME_PERIOD_MIN_US    4000UL
#define NABU_REAR_FRAME_PERIOD_MAX_US    60000UL
#define NABU_REAR_FRAME_PERIOD_INIT_US   25000UL
/*
 * Coarse integration window, in whole sensor lines.  These must stay in step
 * with NABU_REAR_AE_EXPOSURE_MIN/MAX in NabuCamHwRear.c, which is where the
 * control loop derives its requests from: HwCapture.c keeps its own copy of
 * the capture context rather than including HwCapture.h, so the two sides have
 * no shared header to read the limit from.  Rejecting a request the controller
 * legitimately produces leaves the sensor at its 4-line start exposure, which
 * is a black picture rather than a merely wrong one.
 */
#define NABU_REAR_EXPOSURE_MIN_LINES     4UL
/*
 * max_line has to leave a readout reserve inside VTS: the OV13B10 reports
 * exposure in lines against a 2400-line frame, so the ceiling stops 64 lines
 * short of the period instead of asking for the whole window.
 */
#define NABU_REAR_EXPOSURE_MAX_LINES     2336UL
#define NABU_REAL_MARKER                 0xABABABABUL
#define NABU_TPG_WIDTH                   128UL
#define NABU_TPG_HEIGHT                  128UL
#define NABU_TPG_STRIDE                  160UL
#define NABU_TPG_FRAME_BYTES             (NABU_TPG_STRIDE * NABU_TPG_HEIGHT)
#define NABU_TPG_MARKER                  0xABABABABUL
#define NABU_RT_BANDWIDTH                3000000000ULL
#define NABU_TLMM_CTL(gpio)              (0x1000UL * (ULONG)(gpio))
#define NABU_TLMM_IO(gpio)               (NABU_TLMM_CTL(gpio) + 4UL)
#define NABU_REAR_VANA_GPIO              11UL
#define NABU_REAR_MCLK_GPIO              13UL
#define NABU_REAR_RESET_GPIO             28UL

#define NABU_SMMU_SET_STATE_OFFSET       0x28UL
#define NABU_SMMU_GET_CONFIG_OFFSET      0x30UL
#define NABU_SMMU_LEGACY_CB_OFFSET       0x38UL
#define NABU_SMMU_LEGACY_CB_REMOVE_OFFSET 0x40UL
#define NABU_VCM_CREATE_OFFSET           0x50UL
#define NABU_VCM_FREE_OFFSET             0x58UL
#define NABU_VCM_RESERVE_OFFSET          0x60UL
#define NABU_VCM_RELEASE_OFFSET          0x78UL
#define NABU_VCM_BACK_OFFSET             0x90UL
#define NABU_VCM_UNBACK_OFFSET           0x98UL
#define NABU_VCM_ASSOC_OFFSET            0xa0UL
#define NABU_VCM_DEASSOC_OFFSET          0xa8UL
#define NABU_VCM_ACTIVATE_OFFSET         0xb0UL
#define NABU_VCM_DEACTIVATE_OFFSET       0xb8UL
#define NABU_VCM_GET_DEV_ADDR_OFFSET     0xc0UL
#define NABU_IOMMU_IS_IOMMU_OFFSET       0x180UL

#define NABU_PLAT_CMD_ACQUIRE            0x232017UL
#define NABU_PLAT_CMD_RELEASE            0x23201BUL

static const GUID NabuOwnSmmuDeviceInterfaceGuid =
    { 0x36079ae4, 0x78e8, 0x452d, { 0xaf, 0x50, 0x0c, 0xff, 0x78, 0xb2, 0xf1, 0xca } };
static const GUID NabuOwnSmmuQueryInterfaceGuid =
    { 0xb7ddb2a6, 0x7c10, 0x4129, { 0x81, 0xcc, 0x4b, 0xc5, 0x2b, 0x48, 0xed, 0x95 } };

typedef struct _NABU_SENSOR_REG8 {
    USHORT Register;
    UCHAR Value;
} NABU_SENSOR_REG8;

static const NABU_SENSOR_REG8 NabuOwnOv13b10Init[] = {
    {0x0303,0x04},{0x0305,0xaf},{0x0321,0x00},{0x0323,0x04},
    {0x0324,0x01},{0x0325,0xa4},{0x0326,0x81},{0x0327,0x04},
    {0x3012,0x07},{0x3013,0x32},{0x3107,0x23},{0x3501,0x0c},
    {0x3502,0x10},{0x3504,0x08},{0x3508,0x07},{0x3509,0xc0},
    {0x3600,0x16},{0x3601,0x54},{0x3612,0x4e},{0x3620,0x00},
    {0x3621,0x68},{0x3622,0x66},{0x3623,0x03},{0x3662,0x92},
    {0x3666,0xbb},{0x3667,0x44},{0x366e,0xff},{0x366f,0xf3},
    {0x3675,0x44},{0x3676,0x00},{0x367f,0xe9},{0x3681,0x32},
    {0x3682,0x1f},{0x3683,0x0b},{0x3684,0x0b},{0x3704,0x0f},
    {0x3706,0x40},{0x3708,0x3b},{0x3709,0x72},{0x370b,0xa2},
    {0x3714,0x24},{0x371a,0x3e},{0x3725,0x42},{0x3739,0x12},
    {0x3767,0x00},{0x377a,0x0d},{0x3789,0x18},{0x3790,0x40},
    {0x3791,0xa2},{0x37c2,0x04},{0x37c3,0xf1},{0x37d9,0x0c},
    {0x37da,0x02},{0x37dc,0x02},{0x37e1,0x04},{0x37e2,0x0a},
    {0x3800,0x00},{0x3801,0x00},{0x3802,0x00},{0x3803,0x08},
    {0x3804,0x10},{0x3805,0x8f},{0x3806,0x0c},{0x3807,0x47},
    {0x3808,0x10},{0x3809,0x70},{0x380a,0x0c},{0x380b,0x30},
    {0x380c,0x04},{0x380d,0x98},{0x380e,0x0c},{0x380f,0x7c},
    {0x3811,0x0f},{0x3813,0x09},{0x3814,0x01},{0x3815,0x01},
    {0x3816,0x01},{0x3817,0x01},{0x381f,0x08},{0x3820,0x88},
    {0x3821,0x00},{0x3822,0x14},{0x382e,0xe6},{0x3c80,0x00},
    {0x3c87,0x01},{0x3c8c,0x19},{0x3c8d,0x1c},{0x3ca0,0x00},
    {0x3ca1,0x00},{0x3ca2,0x00},{0x3ca3,0x00},{0x3ca4,0x50},
    {0x3ca5,0x11},{0x3ca6,0x01},{0x3ca7,0x00},{0x3ca8,0x00},
    {0x4008,0x02},{0x4009,0x0f},{0x400a,0x01},{0x400b,0x19},
    {0x4011,0x21},{0x4017,0x08},{0x4019,0x04},{0x401a,0x58},
    {0x4032,0x1e},{0x4050,0x02},{0x4051,0x09},{0x405e,0x00},
    {0x4066,0x02},{0x4501,0x00},{0x4502,0x10},{0x4505,0x00},
    {0x4800,0x64},{0x481b,0x3e},{0x481f,0x30},{0x4825,0x34},
    {0x4837,0x0e},{0x484b,0x01},{0x4883,0x02},{0x5000,0xff},
    {0x5001,0x0f},{0x5045,0x20},{0x5046,0x20},{0x5047,0xa4},
    {0x5048,0x20},{0x5049,0xa4}
};

static const NABU_SENSOR_REG8 NabuOwnOv13b10Mode2104x1560[] = {
    /*
     * VTS is 2400 here, not mainline's 1598 (0x063e) for this mode.  The
     * preview loop spends ~15 ms per frame on the CPU compaction, so it never
     * reaches the 60 fps the programmed mode implies - it delivers whatever the
     * optical period allows.  A longer frame lets the shutter stay open 1.5x
     * longer, which is what took the rear out of saturated analogue gain; the
     * cost is one more write-master re-arm per cycle, and the coarse
     * integration ceiling keeps a readout reserve inside these 2400 lines.
     */
    /*
     * The base table leaves analogue gain at 0x07c0 (the sensor maximum).
     * Android/Linux applies the OV13B10 control default before stream-on:
     * 0x80 << 1 = 0x0100.  Apply that documented 1x starting gain here so
     * the first optical frames retain highlights instead of overexposing.
     */
    {0x0305,0xaf},{0x3500,0x00},{0x3501,0x04},{0x3502,0x00},
    {0x3508,0x01},{0x3509,0x00},
    {0x3662,0x88},{0x3714,0x28},
    {0x3739,0x10},{0x37c2,0x14},{0x37d9,0x06},{0x37e2,0x0c},
    {0x3800,0x00},{0x3801,0x00},{0x3802,0x00},{0x3803,0x08},
    {0x3804,0x10},{0x3805,0x8f},{0x3806,0x0c},{0x3807,0x47},
    {0x3808,0x08},{0x3809,0x38},{0x380a,0x06},{0x380b,0x18},
    {0x380c,0x04},{0x380d,0x98},{0x380e,0x09},{0x380f,0x60},
    {0x3810,0x00},{0x3811,0x07},{0x3812,0x00},{0x3813,0x05},
    {0x3814,0x03},{0x3816,0x03},{0x3820,0x8b},{0x3c8c,0x18},
    {0x4008,0x00},{0x4009,0x05},{0x4050,0x00},{0x4051,0x05},
    {0x4501,0x08},{0x4505,0x00},{0x4837,0x0e},{0x5000,0xfd},
    {0x5001,0x0d}
};

typedef struct _NABU_SMMU_INTERFACE {
    INTERFACE Header;
    UCHAR Private[NABU_SMMU_INTERFACE_BYTES - sizeof(INTERFACE)];
} NABU_SMMU_INTERFACE, *PNABU_SMMU_INTERFACE;

typedef NTSTATUS (*NABU_SMMU_SET_STATE)(PVOID, ULONG);
typedef NTSTATUS (*NABU_SMMU_GET_CONFIG)(PVOID, ULONG, PVOID);
typedef NTSTATUS (*NABU_SMMU_LEGACY_STATE_CALLBACK)(PVOID, PVOID);
typedef VOID (*NABU_SMMU_LEGACY_CALLBACK_REGISTER)(
    PVOID, ULONG, NABU_SMMU_LEGACY_STATE_CALLBACK, PVOID);
typedef VOID (*NABU_SMMU_LEGACY_CALLBACK_REMOVE)(PVOID, ULONG);
typedef PVOID (*NABU_VCM_CREATE)(ULONGLONG, ULONGLONG);
typedef NTSTATUS (*NABU_VCM_FREE)(PVOID);
typedef PVOID (*NABU_VCM_RESERVE)(PVOID, ULONGLONG, ULONG);
typedef NTSTATUS (*NABU_VCM_HANDLE_STATUS)(PVOID);
typedef NTSTATUS (*NABU_VCM_BACK)(PVOID, PMDL);
typedef PVOID (*NABU_VCM_ASSOC)(PVOID, PVOID, ULONG);
typedef ULONGLONG (*NABU_VCM_GET_DEV_ADDR)(PVOID);
typedef BOOLEAN (*NABU_SMMU_IS_IOMMU)(VOID);

typedef VOID
(*NABU_REAR_PERSISTENT_PUBLISH)(
    _In_opt_ PVOID PublishContext,
    _In_reads_bytes_(FrameBytes) const UCHAR *Frame,
    _In_ ULONG FrameBytes);

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
     * Mirrors HwCapture.h exactly.  NabuCamHwRear.c includes the header while
     * this file keeps its own copy of the context, so the two must be edited
     * together or the fields land at different offsets in the two objects.
     */
    PVOID FrameCachedScratch;
    ULONG FrameCachedScratchStatus;
    ULONG FrameCachedScratchUsed;
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

#if !defined(AVSTREAM_BUILD) && !defined(NABU_REAR_CAPTURE_LIBRARY)
DRIVER_INITIALIZE DriverEntry;

EVT_WDF_DRIVER_DEVICE_ADD NabuOwnEvtDeviceAdd;
EVT_WDF_DEVICE_PREPARE_HARDWARE NabuOwnEvtPrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE NabuOwnEvtReleaseHardware;
EVT_WDF_DEVICE_D0_ENTRY NabuOwnEvtD0Entry;

#endif
VOID NabuOwnWriteDiagnostics(_In_ PNABU_OWN_STAGE23_CONTEXT Context);

static VOID
NabuOwnWriteValue(
    _In_ WDFKEY Key,
    _In_z_ PCWSTR Name,
    _In_ ULONG Value)
{
    UNICODE_STRING valueName;
    RtlInitUnicodeString(&valueName, Name);
    (VOID)WdfRegistryAssignULong(Key, &valueName, Value);
}

static VOID
NabuOwnRemoveValue(
    _In_ WDFKEY Key,
    _In_z_ PCWSTR Name)
{
    UNICODE_STRING valueName;

    RtlInitUnicodeString(&valueName, Name);
    (VOID)WdfRegistryRemoveValue(Key, &valueName);
}

static USHORT
NabuOwnReadU16(_In_reads_bytes_(2) const UCHAR *Source)
{
    USHORT value = 0;
    RtlCopyMemory(&value, Source, sizeof(value));
    return value;
}

VOID
NabuOwnReadAndClearArm(_In_ PNABU_OWN_STAGE23_CONTEXT Context)
{
    WDFKEY key = NULL;
    UNICODE_STRING name;
    ULONG arm = 0;
    ULONG mode = 1;

    if (!NT_SUCCESS(WdfDeviceOpenRegistryKey(Context->Device,
                                              PLUGPLAY_REGKEY_DEVICE,
                                              KEY_QUERY_VALUE | KEY_SET_VALUE,
                                              WDF_NO_OBJECT_ATTRIBUTES,
                                              &key))) {
        return;
    }

    RtlInitUnicodeString(&name, L"NabuOwn_Arm");
    if (NT_SUCCESS(WdfRegistryQueryULong(key, &name, &arm))) {
        Context->ArmRequested = arm;
    }

    RtlInitUnicodeString(&name, L"NabuOwn_VcmMode");
    if (NT_SUCCESS(WdfRegistryQueryULong(key, &name, &mode)) &&
        mode >= 1 && mode <= 5) {
        Context->VcmModeRequested = mode;
    } else {
        Context->VcmModeRequested = 1;
    }
    if (Context->ArmRequested == NABU_OWN_ARM_REAL_CAPTURE) {
        Context->VcmModeRequested = 5;
    }

    // Clear before any platform call.  A reboot can never repeat a test.
    RtlInitUnicodeString(&name, L"NabuOwn_Arm");
    (VOID)WdfRegistryAssignULong(key, &name, 0);
    WdfRegistryClose(key);
}

static VOID
NabuOwnDecodeRearScfg(
    _Inout_ PNABU_OWN_STAGE23_CONTEXT Context,
    _In_reads_bytes_(BytesReturned) const UCHAR *Output,
    _In_ size_t BytesReturned)
{
    ULONG flags;
    ULONG frequencyCode;
    ULONG addressCode;
    ULONG dataCode;

    // SCFG package layout returned by the Nabu rear ACPI child:
    // two strings, then three 12-byte compact configuration records at 0x70.
    if (BytesReturned < 0x8e) {
        return;
    }
    if (NabuOwnReadU16(Output + 0x70) != 8 ||
        NabuOwnReadU16(Output + 0x7c) != 8 ||
        NabuOwnReadU16(Output + 0x88) != 8) {
        return;
    }

    Context->ScfgConfig0 = ((ULONG)NabuOwnReadU16(Output + 0x74) << 16) |
                           NabuOwnReadU16(Output + 0x72);
    Context->ScfgConfig1 = ((ULONG)NabuOwnReadU16(Output + 0x80) << 16) |
                           NabuOwnReadU16(Output + 0x7e);
    Context->ScfgConfig2 = ((ULONG)NabuOwnReadU16(Output + 0x8c) << 16) |
                           NabuOwnReadU16(Output + 0x8a);

    flags = Context->ScfgConfig0 >> 16;
    frequencyCode = flags & 0x3;
    addressCode = (flags >> 2) & 0x3;
    dataCode = (flags >> 4) & 0x3;

    if (frequencyCode == 0) {
        Context->CciFrequencyKHz = 100;
    } else if (frequencyCode == 1) {
        Context->CciFrequencyKHz = 400;
    } else if (frequencyCode == 2) {
        Context->CciFrequencyKHz = 1000;
    } else {
        return;
    }

    Context->CciMaster = NABU_OWN_REAR_CCI_MASTER;
    Context->CciSlaveAddress = Context->ScfgConfig0 & 0xffff;
    Context->CciAddressType = addressCode + 1;
    Context->CciDataLength = dataCode + 1;
    Context->CciRegister = Context->ScfgConfig1 & 0xffff;
    Context->CciExpectedId = Context->ScfgConfig1 >> 16;

    // A hardware test is allowed only for the exact rear-sensor parameters
    // verified in this ACPI response.  Any unexpected layout is a no-op.
    if (Context->CciMaster == 0 &&
        Context->CciSlaveAddress == 0x20 &&
        Context->CciFrequencyKHz == 400 &&
        Context->CciAddressType == 2 &&
        Context->CciDataLength == 2 &&
        Context->CciRegister == 0x300b &&
        Context->CciExpectedId == 0x0d42) {
        Context->CciConfigValid = 1;
    }
}

VOID
NabuOwnQueryScfg(_Inout_ PNABU_OWN_STAGE23_CONTEXT Context)
{
    ACPI_EVAL_INPUT_BUFFER_COMPLEX_V1_EX input;
    WDF_MEMORY_DESCRIPTOR inputDescriptor;
    WDF_MEMORY_DESCRIPTOR outputDescriptor;
    WDFIOTARGET target;
    PUCHAR output;
    size_t bytesReturned = 0;
    ULONG availableWords;
    ULONG i;
    NTSTATUS status;

    Context->ScfgAttempted = 1;
    Context->ScfgStatus = (ULONG)STATUS_UNSUCCESSFUL;

    output = (PUCHAR)ExAllocatePool2(POOL_FLAG_NON_PAGED,
                                     NABU_OWN_SCFG_OUTPUT_BYTES,
                                     NABU_OWN_POOL_TAG);
    if (output == NULL) {
        Context->ScfgStatus = (ULONG)STATUS_INSUFFICIENT_RESOURCES;
        return;
    }

    RtlZeroMemory(&input, sizeof(input));
    RtlZeroMemory(output, NABU_OWN_SCFG_OUTPUT_BYTES);
    input.Signature = ACPI_EVAL_INPUT_BUFFER_COMPLEX_SIGNATURE_EX;
    RtlCopyMemory(input.MethodName, "SCFG", sizeof("SCFG"));
    input.Size = sizeof(ACPI_METHOD_ARGUMENT_V1);
    input.ArgumentCount = 0;

    target = WdfDeviceGetIoTarget(Context->Device);
    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&inputDescriptor, &input, sizeof(input));
    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&outputDescriptor, output,
                                      NABU_OWN_SCFG_OUTPUT_BYTES);
    status = WdfIoTargetSendIoctlSynchronously(target,
                                                WDF_NO_HANDLE,
                                                IOCTL_ACPI_EVAL_METHOD_EX,
                                                &inputDescriptor,
                                                &outputDescriptor,
                                                NULL,
                                                &bytesReturned);
    Context->ScfgStatus = (ULONG)status;
    Context->ScfgBytesReturned = (ULONG)bytesReturned;

    availableWords = (ULONG)(bytesReturned / sizeof(ULONG));
    if (availableWords > NABU_OWN_SCFG_WORD_COUNT) {
        availableWords = NABU_OWN_SCFG_WORD_COUNT;
    }
    for (i = 0; i < availableWords; ++i) {
        Context->ScfgWords[i] = ((PULONG)output)[i];
    }
    if (bytesReturned > 144) {
        Context->ScfgTailByte0 = output[144];
    }
    if (bytesReturned > 145) {
        Context->ScfgTailByte1 = output[145];
    }

    if (NT_SUCCESS(status) && bytesReturned >= sizeof(ACPI_EVAL_OUTPUT_BUFFER_V1)) {
        PACPI_EVAL_OUTPUT_BUFFER_V1 eval = (PACPI_EVAL_OUTPUT_BUFFER_V1)output;
        Context->ScfgOutputSignature = eval->Signature;
        Context->ScfgOutputLength = eval->Length;
        Context->ScfgOutputCount = eval->Count;
        if (bytesReturned >= FIELD_OFFSET(ACPI_EVAL_OUTPUT_BUFFER_V1, Argument) +
                             sizeof(ACPI_METHOD_ARGUMENT_V1)) {
            Context->ScfgPackageType = eval->Argument[0].Type;
            Context->ScfgPackageDataLength = eval->Argument[0].DataLength;
        }
        NabuOwnDecodeRearScfg(Context, output, bytesReturned);
    }

    ExFreePoolWithTag(output, NABU_OWN_POOL_TAG);
}

static VOID
NabuOwnBuildStockCciRead(
    _Out_writes_bytes_(NABU_OWN_CCI_READ_BYTES) UCHAR Request[NABU_OWN_CCI_READ_BYTES],
    _In_ ULONG FrequencyKHz,
    _In_ UCHAR SlaveAddress,
    _In_ ULONG AddressType,
    _Out_writes_bytes_(Length) UCHAR *Data,
    _In_ USHORT RegisterAddress,
    _In_ USHORT Length,
    _In_ ULONG Master)
{
    RtlZeroMemory(Request, NABU_OWN_CCI_READ_BYTES);
    RtlCopyMemory(Request + 0x04, &FrequencyKHz, sizeof(FrequencyKHz));
    RtlCopyMemory(Request + 0x08, &SlaveAddress, sizeof(SlaveAddress));
    RtlCopyMemory(Request + 0x0c, &AddressType, sizeof(AddressType));
    RtlCopyMemory(Request + 0x18, &Data, sizeof(Data));
    RtlCopyMemory(Request + 0x28, &RegisterAddress, sizeof(RegisterAddress));
    RtlCopyMemory(Request + 0x2a, &Length, sizeof(Length));
    RtlCopyMemory(Request + 0x3c, &Master, sizeof(Master));
}

static VOID
NabuOwnBuildStockCciWrite(
    _Out_writes_bytes_(NABU_OWN_CCI_READ_BYTES) UCHAR Request[NABU_OWN_CCI_READ_BYTES],
    _In_ ULONG FrequencyKHz,
    _In_ UCHAR SlaveAddress,
    _In_ ULONG AddressType,
    _In_reads_bytes_(Length) UCHAR *Data,
    _In_ USHORT RegisterAddress,
    _In_ USHORT Length,
    _In_ ULONG Master)
{
    USHORT count = 1;

    RtlZeroMemory(Request, NABU_OWN_CCI_READ_BYTES);
    RtlCopyMemory(Request + 0x04, &FrequencyKHz, sizeof(FrequencyKHz));
    RtlCopyMemory(Request + 0x08, &SlaveAddress, sizeof(SlaveAddress));
    RtlCopyMemory(Request + 0x0c, &AddressType, sizeof(AddressType));
    RtlCopyMemory(Request + 0x18, &Data, sizeof(Data));
    RtlCopyMemory(Request + 0x20, &Data, sizeof(Data));
    RtlCopyMemory(Request + 0x28, &RegisterAddress, sizeof(RegisterAddress));
    RtlCopyMemory(Request + 0x2a, &Length, sizeof(Length));
    RtlCopyMemory(Request + 0x2c, &count, sizeof(count));
    RtlCopyMemory(Request + 0x3c, &Master, sizeof(Master));
}

static VOID
NabuOwnRunCciProbe(_Inout_ PNABU_OWN_STAGE23_CONTEXT Context)
{
    PLATFORM_CLIENT client;
    UCHAR request[NABU_OWN_CCI_READ_BYTES];
    UCHAR receive[2];
    ULONG master = NABU_OWN_REAR_CCI_MASTER;
    ULONG attempt;
    BOOLEAN opened = FALSE;
    BOOLEAN acquired1 = FALSE;
    BOOLEAN initialized = FALSE;
    BOOLEAN acquired2 = FALSE;
    NTSTATUS status;

    Context->CciAttempted = 1;
    Context->HardwareTouched = 1;
    Context->CciPhase = 1;
    NabuOwnWriteDiagnostics(Context);

    status = PlatformOpen(Context->Device, &client);
    Context->CciPlatformOpenStatus = (ULONG)status;
    Context->CciPhase = 2;
    NabuOwnWriteDiagnostics(Context);
    if (!NT_SUCCESS(status)) {
        return;
    }
    opened = TRUE;

    status = PlatformDispatch(&client, NABU_PLAT_CMD_ACQUIRE, NULL, NULL, NULL);
    Context->CciAcquire1Status = (ULONG)status;
    Context->CciPhase = 3;
    NabuOwnWriteDiagnostics(Context);
    if (!NT_SUCCESS(status)) {
        goto Cleanup;
    }
    acquired1 = TRUE;

    status = PlatformInitMaster(&client, master);
    Context->CciInitMasterStatus = (ULONG)status;
    Context->CciPhase = 4;
    NabuOwnWriteDiagnostics(Context);
    if (!NT_SUCCESS(status)) {
        goto Cleanup;
    }
    initialized = TRUE;

    status = PlatformDispatch(&client, NABU_PLAT_CMD_ACQUIRE, NULL, NULL, NULL);
    Context->CciAcquire2Status = (ULONG)status;
    Context->CciPhase = 5;
    NabuOwnWriteDiagnostics(Context);
    if (!NT_SUCCESS(status)) {
        goto Cleanup;
    }
    acquired2 = TRUE;

    // EvtDeviceD0Entry is only called after KMDF has brought the device into
    // D0.  Calling WdfDeviceStopIdle from PrepareHardware was rejected with
    // STATUS_POWER_STATE_INVALID on Nabu (Stage 19), so do not repeat it.
    Context->CciStopIdleSkippedForD0 = 1;
    Context->CciD0EntryConfirmed = 1;
    Context->CciPhase = 6;
    NabuOwnWriteDiagnostics(Context);

    for (attempt = 1; attempt <= 3; ++attempt) {
        RtlZeroMemory(receive, sizeof(receive));
        NabuOwnBuildStockCciRead(request,
                                 Context->CciFrequencyKHz,
                                 (UCHAR)Context->CciSlaveAddress,
                                 Context->CciAddressType,
                                 receive,
                                 (USHORT)Context->CciRegister,
                                 (USHORT)Context->CciDataLength,
                                 Context->CciMaster);
        status = PlatformDispatch(&client, PLAT_CMD_I2C_READ, NULL, NULL, request);
        Context->CciReadStatus = (ULONG)status;
        Context->CciReadAttemptCount = attempt;
        Context->CciRxByte0 = receive[0];
        Context->CciRxByte1 = receive[1];
        Context->CciPhase = 7;
        NabuOwnWriteDiagnostics(Context);
        if (NT_SUCCESS(status)) {
            Context->CciChipId = ((ULONG)receive[0] << 8) | receive[1];
            Context->CciIdMatchesExpected =
                (Context->CciChipId == Context->CciExpectedId) ? 1 : 0;
            break;
        }
    }

Cleanup:
    if (initialized) {
        status = PlatformDispatch(&client, PLAT_CMD_I2C_MASTER_DEINIT,
                                  &master, NULL, NULL);
        Context->CciDeinitStatus = (ULONG)status;
        Context->CciPhase = 8;
        NabuOwnWriteDiagnostics(Context);
    }
    if (acquired1) {
        status = PlatformDispatch(&client, NABU_PLAT_CMD_RELEASE, NULL, NULL, NULL);
        Context->CciRelease1Status = (ULONG)status;
        Context->CciPhase = 9;
        NabuOwnWriteDiagnostics(Context);
    }
    if (acquired2) {
        status = PlatformDispatch(&client, NABU_PLAT_CMD_RELEASE, NULL, NULL, NULL);
        Context->CciRelease2Status = (ULONG)status;
        Context->CciPhase = 10;
        NabuOwnWriteDiagnostics(Context);
    }
    if (opened) {
        PlatformClose(&client);
    }
}

VOID
NabuOwnCreateDmaBuffer(_Inout_ PNABU_OWN_STAGE23_CONTEXT Context)
{
    WDF_COMMON_BUFFER_CONFIG bufferConfig;
    PHYSICAL_ADDRESS logicalAddress;
    PHYSICAL_ADDRESS physicalAddress;
    PVOID virtualAddress = NULL;
    NTSTATUS status;

    Context->DmaBufferAttempted = 1;
    Context->DmaPhase = 1;
    NabuOwnWriteDiagnostics(Context);

    if (Context->DmaEnabler == NULL) {
        Context->DmaBufferCreateStatus = Context->DmaEnablerCreateStatus;
        Context->DmaPhase = 2;
        NabuOwnWriteDiagnostics(Context);
        return;
    }

    if (Context->DmaCommonBuffer != NULL || Context->DmaPoolBuffer != NULL) {
        Context->DmaBufferAlreadyPresent = 1;
        Context->DmaPhase = 3;
        NabuOwnWriteDiagnostics(Context);
        return;
    }

    WDF_COMMON_BUFFER_CONFIG_INIT(&bufferConfig, NABU_OWN_DMA_ALIGNMENT_MASK);
    status = WdfCommonBufferCreateWithConfig(Context->DmaEnabler,
                                              NABU_OWN_DMA_BUFFER_BYTES,
                                              &bufferConfig,
                                              WDF_NO_OBJECT_ATTRIBUTES,
                                              &Context->DmaCommonBuffer);
#if NABU_OWN_FORCE_PAGE_LIST_TEST
    if (NT_SUCCESS(status)) {
        WdfObjectDelete(Context->DmaCommonBuffer);
        Context->DmaCommonBuffer = NULL;
        status = STATUS_INSUFFICIENT_RESOURCES;
    }
#endif
    Context->DmaCommonBufferCreateStatus = (ULONG)status;
    Context->DmaPhase = 4;
    NabuOwnWriteDiagnostics(Context);
    if (NT_SUCCESS(status)) {
        virtualAddress = WdfCommonBufferGetAlignedVirtualAddress(
            Context->DmaCommonBuffer);
        logicalAddress = WdfCommonBufferGetAlignedLogicalAddress(
            Context->DmaCommonBuffer);
        physicalAddress = MmGetPhysicalAddress(virtualAddress);
        Context->DmaBufferCreateStatus = (ULONG)STATUS_SUCCESS;
        Context->DmaBufferLength = (ULONG)WdfCommonBufferGetLength(
            Context->DmaCommonBuffer);
        Context->DmaLogicalAddressLow = logicalAddress.LowPart;
        Context->DmaLogicalAddressHigh = logicalAddress.HighPart;
        Context->DmaVirtualAddressLow = (ULONG)(ULONG_PTR)virtualAddress;
        Context->DmaPhysicalAddressLow = physicalAddress.LowPart;
        Context->DmaPhysicalAddressHigh = physicalAddress.HighPart;
        Context->DmaLogicalDiffersFromPhysical =
            (logicalAddress.QuadPart != physicalAddress.QuadPart) ? 1UL : 0UL;
        Context->DmaPhase = 6;
        NabuOwnWriteDiagnostics(Context);
        return;
    }

    Context->DmaPoolBuffer = ExAllocatePool2(POOL_FLAG_NON_PAGED,
                                              NABU_OWN_DMA_BUFFER_BYTES,
                                              NABU_OWN_DMA_POOL_TAG);
    if (Context->DmaPoolBuffer == NULL) {
        Context->DmaBufferCreateStatus = (ULONG)STATUS_INSUFFICIENT_RESOURCES;
        Context->DmaPhase = 8;
        NabuOwnWriteDiagnostics(Context);
        return;
    }

    virtualAddress = Context->DmaPoolBuffer;
    physicalAddress = MmGetPhysicalAddress(virtualAddress);
    Context->DmaPoolFallbackUsed = 1;
    Context->DmaBufferCreateStatus = (ULONG)STATUS_SUCCESS;
    Context->DmaBufferLength = NABU_OWN_DMA_BUFFER_BYTES;
    Context->DmaLogicalAddressLow = 0;
    Context->DmaLogicalAddressHigh = 0;
    Context->DmaVirtualAddressLow = (ULONG)(ULONG_PTR)virtualAddress;
    Context->DmaPhysicalAddressLow = physicalAddress.LowPart;
    Context->DmaPhysicalAddressHigh = physicalAddress.HighPart;
    Context->DmaLogicalDiffersFromPhysical = 1;
    Context->DmaPhase = 7;
    NabuOwnWriteDiagnostics(Context);
}

PVOID
NabuOwnGetDmaVirtualAddress(_In_ PNABU_OWN_STAGE23_CONTEXT Context)
{
    if (Context->DmaCommonBuffer != NULL) {
        return WdfCommonBufferGetAlignedVirtualAddress(Context->DmaCommonBuffer);
    }
    return Context->DmaPoolBuffer;
}

VOID
NabuOwnReleaseDmaBuffer(_Inout_ PNABU_OWN_STAGE23_CONTEXT Context)
{
    if (Context->DmaCommonBuffer != NULL) {
        WdfObjectDelete(Context->DmaCommonBuffer);
        Context->DmaCommonBuffer = NULL;
    }
    if (Context->DmaPoolBuffer != NULL) {
        ExFreePoolWithTag(Context->DmaPoolBuffer, NABU_OWN_DMA_POOL_TAG);
        Context->DmaPoolBuffer = NULL;
    }
    if (Context->FrameCachedScratch != NULL) {
        ExFreePoolWithTag(
            Context->FrameCachedScratch, NABU_OWN_SCRATCH_POOL_TAG);
        Context->FrameCachedScratch = NULL;
    }
    Context->DmaBufferReleased = 1;
    NabuOwnWriteDiagnostics(Context);
}

static PVOID
NabuOwnGetInterfaceFunction(
    _In_reads_bytes_(NABU_SMMU_INTERFACE_BYTES) const UCHAR *Table,
    _In_ ULONG Offset)
{
    PVOID function = NULL;

    if (Offset <= NABU_SMMU_INTERFACE_BYTES - sizeof(function)) {
        RtlCopyMemory(&function, Table + Offset, sizeof(function));
    }
    return function;
}

static ULONG
NabuOwnRead32(_In_ PUCHAR Base, _In_ ULONG Offset)
{
    return *(volatile ULONG *)(Base + Offset);
}

static VOID
NabuOwnWrite32(_In_ PUCHAR Base, _In_ ULONG Offset, _In_ ULONG Value)
{
    *(volatile ULONG *)(Base + Offset) = Value;
}

static ULONG
NabuOwnEnableBranch(_In_ PUCHAR Camcc, _In_ ULONG Offset)
{
    ULONG value;
    ULONG elapsed;

    value = NabuOwnRead32(Camcc, Offset);
    NabuOwnWrite32(Camcc, Offset, value | 1UL);
    KeMemoryBarrier();
    for (elapsed = 0; elapsed < 5000; elapsed += 10) {
        value = NabuOwnRead32(Camcc, Offset);
        if ((value & 0x80000000UL) == 0) {
            break;
        }
        KeStallExecutionProcessor(10);
    }
    return value;
}

static BOOLEAN
NabuOwnConfigureIfePll3For558Mhz(
    _Inout_ PNABU_OWN_STAGE23_CONTEXT Context,
    _In_ PUCHAR Camcc)
{
    const ULONG pll = 0x3000;
    ULONG mode;
    ULONG user;
    ULONG elapsed;

    Context->RealPll3ModeBefore = NabuOwnRead32(Camcc, pll + 0x00);
    Context->RealPll3LBefore = NabuOwnRead32(Camcc, pll + 0x04);
    Context->RealPll3AlphaBefore = NabuOwnRead32(Camcc, pll + 0x40);
    Context->RealPll3UserBefore = NabuOwnRead32(Camcc, pll + 0x0C);
    Context->RealPll3OpmodeBefore = NabuOwnRead32(Camcc, pll + 0x38);

    // Qualcomm's 558 MHz IFE step is PLL3_OUT_EVEN.  Program PLL3 to
    // 1116 MHz (19.2 MHz * (0x3A + 0x2000 / 65536)) and divide by two.
    mode = Context->RealPll3ModeBefore & ~1UL;
    NabuOwnWrite32(Camcc, pll + 0x00, mode);
    user = Context->RealPll3UserBefore & ~7UL;
    NabuOwnWrite32(Camcc, pll + 0x0C, user);
    NabuOwnWrite32(Camcc, pll + 0x38, 0);
    NabuOwnWrite32(Camcc, pll + 0x00,
                   (mode | (1UL << 23)) & ~(1UL << 2));
    KeMemoryBarrier();

    NabuOwnWrite32(Camcc, pll + 0x04, 0x3A);
    NabuOwnWrite32(Camcc, pll + 0x08, 0x44);
    NabuOwnWrite32(Camcc, pll + 0x40, 0x2000);
    NabuOwnWrite32(Camcc, pll + 0x18, 0x20485699);
    NabuOwnWrite32(Camcc, pll + 0x1C, 0x00002267);
    NabuOwnWrite32(Camcc, pll + 0x20, 0x00000024);
    NabuOwnWrite32(Camcc, pll + 0x0C, 0x00000100);
    NabuOwnWrite32(Camcc, pll + 0x10, 0x00000805);
    NabuOwnWrite32(Camcc, pll + 0x14, 0x000000D0);
    NabuOwnWrite32(Camcc, pll + 0x24, 0);
    NabuOwnWrite32(Camcc, pll + 0x28, 0x00000002);
    NabuOwnWrite32(Camcc, pll + 0x2C, 0);

    mode = NabuOwnRead32(Camcc, pll + 0x00);
    NabuOwnWrite32(Camcc, pll + 0x00,
                   (mode | (1UL << 23) | (1UL << 2)) & ~1UL);
    KeMemoryBarrier();
    KeStallExecutionProcessor(10);
    NabuOwnWrite32(Camcc, pll + 0x38, 1);
    KeMemoryBarrier();

    for (elapsed = 0; elapsed < 5000; elapsed += 10) {
        mode = NabuOwnRead32(Camcc, pll + 0x00);
        if ((mode & (1UL << 31)) != 0) {
            break;
        }
        KeStallExecutionProcessor(10);
    }

    if ((mode & (1UL << 31)) != 0) {
        NabuOwnWrite32(Camcc, pll + 0x0C,
                       NabuOwnRead32(Camcc, pll + 0x0C) | 7UL);
        NabuOwnWrite32(Camcc, pll + 0x00,
                       NabuOwnRead32(Camcc, pll + 0x00) | 1UL);
        KeMemoryBarrier();
        KeStallExecutionProcessor(100);
    }

    Context->RealPll3ModeAfter = NabuOwnRead32(Camcc, pll + 0x00);
    Context->RealPll3LAfter = NabuOwnRead32(Camcc, pll + 0x04);
    Context->RealPll3AlphaAfter = NabuOwnRead32(Camcc, pll + 0x40);
    Context->RealPll3UserAfter = NabuOwnRead32(Camcc, pll + 0x0C);
    Context->RealPll3OpmodeAfter = NabuOwnRead32(Camcc, pll + 0x38);
    Context->RealPll3LockOk =
        ((Context->RealPll3ModeAfter & ((1UL << 31) | 1UL)) ==
         ((1UL << 31) | 1UL) &&
         (Context->RealPll3OpmodeAfter & 1UL) != 0) ? 1UL : 0UL;
    return Context->RealPll3LockOk != 0;
}

static BOOLEAN
NabuOwnEnableIfe0Clocks(
    _Inout_ PNABU_OWN_STAGE23_CONTEXT Context,
    _In_ PUCHAR Camcc)
{
    ULONG value;
    ULONG elapsed;

    Context->TpgIfeGdscBefore = NabuOwnRead32(Camcc, 0xA004);
    NabuOwnWrite32(Camcc, 0xA004, Context->TpgIfeGdscBefore & ~1UL);
    KeMemoryBarrier();
    for (elapsed = 0; elapsed < 20000; elapsed += 10) {
        value = NabuOwnRead32(Camcc, 0xA008);
        if ((value & 0x00010000UL) != 0) {
            Context->TpgIfeGdscReady = 1;
            break;
        }
        KeStallExecutionProcessor(10);
    }
    Context->TpgIfeGdscAfter = NabuOwnRead32(Camcc, 0xA008);
    if (Context->TpgIfeGdscReady == 0) {
        return FALSE;
    }

    NabuOwnWrite32(Camcc, 0xA028,
                   NabuOwnRead32(Camcc, 0xA028) & ~1UL);
    KeMemoryBarrier();
    KeStallExecutionProcessor(100);
    if (!NabuOwnConfigureIfePll3For558Mhz(Context, Camcc)) {
        return FALSE;
    }

    NabuOwnWrite32(Camcc, 0xA014, (6UL << 8) | 1UL);
    NabuOwnWrite32(Camcc, 0xA010,
                   NabuOwnRead32(Camcc, 0xA010) | 3UL);
    KeMemoryBarrier();
    for (elapsed = 0; elapsed < 5000; elapsed += 10) {
        if ((NabuOwnRead32(Camcc, 0xA010) & 1UL) == 0) {
            break;
        }
        KeStallExecutionProcessor(10);
    }

    Context->TpgIfeClockCbcr = NabuOwnEnableBranch(Camcc, 0xA028);
    Context->RealIfeClkCmdAfter = NabuOwnRead32(Camcc, 0xA010);
    Context->RealIfeClkCfgAfter = NabuOwnRead32(Camcc, 0xA014);
    Context->RealIfeClkCbcrAfter = NabuOwnRead32(Camcc, 0xA028);

    Context->TpgIfeAxiCbcr = NabuOwnEnableBranch(Camcc, 0xA080);
    (VOID)NabuOwnEnableBranch(Camcc, 0xA07C);
    Context->TpgIfeCsidCbcr = NabuOwnEnableBranch(Camcc, 0xA054);
    KeStallExecutionProcessor(2000);

    return ((Context->TpgIfeClockCbcr | Context->TpgIfeAxiCbcr |
             Context->TpgIfeCsidCbcr) & 0x80000000UL) == 0;
}

static BOOLEAN
NabuOwnWaitRcgr(_In_ PUCHAR Camcc, _In_ ULONG CommandOffset)
{
    ULONG elapsed;

    for (elapsed = 0; elapsed < 5000; elapsed += 10) {
        if ((NabuOwnRead32(Camcc, CommandOffset) & 1UL) == 0) {
            return TRUE;
        }
        KeStallExecutionProcessor(10);
    }
    return FALSE;
}

static BOOLEAN
NabuOwnEnableRearClockTree(
    _Inout_ PNABU_OWN_STAGE23_CONTEXT Context,
    _In_ PUCHAR Camcc)
{
    ULONG pllMode;
    ULONG wait;
    BOOLEAN updatesOk = TRUE;

    pllMode = NabuOwnRead32(Camcc, 0x0000);
    if ((pllMode & 1UL) == 0 ||
        (NabuOwnRead32(Camcc, 0x0038) & 1UL) == 0) {
        NabuOwnWrite32(Camcc, 0x0004, 0x3E);
        NabuOwnWrite32(Camcc, 0x0008, 0x44);
        NabuOwnWrite32(Camcc, 0x0040, 0x8000);
        NabuOwnWrite32(Camcc, 0x0018, 0x20485699);
        NabuOwnWrite32(Camcc, 0x001C, 0x00002267);
        NabuOwnWrite32(Camcc, 0x0020, 0x00000024);
        NabuOwnWrite32(Camcc, 0x000C, 0x00003100);
        NabuOwnWrite32(Camcc, 0x0010, 0x00000805);
        NabuOwnWrite32(Camcc, 0x0014, 0x000000D0);
        NabuOwnWrite32(Camcc, 0x0024, 0);
        NabuOwnWrite32(Camcc, 0x0028, 0);
        NabuOwnWrite32(Camcc, 0x002C, 0x20);

        pllMode = (NabuOwnRead32(Camcc, 0x0000) | (1UL << 23)) & ~1UL;
        NabuOwnWrite32(Camcc, 0x0000, pllMode);
        NabuOwnWrite32(Camcc, 0x0038, 0);
        NabuOwnWrite32(Camcc, 0x0000, pllMode | (1UL << 2));
        KeMemoryBarrier();
        KeStallExecutionProcessor(10);
        NabuOwnWrite32(Camcc, 0x0038, 1);
        for (wait = 0; wait < 1000; wait += 10) {
            pllMode = NabuOwnRead32(Camcc, 0x0000);
            if ((pllMode & (1UL << 31)) != 0) {
                break;
            }
            KeStallExecutionProcessor(10);
        }
        if ((pllMode & (1UL << 31)) != 0) {
            NabuOwnWrite32(Camcc, 0x000C,
                           NabuOwnRead32(Camcc, 0x000C) | 7UL);
            NabuOwnWrite32(Camcc, 0x0000,
                           NabuOwnRead32(Camcc, 0x0000) | 1UL);
            KeMemoryBarrier();
            KeStallExecutionProcessor(10);
        }
    }
    Context->RealPll0LockOk =
        (NabuOwnRead32(Camcc, 0x0000) & (1UL << 31)) != 0 ? 1UL : 0UL;
    if (Context->RealPll0LockOk == 0) {
        return FALSE;
    }

    NabuOwnWrite32(Camcc, 0xA07C, NabuOwnRead32(Camcc, 0xA07C) & ~1UL);
    NabuOwnWrite32(Camcc, 0xA054, NabuOwnRead32(Camcc, 0xA054) & ~1UL);
    KeMemoryBarrier();

    NabuOwnWrite32(Camcc, 0xA068, 0x301);
    NabuOwnWrite32(Camcc, 0xA064, NabuOwnRead32(Camcc, 0xA064) | 3UL);
    updatesOk = NabuOwnWaitRcgr(Camcc, 0xA064) && updatesOk;
    NabuOwnWrite32(Camcc, 0xA040, 0x103);
    NabuOwnWrite32(Camcc, 0xA03C, NabuOwnRead32(Camcc, 0xA03C) | 3UL);
    updatesOk = NabuOwnWaitRcgr(Camcc, 0xA03C) && updatesOk;

    NabuOwnWrite32(Camcc, 0x6008, 0x107);
    NabuOwnWrite32(Camcc, 0x6004, NabuOwnRead32(Camcc, 0x6004) | 3UL);
    updatesOk = NabuOwnWaitRcgr(Camcc, 0x6004) && updatesOk;
    (VOID)NabuOwnEnableBranch(Camcc, 0x601C);
    (VOID)NabuOwnEnableBranch(Camcc, 0x6020);
    (VOID)NabuOwnEnableBranch(Camcc, 0xA07C);
    (VOID)NabuOwnEnableBranch(Camcc, 0xA054);

    NabuOwnWrite32(Camcc, 0x5008, 1UL);
    NabuOwnWrite32(Camcc, 0x5004, NabuOwnRead32(Camcc, 0x5004) | 3UL);
    updatesOk = NabuOwnWaitRcgr(Camcc, 0x5004) && updatesOk;
    Context->RealMclk0Cbcr = NabuOwnEnableBranch(Camcc, 0x501C);
    Context->RealMclk0Cmd = NabuOwnRead32(Camcc, 0x5004);
    Context->RealMclk0Cfg = NabuOwnRead32(Camcc, 0x5008);
    Context->RealMclk0Cbcr = NabuOwnRead32(Camcc, 0x501C);
    KeStallExecutionProcessor(2000);

    Context->RealClockTreeReady =
        (updatesOk &&
         (Context->RealMclk0Cbcr & 0x80000000UL) == 0 &&
         (NabuOwnRead32(Camcc, 0x601C) & 0x80000000UL) == 0 &&
         (NabuOwnRead32(Camcc, 0x6020) & 0x80000000UL) == 0 &&
         (NabuOwnRead32(Camcc, 0xA07C) & 0x80000000UL) == 0 &&
         (NabuOwnRead32(Camcc, 0xA054) & 0x80000000UL) == 0) ? 1UL : 0UL;
    return Context->RealClockTreeReady != 0;
}

static BOOLEAN
NabuOwnConfigureRearPins(VOID)
{
    PHYSICAL_ADDRESS address;
    PUCHAR north = NULL;
    PUCHAR east = NULL;
    ULONG value;
    BOOLEAN ready = FALSE;

    address.QuadPart = NABU_TLMM_NORTH_PHYS;
    north = (PUCHAR)MmMapIoSpaceEx(address, NABU_TLMM_NORTH_BYTES,
                                   PAGE_READWRITE | PAGE_NOCACHE);
    address.QuadPart = NABU_TLMM_EAST_PHYS;
    east = (PUCHAR)MmMapIoSpaceEx(address, NABU_TLMM_EAST_BYTES,
                                  PAGE_READWRITE | PAGE_NOCACHE);
    if (north == NULL || east == NULL) {
        goto Cleanup;
    }

    value = NabuOwnRead32(north, NABU_TLMM_CTL(NABU_REAR_VANA_GPIO));
    value = (value & ~(7UL << 2)) | (1UL << 9);
    NabuOwnWrite32(north, NABU_TLMM_CTL(NABU_REAR_VANA_GPIO), value);
    NabuOwnWrite32(north, NABU_TLMM_IO(NABU_REAR_VANA_GPIO),
                   NabuOwnRead32(north, NABU_TLMM_IO(NABU_REAR_VANA_GPIO)) | 2UL);

    value = NabuOwnRead32(north, NABU_TLMM_CTL(NABU_REAR_MCLK_GPIO));
    value &= ~(7UL << 2);
    value |= (1UL << 2);
    value &= ~3UL;
    NabuOwnWrite32(north, NABU_TLMM_CTL(NABU_REAR_MCLK_GPIO), value);

    value = NabuOwnRead32(east, NABU_TLMM_CTL(NABU_REAR_RESET_GPIO));
    value = (value & ~(7UL << 2)) | (1UL << 9);
    NabuOwnWrite32(east, NABU_TLMM_CTL(NABU_REAR_RESET_GPIO), value);
    NabuOwnWrite32(east, NABU_TLMM_IO(NABU_REAR_RESET_GPIO),
                   NabuOwnRead32(east, NABU_TLMM_IO(NABU_REAR_RESET_GPIO)) & ~2UL);
    KeStallExecutionProcessor(2000);
    NabuOwnWrite32(east, NABU_TLMM_IO(NABU_REAR_RESET_GPIO),
                   NabuOwnRead32(east, NABU_TLMM_IO(NABU_REAR_RESET_GPIO)) | 2UL);
    KeStallExecutionProcessor(5000);
    ready = TRUE;

Cleanup:
    if (east != NULL) {
        MmUnmapIoSpace(east, NABU_TLMM_EAST_BYTES);
    }
    if (north != NULL) {
        MmUnmapIoSpace(north, NABU_TLMM_NORTH_BYTES);
    }
    return ready;
}

static BOOLEAN
NabuOwnPrepareRearSensorRails(_Inout_ PNABU_OWN_STAGE23_CONTEXT Context)
{
    SPMI_ARB_CONTEXT spmi;
    PHYSICAL_ADDRESS address;
    NTSTATUS status;
    NTSTATUS readStatus;
    ULONG vsetLb = 0;
    ULONG vsetUb = 0;
    ULONG attempt;
    BOOLEAN vioValid = FALSE;
    BOOLEAN vdigValid = FALSE;
    BOOLEAN vioReady = FALSE;
    BOOLEAN vdigReady = FALSE;

    RtlZeroMemory(&spmi, sizeof(spmi));
    Context->RealPowerRailsAttempted = 1;
    Context->RealPowerRailsReady = 0;
    Context->RealSpmiInitStatus = (ULONG)STATUS_NOT_SUPPORTED;
    Context->RealVioFindStatus = (ULONG)STATUS_NOT_SUPPORTED;
    Context->RealVioWriteStatus = (ULONG)STATUS_NOT_SUPPORTED;
    Context->RealVdigFindStatus = (ULONG)STATUS_NOT_SUPPORTED;
    Context->RealVdigModeWriteStatus = (ULONG)STATUS_NOT_SUPPORTED;
    Context->RealVdigOutWriteStatus = (ULONG)STATUS_NOT_SUPPORTED;
    Context->RealVdigEnWriteStatus = (ULONG)STATUS_NOT_SUPPORTED;
    Context->RealBobFindStatus = (ULONG)STATUS_NOT_SUPPORTED;

    address.QuadPart = SPMI_ARB_CORE_PHYS_ADDR;
    spmi.CoreBase = (PUCHAR)MmMapIoSpaceEx(
        address, SPMI_ARB_CORE_REGION_SIZE, PAGE_READWRITE | PAGE_NOCACHE);
    address.QuadPart = SPMI_ARB_CNFG_PHYS_ADDR;
    spmi.CnfgBase = (PUCHAR)MmMapIoSpaceEx(
        address, SPMI_ARB_CNFG_REGION_SIZE, PAGE_READWRITE | PAGE_NOCACHE);
    address.QuadPart = SPMI_ARB_OBSRVR_PHYS_ADDR;
    spmi.ObsrvrBase = (PUCHAR)MmMapIoSpaceEx(
        address, SPMI_ARB_OBSRVR_REGION_SIZE, PAGE_READWRITE | PAGE_NOCACHE);
    address.QuadPart = SPMI_ARB_CHNLS_PHYS_ADDR;
    spmi.ChnlsBase = (PUCHAR)MmMapIoSpaceEx(
        address, SPMI_ARB_CHNLS_REGION_SIZE, PAGE_READWRITE | PAGE_NOCACHE);

    if (spmi.CoreBase == NULL || spmi.CnfgBase == NULL ||
        spmi.ObsrvrBase == NULL || spmi.ChnlsBase == NULL) {
        Context->RealSpmiInitStatus = (ULONG)STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }

    status = SpmiArbInitialize(&spmi);
    Context->RealSpmiInitStatus = (ULONG)status;
    Context->RealSpmiHwVersion = spmi.HwVersion;
    Context->RealSpmiApidCount = spmi.ApidCount;
    if (!NT_SUCCESS(status)) {
        goto Cleanup;
    }

    status = SpmiArbFindApid(
        &spmi, (UCHAR)SPMI_PM8150L_REG_SID,
        (UCHAR)SPMI_PM8150L_LDO1_PID,
        &Context->RealVioApid, &Context->RealVioWriteEe);
    Context->RealVioFindStatus = (ULONG)status;
    if (NT_SUCCESS(status)) {
        readStatus = SpmiArbReadPeripheral(
            &spmi, (UCHAR)SPMI_PM8150L_REG_SID,
            (UCHAR)SPMI_PM8150L_LDO1_PID,
            (UCHAR)SPMI_VREG_REG_TYPE, SPMI_OUR_EE,
            Context->RealVioApid, &Context->RealVioType, 1);
        if (NT_SUCCESS(readStatus)) {
            readStatus = SpmiArbReadPeripheral(
                &spmi, (UCHAR)SPMI_PM8150L_REG_SID,
                (UCHAR)SPMI_PM8150L_LDO1_PID,
                (UCHAR)SPMI_VREG_REG_VSET_LB, SPMI_OUR_EE,
                Context->RealVioApid, &vsetLb, 1);
        }
        if (NT_SUCCESS(readStatus)) {
            readStatus = SpmiArbReadPeripheral(
                &spmi, (UCHAR)SPMI_PM8150L_REG_SID,
                (UCHAR)SPMI_PM8150L_LDO1_PID,
                (UCHAR)SPMI_VREG_REG_VSET_UB, SPMI_OUR_EE,
                Context->RealVioApid, &vsetUb, 1);
        }
        if (NT_SUCCESS(readStatus)) {
            readStatus = SpmiArbReadPeripheral(
                &spmi, (UCHAR)SPMI_PM8150L_REG_SID,
                (UCHAR)SPMI_PM8150L_LDO1_PID,
                (UCHAR)SPMI_VREG_REG_EN_CTL, SPMI_OUR_EE,
                Context->RealVioApid, &Context->RealVioEnBefore, 1);
        }
        if (NT_SUCCESS(readStatus)) {
            readStatus = SpmiArbReadPeripheral(
                &spmi, (UCHAR)SPMI_PM8150L_REG_SID,
                (UCHAR)SPMI_PM8150L_LDO1_PID,
                (UCHAR)SPMI_VREG_REG_STATUS1, SPMI_OUR_EE,
                Context->RealVioApid, &Context->RealVioStatusBefore, 1);
        }

        Context->RealVioVsetMv = (vsetUb << 8) | vsetLb;
        vioValid = NT_SUCCESS(readStatus) &&
            Context->RealVioType == SPMI_VREG_TYPE_LDO &&
            Context->RealVioVsetMv == SPMI_VREG_EXPECTED_VIO_MV;

        if (vioValid) {
            Context->RealVioWriteStatus = (ULONG)STATUS_SUCCESS;
            Context->RealVioEnAfter = Context->RealVioEnBefore;
            Context->RealVioStatusAfter = Context->RealVioStatusBefore;
            if ((Context->RealVioEnBefore & SPMI_VREG_EN_BIT) == 0) {
                if (Context->RealVioWriteEe == SPMI_OUR_EE) {
                    Context->RealVioWriteStatus = (ULONG)SpmiArbWritePeripheral(
                        &spmi, (UCHAR)SPMI_PM8150L_REG_SID,
                        (UCHAR)SPMI_PM8150L_LDO1_PID,
                        (UCHAR)SPMI_VREG_REG_EN_CTL, Context->RealVioApid,
                        SPMI_VREG_EN_BIT, 1);
                } else {
                    Context->RealVioWriteStatus = (ULONG)STATUS_ACCESS_DENIED;
                }
            }

            for (attempt = 0; attempt < 20; ++attempt) {
                KeStallExecutionProcessor(1000);
                (VOID)SpmiArbReadPeripheral(
                    &spmi, (UCHAR)SPMI_PM8150L_REG_SID,
                    (UCHAR)SPMI_PM8150L_LDO1_PID,
                    (UCHAR)SPMI_VREG_REG_EN_CTL, SPMI_OUR_EE,
                    Context->RealVioApid, &Context->RealVioEnAfter, 1);
                (VOID)SpmiArbReadPeripheral(
                    &spmi, (UCHAR)SPMI_PM8150L_REG_SID,
                    (UCHAR)SPMI_PM8150L_LDO1_PID,
                    (UCHAR)SPMI_VREG_REG_STATUS1, SPMI_OUR_EE,
                    Context->RealVioApid, &Context->RealVioStatusAfter, 1);
                if ((Context->RealVioEnAfter & SPMI_VREG_EN_BIT) != 0 &&
                    (Context->RealVioStatusAfter & 0x80UL) != 0) {
                    vioReady = TRUE;
                    break;
                }
            }
        }
    }

    status = SpmiArbFindApid(
        &spmi, (UCHAR)SPMI_PM8150L_SID,
        (UCHAR)SPMI_PM8150L_GPIO3_PID,
        &Context->RealVdigApid, &Context->RealVdigWriteEe);
    Context->RealVdigFindStatus = (ULONG)status;
    if (NT_SUCCESS(status)) {
        readStatus = SpmiArbReadPeripheral(
            &spmi, (UCHAR)SPMI_PM8150L_SID,
            (UCHAR)SPMI_PM8150L_GPIO3_PID,
            (UCHAR)SPMI_PMIC_GPIO_REG_TYPE, SPMI_OUR_EE,
            Context->RealVdigApid, &Context->RealVdigType, 1);
        if (NT_SUCCESS(readStatus)) {
            readStatus = SpmiArbReadPeripheral(
                &spmi, (UCHAR)SPMI_PM8150L_SID,
                (UCHAR)SPMI_PM8150L_GPIO3_PID,
                (UCHAR)SPMI_PMIC_GPIO_REG_SUBTYPE, SPMI_OUR_EE,
                Context->RealVdigApid, &Context->RealVdigSubtype, 1);
        }
        if (NT_SUCCESS(readStatus)) {
            readStatus = SpmiArbReadPeripheral(
                &spmi, (UCHAR)SPMI_PM8150L_SID,
                (UCHAR)SPMI_PM8150L_GPIO3_PID,
                (UCHAR)SPMI_PMIC_GPIO_REG_MODE_CTL, SPMI_OUR_EE,
                Context->RealVdigApid, &Context->RealVdigModeBefore, 1);
        }
        if (NT_SUCCESS(readStatus)) {
            readStatus = SpmiArbReadPeripheral(
                &spmi, (UCHAR)SPMI_PM8150L_SID,
                (UCHAR)SPMI_PM8150L_GPIO3_PID,
                (UCHAR)SPMI_PMIC_GPIO_REG_DIG_OUT_SRC, SPMI_OUR_EE,
                Context->RealVdigApid, &Context->RealVdigOutBefore, 1);
        }
        if (NT_SUCCESS(readStatus)) {
            readStatus = SpmiArbReadPeripheral(
                &spmi, (UCHAR)SPMI_PM8150L_SID,
                (UCHAR)SPMI_PM8150L_GPIO3_PID,
                (UCHAR)SPMI_PMIC_GPIO_REG_EN_CTL, SPMI_OUR_EE,
                Context->RealVdigApid, &Context->RealVdigEnBefore, 1);
        }

        vdigValid = NT_SUCCESS(readStatus) &&
            Context->RealVdigType == SPMI_PMIC_GPIO_TYPE_GPIO &&
            (Context->RealVdigSubtype == SPMI_PMIC_GPIO_SUBTYPE_LV ||
             Context->RealVdigSubtype == SPMI_PMIC_GPIO_SUBTYPE_MV) &&
            Context->RealVdigWriteEe == SPMI_OUR_EE;

        if (vdigValid) {
            Context->RealVdigModeWriteStatus = (ULONG)STATUS_SUCCESS;
            Context->RealVdigOutWriteStatus = (ULONG)STATUS_SUCCESS;
            Context->RealVdigEnWriteStatus = (ULONG)STATUS_SUCCESS;

            if ((Context->RealVdigModeBefore & 3UL) != 1UL) {
                Context->RealVdigModeWriteStatus = (ULONG)SpmiArbWritePeripheral(
                    &spmi, (UCHAR)SPMI_PM8150L_SID,
                    (UCHAR)SPMI_PM8150L_GPIO3_PID,
                    (UCHAR)SPMI_PMIC_GPIO_REG_MODE_CTL,
                    Context->RealVdigApid, 1UL, 1);
            }
            if (Context->RealVdigOutBefore != SPMI_PMIC_GPIO_LV_MV_OUT_HIGH) {
                Context->RealVdigOutWriteStatus = (ULONG)SpmiArbWritePeripheral(
                    &spmi, (UCHAR)SPMI_PM8150L_SID,
                    (UCHAR)SPMI_PM8150L_GPIO3_PID,
                    (UCHAR)SPMI_PMIC_GPIO_REG_DIG_OUT_SRC,
                    Context->RealVdigApid, SPMI_PMIC_GPIO_LV_MV_OUT_HIGH, 1);
            }
            if ((Context->RealVdigEnBefore & SPMI_PMIC_GPIO_EN_MASTER_BIT) == 0) {
                Context->RealVdigEnWriteStatus = (ULONG)SpmiArbWritePeripheral(
                    &spmi, (UCHAR)SPMI_PM8150L_SID,
                    (UCHAR)SPMI_PM8150L_GPIO3_PID,
                    (UCHAR)SPMI_PMIC_GPIO_REG_EN_CTL,
                    Context->RealVdigApid, SPMI_PMIC_GPIO_EN_MASTER_BIT, 1);
            }
            KeStallExecutionProcessor(5000);

            (VOID)SpmiArbReadPeripheral(
                &spmi, (UCHAR)SPMI_PM8150L_SID,
                (UCHAR)SPMI_PM8150L_GPIO3_PID,
                (UCHAR)SPMI_PMIC_GPIO_REG_MODE_CTL, SPMI_OUR_EE,
                Context->RealVdigApid, &Context->RealVdigModeAfter, 1);
            (VOID)SpmiArbReadPeripheral(
                &spmi, (UCHAR)SPMI_PM8150L_SID,
                (UCHAR)SPMI_PM8150L_GPIO3_PID,
                (UCHAR)SPMI_PMIC_GPIO_REG_DIG_OUT_SRC, SPMI_OUR_EE,
                Context->RealVdigApid, &Context->RealVdigOutAfter, 1);
            (VOID)SpmiArbReadPeripheral(
                &spmi, (UCHAR)SPMI_PM8150L_SID,
                (UCHAR)SPMI_PM8150L_GPIO3_PID,
                (UCHAR)SPMI_PMIC_GPIO_REG_EN_CTL, SPMI_OUR_EE,
                Context->RealVdigApid, &Context->RealVdigEnAfter, 1);

            vdigReady = (Context->RealVdigModeAfter & 3UL) == 1UL &&
                Context->RealVdigOutAfter == SPMI_PMIC_GPIO_LV_MV_OUT_HIGH &&
                (Context->RealVdigEnAfter & SPMI_PMIC_GPIO_EN_MASTER_BIT) != 0;
        }
    }

    status = SpmiArbFindApid(
        &spmi, (UCHAR)SPMI_PM8150L_REG_SID,
        (UCHAR)SPMI_PM8150L_BOB_PID,
        &Context->RealBobApid, &Context->RealBobWriteEe);
    Context->RealBobFindStatus = (ULONG)status;
    if (NT_SUCCESS(status)) {
        (VOID)SpmiArbReadPeripheral(
            &spmi, (UCHAR)SPMI_PM8150L_REG_SID,
            (UCHAR)SPMI_PM8150L_BOB_PID,
            (UCHAR)SPMI_VREG_REG_TYPE, SPMI_OUR_EE,
            Context->RealBobApid, &Context->RealBobType, 1);
        (VOID)SpmiArbReadPeripheral(
            &spmi, (UCHAR)SPMI_PM8150L_REG_SID,
            (UCHAR)SPMI_PM8150L_BOB_PID,
            (UCHAR)SPMI_VREG_REG_EN_CTL, SPMI_OUR_EE,
            Context->RealBobApid, &Context->RealBobEn, 1);
        (VOID)SpmiArbReadPeripheral(
            &spmi, (UCHAR)SPMI_PM8150L_REG_SID,
            (UCHAR)SPMI_PM8150L_BOB_PID,
            (UCHAR)SPMI_VREG_REG_STATUS1, SPMI_OUR_EE,
            Context->RealBobApid, &Context->RealBobStatus, 1);
    }

    Context->RealPowerRailsReady = (vioReady && vdigReady) ? 1UL : 0UL;

Cleanup:
    if (spmi.ChnlsBase != NULL) {
        MmUnmapIoSpace(spmi.ChnlsBase, SPMI_ARB_CHNLS_REGION_SIZE);
    }
    if (spmi.ObsrvrBase != NULL) {
        MmUnmapIoSpace(spmi.ObsrvrBase, SPMI_ARB_OBSRVR_REGION_SIZE);
    }
    if (spmi.CnfgBase != NULL) {
        MmUnmapIoSpace(spmi.CnfgBase, SPMI_ARB_CNFG_REGION_SIZE);
    }
    if (spmi.CoreBase != NULL) {
        MmUnmapIoSpace(spmi.CoreBase, SPMI_ARB_CORE_REGION_SIZE);
    }
    return Context->RealPowerRailsReady != 0;
}

static NTSTATUS
NabuOwnSensorWriteRetry(
    _In_ PNABU_OWN_STAGE23_CONTEXT Context,
    _In_ PPLATFORM_CLIENT Client,
    _In_ USHORT Register,
    _In_ UCHAR Value)
{
    UCHAR request[NABU_OWN_CCI_READ_BYTES];
    ULONG attempt;
    NTSTATUS status = STATUS_UNSUCCESSFUL;

    if (Context->CciConfigValid == 0) {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    for (attempt = 0; attempt < 5; ++attempt) {
        NabuOwnBuildStockCciWrite(request,
                                  Context->CciFrequencyKHz,
                                  (UCHAR)Context->CciSlaveAddress,
                                  Context->CciAddressType,
                                  &Value,
                                  Register,
                                  sizeof(Value),
                                  Context->CciMaster);
        status = PlatformDispatch(Client, PLAT_CMD_I2C_WRITE,
                                  request, NULL, NULL);
        if (NT_SUCCESS(status)) {
            break;
        }
        KeStallExecutionProcessor(1000);
    }
    return status;
}

static NTSTATUS
NabuOwnSensorReadRetry(
    _In_ PNABU_OWN_STAGE23_CONTEXT Context,
    _In_ PPLATFORM_CLIENT Client,
    _In_ USHORT Register,
    _Out_writes_bytes_(Length) PUCHAR Buffer,
    _In_ USHORT Length)
{
    UCHAR request[NABU_OWN_CCI_READ_BYTES];
    ULONG attempt;
    NTSTATUS status = STATUS_UNSUCCESSFUL;

    if (Context->CciConfigValid == 0) {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    for (attempt = 0; attempt < 5; ++attempt) {
        RtlZeroMemory(Buffer, Length);
        NabuOwnBuildStockCciRead(request,
                                 Context->CciFrequencyKHz,
                                 (UCHAR)Context->CciSlaveAddress,
                                 Context->CciAddressType,
                                 Buffer,
                                 Register,
                                 Length,
                                 Context->CciMaster);
        status = PlatformDispatch(Client, PLAT_CMD_I2C_READ,
                                  NULL, NULL, request);
        if (NT_SUCCESS(status)) {
            break;
        }
        KeStallExecutionProcessor(1000);
    }
    return status;
}

static NTSTATUS
NabuOwnWriteSensorTable(
    _In_ PNABU_OWN_STAGE23_CONTEXT Context,
    _In_ PPLATFORM_CLIENT Client,
    _In_reads_(Count) const NABU_SENSOR_REG8 *Table,
    _In_ ULONG Count,
    _Out_ PULONG FailIndex,
    _Out_ PULONG FailRegister)
{
    ULONG i;
    NTSTATUS status = STATUS_SUCCESS;

    *FailIndex = MAXULONG;
    *FailRegister = 0;
    for (i = 0; i < Count; ++i) {
        status = NabuOwnSensorWriteRetry(Context,
                                         Client,
                                         Table[i].Register,
                                         Table[i].Value);
        if (!NT_SUCCESS(status)) {
            *FailIndex = i;
            *FailRegister = Table[i].Register;
            break;
        }
    }
    return status;
}

static VOID
NabuOwnRunTpgCapture(
    _Inout_ PNABU_OWN_STAGE23_CONTEXT Context,
    _Inout_updates_bytes_(NABU_OWN_DMA_BUFFER_BYTES) PUCHAR Buffer,
    _In_ ULONGLONG DeviceAddress)
{
    PLATFORM_CLIENT client;
    PHYSICAL_ADDRESS address;
    PUCHAR camcc = NULL;
    PUCHAR csid = NULL;
    PUCHAR vfe = NULL;
    BOOLEAN opened = FALSE;
    BOOLEAN acquired = FALSE;
    BOOLEAN bandwidthVoted = FALSE;
    BOOLEAN tpgStarted = FALSE;
    BOOLEAN wmStarted = FALSE;
    ULONG resetStatus = 0;
    ULONG attempt;
    ULONG rdiCfg0;
    ULONG tpgCtrl;
    ULONG wm = NABU_VFE_WM0;
    NTSTATUS status;

    RtlZeroMemory(&client, sizeof(client));
    Context->TpgAttempted = 1;
    Context->TpgCompleted = 0;
    Context->TpgPlatformOpenStatus = (ULONG)STATUS_NOT_SUPPORTED;
    Context->TpgPlatformAcquireStatus = (ULONG)STATUS_NOT_SUPPORTED;
    Context->TpgPlatformPowerStatus = (ULONG)STATUS_NOT_SUPPORTED;
    Context->TpgPlatformReleaseStatus = (ULONG)STATUS_NOT_SUPPORTED;
    Context->TpgRtBandwidthStatus = (ULONG)STATUS_NOT_SUPPORTED;
    Context->TpgRtBandwidthReleaseStatus = (ULONG)STATUS_NOT_SUPPORTED;
    Context->TpgFileStatus = (ULONG)STATUS_NOT_SUPPORTED;
    Context->TpgIovaLow = (ULONG)DeviceAddress;
    Context->TpgIovaHigh = (ULONG)(DeviceAddress >> 32);
    Context->TpgPhase = 1;
    NabuOwnWriteDiagnostics(Context);

    if (DeviceAddress == 0 || DeviceAddress > MAXULONG ||
        Buffer == NULL || Context->DmaBufferLength < NABU_TPG_FRAME_BYTES) {
        Context->TpgPhase = 2;
        goto Cleanup;
    }

    RtlFillMemory(Buffer, Context->DmaBufferLength, 0xAB);
    KeMemoryBarrier();

    status = PlatformOpen(Context->Device, &client);
    Context->TpgPlatformOpenStatus = (ULONG)status;
    if (!NT_SUCCESS(status)) {
        Context->TpgPhase = 3;
        goto Cleanup;
    }
    opened = TRUE;

    status = PlatformDispatch(&client, NABU_PLAT_CMD_ACQUIRE,
                              NULL, NULL, NULL);
    Context->TpgPlatformAcquireStatus = (ULONG)status;
    if (!NT_SUCCESS(status)) {
        Context->TpgPhase = 4;
        goto Cleanup;
    }
    acquired = TRUE;

    status = PlatformPower(&client);
    Context->TpgPlatformPowerStatus = (ULONG)status;
    if (!NT_SUCCESS(status)) {
        Context->TpgPhase = 5;
        goto Cleanup;
    }

    address.QuadPart = NABU_CAMCC_PHYS;
    camcc = (PUCHAR)MmMapIoSpaceEx(address, NABU_CAMCC_BYTES,
                                   PAGE_READWRITE | PAGE_NOCACHE);
    if (camcc == NULL || !NabuOwnEnableIfe0Clocks(Context, camcc)) {
        Context->TpgPhase = 10;
        goto Cleanup;
    }
    Context->TpgPhase = 11;
    NabuOwnWriteDiagnostics(Context);

    status = PlatformRequestRtBandwidth(&client, 0,
                                        NABU_RT_BANDWIDTH,
                                        NABU_RT_BANDWIDTH);
    Context->TpgRtBandwidthStatus = (ULONG)status;
    if (!NT_SUCCESS(status)) {
        Context->TpgPhase = 12;
        goto Cleanup;
    }
    bandwidthVoted = TRUE;
    KeStallExecutionProcessor(5000);
    Context->TpgCamnocAxiCbcr = NabuOwnRead32(camcc, 0xC18C);
    if ((Context->TpgCamnocAxiCbcr & 0x80000000UL) != 0) {
        Context->TpgPhase = 13;
        goto Cleanup;
    }

    address.QuadPart = NABU_CSID0_PHYS;
    csid = (PUCHAR)MmMapIoSpaceEx(address, NABU_CSID0_BYTES,
                                  PAGE_READWRITE | PAGE_NOCACHE);
    address.QuadPart = NABU_VFE0_PHYS;
    vfe = (PUCHAR)MmMapIoSpaceEx(address, NABU_VFE0_BYTES,
                                 PAGE_READWRITE | PAGE_NOCACHE);
    if (csid == NULL || vfe == NULL) {
        Context->TpgPhase = 20;
        goto Cleanup;
    }

    NabuOwnWrite32(vfe, 0x5C, 0x80000000UL);
    NabuOwnWrite32(vfe, 0x60, 0);
    NabuOwnWrite32(vfe, 0x64, 0xFFFFFFFFUL);
    NabuOwnWrite32(vfe, 0x68, 0xFFFFFFFFUL);
    NabuOwnWrite32(vfe, 0x58, 1);
    KeMemoryBarrier();
    NabuOwnWrite32(vfe, 0x18, 0x00003F9FUL);
    KeMemoryBarrier();
    for (attempt = 0; attempt < 1000; ++attempt) {
        resetStatus = NabuOwnRead32(vfe, 0x6C);
        if ((resetStatus & 0x80000000UL) != 0) {
            break;
        }
        KeStallExecutionProcessor(10);
    }
    Context->TpgVfeResetStatus = resetStatus;
    Context->TpgVfeResetPolls = attempt;
    if ((resetStatus & 0x80000000UL) == 0) {
        Context->TpgPhase = 21;
        goto Cleanup;
    }
    NabuOwnWrite32(vfe, 0x64, resetStatus);
    NabuOwnWrite32(vfe, 0x68, NabuOwnRead32(vfe, 0x70));
    NabuOwnWrite32(vfe, 0x58, 1);

    NabuOwnWrite32(csid, 0x600, 0);
    NabuOwnWrite32(csid, 0x28, 0x0FFFFFFFUL);
    NabuOwnWrite32(csid, 0x48, 0x00007FFFUL);
    NabuOwnWrite32(csid, 0x78, 1);
    NabuOwnWrite32(csid, 0x80, 1);
    rdiCfg0 = (0x0AUL << 22) | (0x2BUL << 16) |
              (0x2UL << 12) | (0x1UL << 10) |
              (1UL << 2) | 3UL | (1UL << 31);
    NabuOwnWrite32(csid, 0x604, (1UL << 10) | 0x0AUL);
    NabuOwnWrite32(csid, 0x608, (0x3FFUL << 12) | 0x740UL);
    NabuOwnWrite32(csid, 0x60C, 0x12345678UL);
    NabuOwnWrite32(csid, 0x610,
                   (NABU_TPG_WIDTH << 16) | NABU_TPG_HEIGHT);
    NabuOwnWrite32(csid, 0x614, 0x2BUL);
    NabuOwnWrite32(csid, 0x618, 0x00020008UL);
    NabuOwnWrite32(csid, 0x640, 1UL << 5);
    NabuOwnWrite32(csid, 0x648, 0);
    NabuOwnWrite32(csid, 0x300, rdiCfg0);
    NabuOwnWrite32(csid, 0x304, 2);
    NabuOwnWrite32(csid, 0x30C, 0);
    NabuOwnWrite32(csid, 0x310, 1);
    NabuOwnWrite32(csid, 0x314, 0);
    NabuOwnWrite32(csid, 0x318, 1);
    NabuOwnWrite32(csid, 0x324, 0);
    NabuOwnWrite32(csid, 0x328, 1);
    NabuOwnWrite32(csid, 0x32C, 0);
    NabuOwnWrite32(csid, 0x330, 1);
    NabuOwnWrite32(csid, 0x308, 1);

    NabuOwnWrite32(vfe, 0x226C, 0x82);
    NabuOwnWrite32(vfe, 0x2080, 0);
    NabuOwnWrite32(vfe, 0x200C, 0xFFFFF);
    NabuOwnWrite32(vfe, 0x211C, 0);
    NabuOwnWrite32(vfe, 0x2084, 0xFFFFFF);
    NabuOwnWrite32(vfe, 0x2050, 0xFFFFFFFFUL);
    NabuOwnWrite32(vfe, 0x2054, 0xFFFFFFFFUL);
    NabuOwnWrite32(vfe, 0x2058, 0xFFFFFFFFUL);
    NabuOwnWrite32(vfe, 0x2068, 1);

    NabuOwnWrite32(vfe, wm + 0x08, 0);
    NabuOwnWrite32(vfe, wm + 0x5C, 0xF);
    NabuOwnWrite32(vfe, wm + 0x1C, 0xFF01);
    NabuOwnWrite32(vfe, wm + 0x20, 0);
    NabuOwnWrite32(vfe, wm + 0x24, 0);
    NabuOwnWrite32(vfe, wm + 0x28, 0xFF01);
    NabuOwnWrite32(vfe, wm + 0x0C, 0);
    NabuOwnWrite32(vfe, wm + 0x18, 0);
    NabuOwnWrite32(vfe, wm + 0x48, 0);
    NabuOwnWrite32(vfe, wm + 0x4C, 1);
    NabuOwnWrite32(vfe, wm + 0x50, 0);
    NabuOwnWrite32(vfe, wm + 0x54, 1);
    NabuOwnWrite32(vfe, wm + 0x14, (ULONG)DeviceAddress);
    NabuOwnWrite32(vfe, wm + 0x58, NABU_TPG_FRAME_BYTES);
    Context->TpgWmImageBefore = NabuOwnRead32(vfe, wm + 0x14);
    NabuOwnWrite32(vfe, wm + 0x08, 3);
    wmStarted = TRUE;
    KeMemoryBarrier();
    NabuOwnWrite32(vfe, 0x4AC, 2);
    KeMemoryBarrier();

    Context->TpgPhase = 50;
    NabuOwnWriteDiagnostics(Context);
    tpgCtrl = (4UL << 20) | (0x80UL << 8) |
              ((4UL - 1) << 4) | 7UL;
    NabuOwnWrite32(csid, 0x600, tpgCtrl);
    tpgStarted = TRUE;
    KeMemoryBarrier();

    for (attempt = 0; attempt < 30; ++attempt) {
        KeStallExecutionProcessor(10000);
        KeMemoryBarrier();
        if (*(volatile ULONG *)(Buffer + 4) != NABU_TPG_MARKER ||
            *(volatile ULONG *)(Buffer + 8) != NABU_TPG_MARKER ||
            (NabuOwnRead32(vfe, 0x205C) & 0x7800UL) != 0) {
            break;
        }
    }

    Context->TpgWmCfg = NabuOwnRead32(vfe, wm + 0x08);
    Context->TpgWmImageAfter = NabuOwnRead32(vfe, wm + 0x14);
    NabuOwnWrite32(csid, 0x600, 0);
    tpgStarted = FALSE;
    NabuOwnWrite32(vfe, wm + 0x08, 0);
    wmStarted = FALSE;
    KeMemoryBarrier();

    Context->TpgVfeBusIrq0 = NabuOwnRead32(vfe, 0x205C);
    Context->TpgVfeBusIrq1 = NabuOwnRead32(vfe, 0x2060);
    Context->TpgVfeBusIrq2 = NabuOwnRead32(vfe, 0x2064);
    Context->TpgVfeBusDebug0 = NabuOwnRead32(vfe, 0x2270);
    Context->TpgCsidRdiIrq = NabuOwnRead32(csid, 0x40);
    Context->TpgCsidTotalPackets = NabuOwnRead32(csid, 0x160);
    Context->TpgBufferSample0 = *(volatile ULONG *)(Buffer + 0);
    Context->TpgBufferSample1 = *(volatile ULONG *)(Buffer + 4);
    Context->TpgBufferSample2 = *(volatile ULONG *)(Buffer + 8);
    Context->TpgBufferSample3 = *(volatile ULONG *)(Buffer + 12);
    Context->TpgBufferChanged =
        (Context->TpgBufferSample0 != NABU_TPG_MARKER ||
         Context->TpgBufferSample1 != NABU_TPG_MARKER ||
         Context->TpgBufferSample2 != NABU_TPG_MARKER ||
         Context->TpgBufferSample3 != NABU_TPG_MARKER) ? 1UL : 0UL;
    Context->TpgPhase = 60;
    NabuOwnWriteDiagnostics(Context);

    if (Context->TpgBufferChanged != 0) {
        UNICODE_STRING fileName;
        OBJECT_ATTRIBUTES attributes;
        IO_STATUS_BLOCK ioStatus;
        HANDLE file = NULL;

        RtlInitUnicodeString(&fileName,
                            L"\\??\\C:\\nabu_stage25_tpg_128x128.raw10");
        InitializeObjectAttributes(&attributes, &fileName,
                                   OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                                   NULL, NULL);
        status = ZwCreateFile(&file, GENERIC_WRITE | SYNCHRONIZE,
                              &attributes, &ioStatus, NULL,
                              FILE_ATTRIBUTE_NORMAL, 0, FILE_OVERWRITE_IF,
                              FILE_SYNCHRONOUS_IO_NONALERT |
                              FILE_NON_DIRECTORY_FILE, NULL, 0);
        Context->TpgFileStatus = (ULONG)status;
        if (NT_SUCCESS(status) && file != NULL) {
            status = ZwWriteFile(file, NULL, NULL, NULL, &ioStatus,
                                 Buffer, NABU_TPG_FRAME_BYTES,
                                 NULL, NULL);
            Context->TpgFileStatus = (ULONG)status;
            if (NT_SUCCESS(status)) {
                Context->TpgFileBytes = (ULONG)ioStatus.Information;
            }
            ZwClose(file);
        }
        Context->TpgCompleted =
            (NT_SUCCESS(status) &&
             Context->TpgFileBytes == NABU_TPG_FRAME_BYTES) ? 1UL : 0UL;
    }

Cleanup:
    if (tpgStarted && csid != NULL) {
        NabuOwnWrite32(csid, 0x600, 0);
        KeMemoryBarrier();
    }
    if (wmStarted && vfe != NULL) {
        NabuOwnWrite32(vfe, wm + 0x08, 0);
        KeMemoryBarrier();
    }
    if (vfe != NULL) {
        MmUnmapIoSpace(vfe, NABU_VFE0_BYTES);
    }
    if (csid != NULL) {
        MmUnmapIoSpace(csid, NABU_CSID0_BYTES);
    }
    if (bandwidthVoted) {
        Context->TpgRtBandwidthReleaseStatus =
            (ULONG)PlatformRequestRtBandwidth(&client, 0, 0, 0);
    }
    if (camcc != NULL) {
        MmUnmapIoSpace(camcc, NABU_CAMCC_BYTES);
    }
    if (acquired) {
        Context->TpgPlatformReleaseStatus =
            (ULONG)PlatformDispatch(&client, NABU_PLAT_CMD_RELEASE,
                                    NULL, NULL, NULL);
    }
    if (opened) {
        PlatformClose(&client);
    }
    if (Context->TpgCompleted != 0) {
        Context->TpgPhase = 100;
    }
    NabuOwnWriteDiagnostics(Context);
}

static BOOLEAN
NabuOwnApplyRearAutoExposure(
    _Inout_ PNABU_OWN_STAGE23_CONTEXT Context,
    _In_ PPLATFORM_CLIENT Client)
{
    LONG requestedGeneration;
    LONG appliedGeneration;
    ULONG exposure;
    ULONG exposureRegister;
    ULONG gainControl;
    ULONG gainRegister;
    NTSTATUS status;

    requestedGeneration = InterlockedCompareExchange(
        &Context->AutoExposureRequestedGeneration, 0, 0);
    appliedGeneration = InterlockedCompareExchange(
        &Context->AutoExposureAppliedGeneration, 0, 0);
    if (requestedGeneration == appliedGeneration) {
        return TRUE;
    }
    exposure = (ULONG)InterlockedCompareExchange(
        &Context->AutoExposureRequestedExposure, 0, 0);
    gainControl = (ULONG)InterlockedCompareExchange(
        &Context->AutoExposureRequestedGain, 0, 0);
    if (exposure < NABU_REAR_EXPOSURE_MIN_LINES ||
        exposure > NABU_REAR_EXPOSURE_MAX_LINES ||
        gainControl < 256UL || gainControl > 3968UL) {
        InterlockedExchange(
            &Context->AutoExposureApplyStatus,
            (LONG)STATUS_INVALID_PARAMETER);
        return FALSE;
    }

    /*
     * CamX passes OV13B10 coarse integration in whole line units.  The
     * sensor's 0x3500..0x3502 register uses a 20-bit value with four
     * fractional bits, so match Android's FillExposureSettings and shift
     * the requested line count before splitting it into register bytes.
     */
    exposureRegister = exposure << 4;

    status = NabuOwnSensorWriteRetry(
        Context, Client, 0x3500,
        (UCHAR)((exposureRegister >> 16) & 0x0FUL));
    if (NT_SUCCESS(status)) {
        status = NabuOwnSensorWriteRetry(
            Context, Client, 0x3501,
            (UCHAR)((exposureRegister >> 8) & 0xFFUL));
    }
    if (NT_SUCCESS(status)) {
        status = NabuOwnSensorWriteRetry(
            Context, Client, 0x3502,
            (UCHAR)(exposureRegister & 0xFFUL));
    }
    /* Android's OV13B10 module already supplies analog gain in Q8. */
    gainRegister = gainControl;
    if (NT_SUCCESS(status)) {
        status = NabuOwnSensorWriteRetry(
            Context, Client, 0x3508,
            (UCHAR)((gainRegister >> 8) & 0x0FUL));
    }
    if (NT_SUCCESS(status)) {
        status = NabuOwnSensorWriteRetry(
            Context, Client, 0x3509,
            (UCHAR)(gainRegister & 0xFFUL));
    }
    InterlockedExchange(
        &Context->AutoExposureApplyStatus, (LONG)status);
    if (NT_SUCCESS(status)) {
        InterlockedExchange(
            &Context->AutoExposureAppliedGeneration,
            requestedGeneration);
        return TRUE;
    }
    return FALSE;
}

static BOOLEAN
NabuOwnCaptureRearPersistentFrame(
    _Inout_ PNABU_OWN_STAGE23_CONTEXT Context,
    _Inout_updates_bytes_(NABU_OWN_DMA_BUFFER_BYTES) PUCHAR Buffer,
    _In_ ULONGLONG DeviceAddress,
    _In_ PUCHAR Csid,
    _In_ PUCHAR Vfe,
    _In_reads_bytes_opt_(NABU_REAR_FRAME_BYTES)
        const UCHAR *PublishBuffer)
{
    LARGE_INTEGER delay;
    LARGE_INTEGER waitFrequency;
    LARGE_INTEGER waitStarted;
    LONGLONG predictedUs;
    LONGLONG guardUs;
    LARGE_INTEGER stageStarted;
    LARGE_INTEGER stageFinished;
    ULONGLONG publishUs;
    ULONGLONG waitUs;
    ULONG packetBefore;
    ULONG packetAfter;
    ULONG csidIrq = 0;
    ULONG busIrq = 0;
    ULONG attempt;
    BOOLEAN changed;

    if (Buffer == NULL || Csid == NULL || Vfe == NULL ||
        DeviceAddress == 0 || DeviceAddress > MAXULONG) {
        return FALSE;
    }

    *(volatile ULONG *)(Buffer + 0) = NABU_REAL_MARKER;
    *(volatile ULONG *)(Buffer + 4) = NABU_REAL_MARKER;
    *(volatile ULONG *)(Buffer + 8) = NABU_REAL_MARKER;
    *(volatile ULONG *)(Buffer + 12) = NABU_REAL_MARKER;
    *(volatile ULONG *)(Buffer + NABU_REAR_FRAME_BYTES - 8) =
        NABU_REAL_MARKER;
    *(volatile ULONG *)(Buffer + NABU_REAR_FRAME_BYTES - 4) =
        NABU_REAL_MARKER;
    KeMemoryBarrier();

    NabuOwnWrite32(Csid, 0x24, 0x0FFFFFFFUL);
    NabuOwnWrite32(Csid, 0x44, 0x00007FFFUL);
    NabuOwnWrite32(Csid, 0x80, 1);
    NabuOwnWrite32(Vfe, 0x2050, 0xFFFFFFFFUL);
    NabuOwnWrite32(Vfe, 0x2054, 0xFFFFFFFFUL);
    NabuOwnWrite32(Vfe, 0x2058, 0xFFFFFFFFUL);
    NabuOwnWrite32(Vfe, 0x2068, 1);

    packetBefore = NabuOwnRead32(Csid, 0x160);
    NabuOwnWrite32(
        Vfe, NABU_VFE_WM0 + 0x14, (ULONG)DeviceAddress);
    NabuOwnWrite32(
        Vfe, NABU_VFE_WM0 + 0x58, NABU_REAR_FRAME_BYTES);
    KeMemoryBarrier();
    NabuOwnWrite32(Vfe, NABU_VFE_WM0 + 0x08, 3);
    KeMemoryBarrier();
    NabuOwnWrite32(Vfe, 0x4AC, 2);
    KeMemoryBarrier();

    /*
     * DMA is now writing the alternate slot.  Compact and publish the
     * previously completed slot while the next optical frame is in flight.
     *
     * The page-list route maps both RAW slots Write-Combined, so the
     * publisher's per-pixel walk of 6.5 MB there costs several times the
     * frame.  One sequential copy into cached pool is cheap by comparison.
     */
    publishUs = 0;
    stageStarted = KeQueryPerformanceCounter(&waitFrequency);
    if (PublishBuffer != NULL && Context->PersistentPublish != NULL) {
        const UCHAR *source = PublishBuffer;

        if (Context->DmaPoolFallbackUsed == 2) {
            if (Context->FrameCachedScratch == NULL) {
                Context->FrameCachedScratch = ExAllocatePool2(
                    POOL_FLAG_NON_PAGED,
                    NABU_REAR_FRAME_BYTES,
                    NABU_OWN_SCRATCH_POOL_TAG);
                Context->FrameCachedScratchStatus =
                    Context->FrameCachedScratch != NULL ?
                        (ULONG)STATUS_SUCCESS :
                        (ULONG)STATUS_INSUFFICIENT_RESOURCES;
            }
            if (Context->FrameCachedScratch != NULL) {
                RtlCopyMemory(
                    Context->FrameCachedScratch,
                    PublishBuffer,
                    NABU_REAR_FRAME_BYTES);
                source = (const UCHAR *)Context->FrameCachedScratch;
                Context->FrameCachedScratchUsed += 1;
            }
        }

        Context->PersistentPublish(
            Context->PersistentPublishContext,
            source,
            NABU_REAR_FRAME_BYTES);
        InterlockedIncrement(&Context->PersistentFrameCount);
    }
    stageFinished = KeQueryPerformanceCounter(NULL);
    if (waitFrequency.QuadPart > 0) {
        publishUs = ((ULONGLONG)(stageFinished.QuadPart -
                                 stageStarted.QuadPart) * 1000000ULL) /
                    (ULONGLONG)waitFrequency.QuadPart;
    }
    Context->PublishUsLast = (ULONG)publishUs;
    if (publishUs > Context->PublishUsMax) {
        Context->PublishUsMax = (ULONG)publishUs;
    }
    Context->PublishUsTotal += (ULONG)publishUs;

    delay.QuadPart = -NABU_REAR_WAIT_SLEEP_US;
    packetAfter = packetBefore;
    waitStarted = KeQueryPerformanceCounter(NULL);
    Context->WaitSleepPolls = 0;
    Context->WaitStallPolls = 0;
    /*
     * Predict the next optical frame boundary from the interval between the
     * last two accepted frames.  That measurement already carries the
     * write-master re-arm latency, which is what really limits the rate, so no
     * line-time constant has to be assumed here.
     */
    predictedUs = (LONGLONG)NABU_REAR_FRAME_PERIOD_INIT_US;
    if (waitFrequency.QuadPart > 0 &&
        Context->FramePeriodAnchorTicks != 0 &&
        Context->FramePeriodUsLast >= NABU_REAR_FRAME_PERIOD_MIN_US &&
        Context->FramePeriodUsLast <= NABU_REAR_FRAME_PERIOD_MAX_US) {
        LONGLONG periodUs = (LONGLONG)Context->FramePeriodUsLast;
        LONGLONG phaseUs =
            ((LONGLONG)(waitStarted.QuadPart -
                        Context->FramePeriodAnchorTicks) * 1000000LL) /
                waitFrequency.QuadPart %
            periodUs;

        if (phaseUs < 0) {
            phaseUs += periodUs;
        }
        predictedUs = periodUs - phaseUs;
    }
    guardUs = (LONGLONG)NABU_REAR_WAIT_GUARD_US;
    if (Context->TimerResolution100Ns >
            NABU_REAR_TIMER_RESOLUTION_100NS / 2UL) {
        LONGLONG coarseGuardUs =
            ((LONGLONG)Context->TimerResolution100Ns * 10LL) + 500LL;

        if (coarseGuardUs > guardUs) {
            guardUs = coarseGuardUs;
        }
    }
    for (attempt = 0; attempt < 400; ++attempt) {
        LARGE_INTEGER waitNow;
        LONGLONG elapsedUs = 0;

        if (InterlockedCompareExchange(
                &Context->PersistentStopRequested, 0, 0) != 0) {
            break;
        }
        packetAfter = NabuOwnRead32(Csid, 0x160);
        csidIrq = NabuOwnRead32(Csid, 0x40);
        busIrq = NabuOwnRead32(Vfe, 0x205C);
        changed =
            *(volatile ULONG *)(
                Buffer + NABU_REAR_FRAME_BYTES - 8) !=
                    NABU_REAL_MARKER ||
            *(volatile ULONG *)(
                Buffer + NABU_REAR_FRAME_BYTES - 4) !=
                    NABU_REAL_MARKER;
        if (changed && packetAfter != packetBefore) {
            break;
        }
        if ((busIrq & 0x7800UL) != 0) {
            break;
        }
        if (waitFrequency.QuadPart > 0) {
            waitNow = KeQueryPerformanceCounter(NULL);
            elapsedUs =
                ((LONGLONG)(waitNow.QuadPart - waitStarted.QuadPart) *
                    1000000LL) / (LONGLONG)waitFrequency.QuadPart;
            if ((ULONGLONG)elapsedUs >= NABU_REAR_FRAME_TIMEOUT_US) {
                break;
            }
        }
        if (elapsedUs + guardUs >= predictedUs &&
            (ULONGLONG)Context->WaitStallPolls *
                    NABU_REAR_WAIT_STALL_US <
                NABU_REAR_WAIT_STALL_BUDGET_US) {
            Context->WaitStallPolls += 1;
            KeStallExecutionProcessor(NABU_REAR_WAIT_STALL_US);
            continue;
        }
        Context->WaitSleepPolls += 1;
        (VOID)KeDelayExecutionThread(
            KernelMode, FALSE, &delay);
    }

    NabuOwnWrite32(Vfe, NABU_VFE_WM0 + 0x08, 0);
    KeMemoryBarrier();
    stageFinished = KeQueryPerformanceCounter(NULL);
    waitUs = waitFrequency.QuadPart > 0 ?
        ((ULONGLONG)(stageFinished.QuadPart - waitStarted.QuadPart) *
            1000000ULL) / (ULONGLONG)waitFrequency.QuadPart : 0ULL;
    Context->WaitUsLast = (ULONG)waitUs;
    if (waitUs > Context->WaitUsMax) {
        Context->WaitUsMax = (ULONG)waitUs;
    }
    Context->WaitUsTotal += (ULONG)waitUs;
    Context->CycleCount += 1;
    changed =
        *(volatile ULONG *)(
            Buffer + NABU_REAR_FRAME_BYTES - 8) != NABU_REAL_MARKER ||
        *(volatile ULONG *)(
            Buffer + NABU_REAR_FRAME_BYTES - 4) != NABU_REAL_MARKER;
    if (changed && packetAfter != packetBefore) {
        if (Context->FramePeriodAnchorTicks != 0 &&
            waitFrequency.QuadPart > 0) {
            ULONGLONG periodUs =
                ((ULONGLONG)(stageFinished.QuadPart -
                             Context->FramePeriodAnchorTicks) * 1000000ULL) /
                    (ULONGLONG)waitFrequency.QuadPart;

            if (periodUs >= NABU_REAR_FRAME_PERIOD_MIN_US &&
                periodUs <= NABU_REAR_FRAME_PERIOD_MAX_US) {
                Context->FramePeriodUsLast = (ULONG)periodUs;
                if (Context->FramePeriodUsMin == 0 ||
                    (ULONG)periodUs < Context->FramePeriodUsMin) {
                    Context->FramePeriodUsMin = (ULONG)periodUs;
                }
                if ((ULONG)periodUs > Context->FramePeriodUsMax) {
                    Context->FramePeriodUsMax = (ULONG)periodUs;
                }
            }
        }
        Context->FramePeriodAnchorTicks = stageFinished.QuadPart;
    }
    Context->RealWaitPolls = attempt < 400 ? attempt + 1 : 400;
    Context->RealWaitExitReason =
        changed && packetAfter != packetBefore ? 1UL :
        ((busIrq & 0x7800UL) != 0 ? 2UL : 4UL);
    Context->RealWaitPacketsFirst = packetBefore;
    Context->RealWaitPacketsLast = packetAfter;
    Context->RealWaitCsidIrqLast = csidIrq;
    Context->RealWaitBusIrqLast = busIrq;
    Context->RealCsidRdiIrq = csidIrq;
    Context->RealCsidTotalPackets = packetAfter;
    Context->RealWmCfg = NabuOwnRead32(Vfe, NABU_VFE_WM0 + 0x08);
    Context->RealWmImageAfter =
        NabuOwnRead32(Vfe, NABU_VFE_WM0 + 0x14);
    Context->RealBufferSample0 = *(volatile ULONG *)(Buffer + 0);
    Context->RealBufferSample1 = *(volatile ULONG *)(Buffer + 4);
    Context->RealBufferSample2 = *(volatile ULONG *)(Buffer + 8);
    Context->RealBufferSample3 = *(volatile ULONG *)(Buffer + 12);
    Context->RealBufferTailSample0 =
        *(volatile ULONG *)(Buffer + NABU_REAR_FRAME_BYTES - 8);
    Context->RealBufferTailSample1 =
        *(volatile ULONG *)(Buffer + NABU_REAR_FRAME_BYTES - 4);
    Context->RealBufferTailChanged = changed ? 1UL : 0UL;
    Context->RealBufferChanged =
        (Context->RealBufferSample0 != NABU_REAL_MARKER ||
         Context->RealBufferSample1 != NABU_REAL_MARKER ||
         Context->RealBufferSample2 != NABU_REAL_MARKER ||
         Context->RealBufferSample3 != NABU_REAL_MARKER) ? 1UL : 0UL;

    if (!changed || packetAfter == packetBefore ||
        (busIrq & 0x7800UL) != 0) {
        if (InterlockedCompareExchange(
                &Context->PersistentStopRequested, 0, 0) == 0) {
            InterlockedIncrement(&Context->PersistentFrameFailureCount);
        }
        return FALSE;
    }

    Context->RealCompleted = 1;
    Context->RealPhase = 60;
    return TRUE;
}

VOID
NabuOwnRunRealCapture(
    _Inout_ PNABU_OWN_STAGE23_CONTEXT Context,
    _Inout_updates_bytes_(NABU_OWN_DMA_BUFFER_BYTES) PUCHAR Buffer,
    _In_ ULONGLONG DeviceAddress)
{
    PLATFORM_CLIENT client;
    PHYSICAL_ADDRESS address;
    PUCHAR camcc = NULL;
    PUCHAR csiphy = NULL;
    PUCHAR csid = NULL;
    PUCHAR vfe = NULL;
    BOOLEAN opened = FALSE;
    BOOLEAN acquired1 = FALSE;
    BOOLEAN acquired2 = FALSE;
    BOOLEAN initialized = FALSE;
    BOOLEAN bandwidthVoted = FALSE;
    BOOLEAN sensorStarted = FALSE;
    BOOLEAN wmStarted = FALSE;
    ULONG consecutivePersistentFailures = 0;
    ULONG wm = NABU_VFE_WM0;
    ULONG resetStatus = 0;
    ULONG attempt;
    ULONG rdiCfg0;
    ULONG master = NABU_OWN_REAR_CCI_MASTER;
    UCHAR chipId[2] = {0};
    UCHAR streamValue = 0;
    NTSTATUS status;

    RtlZeroMemory(&client, sizeof(client));
    Context->RealAttempted = 1;
    Context->RealCompleted = 0;
    Context->RealPlatformOpenStatus = (ULONG)STATUS_NOT_SUPPORTED;
    Context->RealAcquire1Status = (ULONG)STATUS_NOT_SUPPORTED;
    Context->RealPlatformPowerStatus = (ULONG)STATUS_NOT_SUPPORTED;
    Context->RealPlatformPinStatus = (ULONG)STATUS_NOT_SUPPORTED;
    Context->RealInitMasterStatus = (ULONG)STATUS_NOT_SUPPORTED;
    Context->RealAcquire2Status = (ULONG)STATUS_NOT_SUPPORTED;
    Context->RealDeinitMasterStatus = (ULONG)STATUS_NOT_SUPPORTED;
    Context->RealRelease1Status = (ULONG)STATUS_NOT_SUPPORTED;
    Context->RealRelease2Status = (ULONG)STATUS_NOT_SUPPORTED;
    Context->RealRtBandwidthStatus = (ULONG)STATUS_NOT_SUPPORTED;
    Context->RealRtBandwidthReleaseStatus = (ULONG)STATUS_NOT_SUPPORTED;
    Context->RealChipIdStatus = (ULONG)STATUS_NOT_SUPPORTED;
    Context->RealInitStatus = (ULONG)STATUS_NOT_SUPPORTED;
    Context->RealModeStatus = (ULONG)STATUS_NOT_SUPPORTED;
    Context->RealStreamWriteStatus = (ULONG)STATUS_NOT_SUPPORTED;
    Context->RealStreamReadStatus = (ULONG)STATUS_NOT_SUPPORTED;
    Context->RealStreamStopStatus = (ULONG)STATUS_NOT_SUPPORTED;
    Context->RealFileStatus = (ULONG)STATUS_NOT_SUPPORTED;
    Context->RealFileBytes = 0;
    Context->RealWaitPolls = 0;
    Context->RealWaitExitReason = 0;
    Context->RealWaitPacketsFirst = 0;
    Context->RealWaitPacketsLast = 0;
    Context->RealWaitCsidIrqLast = 0;
    Context->RealWaitVfeTopIrqLast = 0;
    Context->RealWaitBusIrqLast = 0;
    Context->RealFrameBytesExpected = NABU_REAR_FRAME_BYTES;
    Context->RealBufferTailChanged = 0;
    Context->RealBufferTailSample0 = NABU_REAL_MARKER;
    Context->RealBufferTailSample1 = NABU_REAL_MARKER;
    Context->RealIovaLow = (ULONG)DeviceAddress;
    Context->RealIovaHigh = (ULONG)(DeviceAddress >> 32);
    Context->RealPhase = 1;
    NabuOwnWriteDiagnostics(Context);

    if (DeviceAddress == 0 || DeviceAddress > MAXULONG ||
        Buffer == NULL || Context->DmaBufferLength < NABU_REAR_FRAME_BYTES) {
        Context->RealPhase = 2;
        goto Cleanup;
    }
    RtlFillMemory(Buffer, Context->DmaBufferLength, 0xAB);
    KeMemoryBarrier();

    status = PlatformOpen(Context->Device, &client);
    Context->RealPlatformOpenStatus = (ULONG)status;
    if (!NT_SUCCESS(status)) {
        Context->RealPhase = 3;
        goto Cleanup;
    }
    opened = TRUE;

    status = PlatformDispatch(&client, NABU_PLAT_CMD_ACQUIRE,
                              NULL, NULL, NULL);
    Context->RealAcquire1Status = (ULONG)status;
    if (!NT_SUCCESS(status)) {
        Context->RealPhase = 4;
        goto Cleanup;
    }
    acquired1 = TRUE;

    status = PlatformPower(&client);
    Context->RealPlatformPowerStatus = (ULONG)status;
    if (!NT_SUCCESS(status)) {
        Context->RealPhase = 5;
        goto Cleanup;
    }
    status = PlatformPinConfig(&client);
    Context->RealPlatformPinStatus = (ULONG)status;
    if (!NT_SUCCESS(status)) {
        Context->RealPhase = 6;
        goto Cleanup;
    }
    status = PlatformInitMaster(&client, master);
    Context->RealInitMasterStatus = (ULONG)status;
    if (!NT_SUCCESS(status)) {
        Context->RealPhase = 7;
        goto Cleanup;
    }
    initialized = TRUE;
    status = PlatformDispatch(&client, NABU_PLAT_CMD_ACQUIRE,
                              NULL, NULL, NULL);
    Context->RealAcquire2Status = (ULONG)status;
    if (!NT_SUCCESS(status)) {
        Context->RealPhase = 8;
        goto Cleanup;
    }
    acquired2 = TRUE;

    if (!NabuOwnPrepareRearSensorRails(Context)) {
        Context->RealPhase = 9;
        goto Cleanup;
    }

    address.QuadPart = NABU_CAMCC_PHYS;
    camcc = (PUCHAR)MmMapIoSpaceEx(address, NABU_CAMCC_BYTES,
                                   PAGE_READWRITE | PAGE_NOCACHE);
    if (camcc == NULL ||
        !NabuOwnEnableIfe0Clocks(Context, camcc) ||
        !NabuOwnEnableRearClockTree(Context, camcc) ||
        !NabuOwnConfigureRearPins()) {
        Context->RealPhase = 10;
        goto Cleanup;
    }
    Context->RealPhase = 11;
    NabuOwnWriteDiagnostics(Context);
    KeStallExecutionProcessor(50000);

    status = NabuOwnSensorReadRetry(Context, &client, 0x300B,
                                    chipId, sizeof(chipId));
    Context->RealChipIdStatus = (ULONG)status;
    Context->RealChipId = ((ULONG)chipId[0] << 8) | chipId[1];
    if (!NT_SUCCESS(status) || Context->RealChipId != 0x0D42UL) {
        Context->RealPhase = 12;
        goto Cleanup;
    }

    status = NabuOwnSensorWriteRetry(Context, &client, 0x0103, 0x01);
    if (!NT_SUCCESS(status)) {
        Context->RealInitStatus = (ULONG)status;
        Context->RealInitFailIndex = 0;
        Context->RealInitFailReg = 0x0103;
        Context->RealPhase = 13;
        goto Cleanup;
    }
    KeStallExecutionProcessor(10000);
    status = NabuOwnWriteSensorTable(Context, &client,
                                     NabuOwnOv13b10Init,
                                     ARRAYSIZE(NabuOwnOv13b10Init),
                                     &Context->RealInitFailIndex,
                                     &Context->RealInitFailReg);
    Context->RealInitStatus = (ULONG)status;
    if (!NT_SUCCESS(status)) {
        Context->RealPhase = 14;
        goto Cleanup;
    }
    status = NabuOwnWriteSensorTable(Context, &client,
                                     NabuOwnOv13b10Mode2104x1560,
                                     ARRAYSIZE(NabuOwnOv13b10Mode2104x1560),
                                     &Context->RealModeFailIndex,
                                     &Context->RealModeFailReg);
    Context->RealModeStatus = (ULONG)status;
    if (!NT_SUCCESS(status)) {
        Context->RealPhase = 15;
        goto Cleanup;
    }
    /* Apply Android warm-start exposure while the sensor is still stopped. */
    (VOID)NabuOwnApplyRearAutoExposure(Context, &client);

    status = PlatformRequestRtBandwidth(&client, 0,
                                        NABU_RT_BANDWIDTH,
                                        NABU_RT_BANDWIDTH);
    Context->RealRtBandwidthStatus = (ULONG)status;
    if (!NT_SUCCESS(status)) {
        Context->RealPhase = 16;
        goto Cleanup;
    }
    bandwidthVoted = TRUE;
    KeStallExecutionProcessor(5000);

    address.QuadPart = NABU_CSIPHY0_PHYS;
    csiphy = (PUCHAR)MmMapIoSpaceEx(address, NABU_CSIPHY0_BYTES,
                                    PAGE_READWRITE | PAGE_NOCACHE);
    address.QuadPart = NABU_CSID0_PHYS;
    csid = (PUCHAR)MmMapIoSpaceEx(address, NABU_CSID0_BYTES,
                                  PAGE_READWRITE | PAGE_NOCACHE);
    address.QuadPart = NABU_VFE0_PHYS;
    vfe = (PUCHAR)MmMapIoSpaceEx(address, NABU_VFE0_BYTES,
                                 PAGE_READWRITE | PAGE_NOCACHE);
    if (csiphy == NULL || csid == NULL || vfe == NULL) {
        Context->RealPhase = 20;
        goto Cleanup;
    }

    Context->RealCsiphyHwVersion = CsiphyReadHwVersion(csiphy);
    CsiphyLanesEnable(csiphy, 4, 0x15);
    KeStallExecutionProcessor(5000);
    CsiphyIrqClear(csiphy);

    NabuOwnWrite32(vfe, 0x5C, 0x80000000UL);
    NabuOwnWrite32(vfe, 0x60, 0);
    NabuOwnWrite32(vfe, 0x64, 0xFFFFFFFFUL);
    NabuOwnWrite32(vfe, 0x68, 0xFFFFFFFFUL);
    NabuOwnWrite32(vfe, 0x58, 1);
    KeMemoryBarrier();
    NabuOwnWrite32(vfe, 0x18, 0x00003F9FUL);
    KeMemoryBarrier();
    for (attempt = 0; attempt < 1000; ++attempt) {
        resetStatus = NabuOwnRead32(vfe, 0x6C);
        if ((resetStatus & 0x80000000UL) != 0) {
            break;
        }
        KeStallExecutionProcessor(10);
    }
    Context->TpgVfeResetStatus = resetStatus;
    Context->TpgVfeResetPolls = attempt;
    if ((resetStatus & 0x80000000UL) == 0) {
        Context->RealPhase = 21;
        goto Cleanup;
    }
    NabuOwnWrite32(vfe, 0x64, resetStatus);
    NabuOwnWrite32(vfe, 0x68, NabuOwnRead32(vfe, 0x70));
    NabuOwnWrite32(vfe, 0x58, 1);

    NabuOwnWrite32(csid, 0x08, 1);
    KeStallExecutionProcessor(2000);
    NabuOwnWrite32(csid, 0x08, 0);
    KeStallExecutionProcessor(500);
    NabuOwnWrite32(csid, 0x600, 0);
    NabuOwnWrite32(csid, 0x100, 3UL | (0x3210UL << 4));
    NabuOwnWrite32(csid, 0x104, 0x41);
    NabuOwnWrite32(csid, 0x108, 0);
    NabuOwnWrite32(csid, 0x78, 1);
    NabuOwnWrite32(csid, 0x28, 0x0FFFFFFFUL);
    NabuOwnWrite32(csid, 0x48, 0x7FFF);
    NabuOwnWrite32(csid, 0x80, 1);
    NabuOwnWrite32(csid, 0x24, 0x0FFFFFFFUL);
    NabuOwnWrite32(csid, 0x44, 0x7FFF);
    rdiCfg0 = (0x2BUL << 16) | (0x2UL << 12) |
              (0x1UL << 10) | (1UL << 2) | 3UL | (1UL << 31);
    NabuOwnWrite32(csid, 0x300, rdiCfg0);
    NabuOwnWrite32(csid, 0x304, 2);
    NabuOwnWrite32(csid, 0x30C, 0);
    NabuOwnWrite32(csid, 0x310, 1);
    NabuOwnWrite32(csid, 0x314, 0);
    NabuOwnWrite32(csid, 0x318, 1);
    NabuOwnWrite32(csid, 0x324, 0);
    NabuOwnWrite32(csid, 0x328, 1);
    NabuOwnWrite32(csid, 0x32C, 0);
    NabuOwnWrite32(csid, 0x330, 1);
    NabuOwnWrite32(csid, 0x308, 1);
    Context->RealCsidRxCfg0 = NabuOwnRead32(csid, 0x100);
    Context->RealCsidRxCfg1 = NabuOwnRead32(csid, 0x104);
    Context->RealCsidRdiCfg0 = NabuOwnRead32(csid, 0x300);

    NabuOwnWrite32(vfe, 0x226C, 0x82);
    NabuOwnWrite32(vfe, 0x2080, 0);
    NabuOwnWrite32(vfe, 0x200C, 0xFFFFF);
    NabuOwnWrite32(vfe, 0x211C, 0);
    NabuOwnWrite32(vfe, 0x2084, 0xFFFFFF);
    NabuOwnWrite32(vfe, 0x2050, 0xFFFFFFFFUL);
    NabuOwnWrite32(vfe, 0x2054, 0xFFFFFFFFUL);
    NabuOwnWrite32(vfe, 0x2058, 0xFFFFFFFFUL);
    NabuOwnWrite32(vfe, 0x2068, 1);

    NabuOwnWrite32(vfe, wm + 0x08, 0);
    NabuOwnWrite32(vfe, wm + 0x5C, 0xF);
    NabuOwnWrite32(vfe, wm + 0x1C, 0xFF01);
    NabuOwnWrite32(vfe, wm + 0x20, 0);
    NabuOwnWrite32(vfe, wm + 0x24, 0);
    NabuOwnWrite32(vfe, wm + 0x28, 0xFF01);
    NabuOwnWrite32(vfe, wm + 0x0C, 0);
    NabuOwnWrite32(vfe, wm + 0x18, 0);
    NabuOwnWrite32(vfe, wm + 0x48, 0);
    NabuOwnWrite32(vfe, wm + 0x4C, 1);
    NabuOwnWrite32(vfe, wm + 0x50, 0);
    NabuOwnWrite32(vfe, wm + 0x54, 1);
    NabuOwnWrite32(vfe, wm + 0x14, (ULONG)DeviceAddress);
    NabuOwnWrite32(vfe, wm + 0x58, NABU_REAR_FRAME_BYTES);
    Context->RealWmImageBefore = NabuOwnRead32(vfe, wm + 0x14);
    NabuOwnWrite32(vfe, wm + 0x08, 3);
    wmStarted = TRUE;
    KeMemoryBarrier();
    NabuOwnWrite32(vfe, 0x4AC, 2);
    KeMemoryBarrier();

    Context->RealPhase = 50;
    NabuOwnWriteDiagnostics(Context);
    status = NabuOwnSensorWriteRetry(Context, &client, 0x0100, 0x01);
    Context->RealStreamWriteStatus = (ULONG)status;
    if (!NT_SUCCESS(status)) {
        Context->RealPhase = 51;
        goto Cleanup;
    }
    sensorStarted = TRUE;
    status = NabuOwnSensorReadRetry(Context, &client, 0x0100,
                                    &streamValue, sizeof(streamValue));
    Context->RealStreamReadStatus = (ULONG)status;
    Context->RealStreamReadback = streamValue;
    if (!NT_SUCCESS(status) || streamValue != 1) {
        Context->RealPhase = 52;
        goto Cleanup;
    }

    for (attempt = 0; attempt < 200; ++attempt) {
        ULONG packets;
        ULONG csidIrq;
        ULONG vfeTopIrq;
        ULONG busIrq;

        KeStallExecutionProcessor(10000);
        KeMemoryBarrier();

        packets = NabuOwnRead32(csid, 0x160);
        csidIrq = NabuOwnRead32(csid, 0x40);
        vfeTopIrq = NabuOwnRead32(vfe, 0x6C);
        busIrq = NabuOwnRead32(vfe, 0x205C);

        if (attempt == 0) {
            Context->RealWaitPacketsFirst = packets;
        }
        Context->RealWaitPacketsLast = packets;
        Context->RealWaitCsidIrqLast = csidIrq;
        Context->RealWaitVfeTopIrqLast = vfeTopIrq;
        Context->RealWaitBusIrqLast = busIrq;
        Context->RealWaitPolls = attempt + 1;

        if (*(volatile ULONG *)(Buffer + NABU_REAR_FRAME_BYTES - 8) !=
                NABU_REAL_MARKER ||
            *(volatile ULONG *)(Buffer + NABU_REAR_FRAME_BYTES - 4) !=
                NABU_REAL_MARKER) {
            Context->RealWaitExitReason = 1;
            break;
        }
        if ((busIrq & 0x7800UL) != 0) {
            Context->RealWaitExitReason = 2;
            break;
        }
        if ((csidIrq & 0x4UL) != 0) {
            Context->RealWaitExitReason = 3;
            break;
        }
    }
    if (attempt == 200) {
        Context->RealWaitExitReason = 4;
    }

    Context->RealWmCfg = NabuOwnRead32(vfe, wm + 0x08);
    Context->RealWmStatus0 = NabuOwnRead32(vfe, wm + 0x00);
    Context->RealWmStatus1 = NabuOwnRead32(vfe, wm + 0x04);
    Context->RealWmImageAfter = NabuOwnRead32(vfe, wm + 0x14);
    Context->RealVfeTopIrq0 = NabuOwnRead32(vfe, 0x6C);
    Context->RealVfeTopIrq1 = NabuOwnRead32(vfe, 0x70);
    Context->RealVfeBusIrq0 = NabuOwnRead32(vfe, 0x205C);
    Context->RealVfeBusIrq1 = NabuOwnRead32(vfe, 0x2060);
    Context->RealVfeBusIrq2 = NabuOwnRead32(vfe, 0x2064);
    Context->RealVfeBusDebug0 = NabuOwnRead32(vfe, 0x2270);
    Context->RealCsidRdiIrq = NabuOwnRead32(csid, 0x40);
    Context->RealCsidTotalPackets = NabuOwnRead32(csid, 0x160);
    Context->RealCsiphyIrq5 = NabuOwnRead32(csiphy, 0x8C4);
    Context->RealCsiphyIrq8 = NabuOwnRead32(csiphy, 0x8D0);
    Context->RealBufferSample0 = *(volatile ULONG *)(Buffer + 0);
    Context->RealBufferSample1 = *(volatile ULONG *)(Buffer + 4);
    Context->RealBufferSample2 = *(volatile ULONG *)(Buffer + 8);
    Context->RealBufferSample3 = *(volatile ULONG *)(Buffer + 12);
    Context->RealBufferTailSample0 =
        *(volatile ULONG *)(Buffer + NABU_REAR_FRAME_BYTES - 8);
    Context->RealBufferTailSample1 =
        *(volatile ULONG *)(Buffer + NABU_REAR_FRAME_BYTES - 4);
    Context->RealBufferTailChanged =
        (Context->RealBufferTailSample0 != NABU_REAL_MARKER ||
         Context->RealBufferTailSample1 != NABU_REAL_MARKER) ? 1UL : 0UL;
    Context->RealBufferChanged =
        (Context->RealBufferSample0 != NABU_REAL_MARKER ||
         Context->RealBufferSample1 != NABU_REAL_MARKER ||
         Context->RealBufferSample2 != NABU_REAL_MARKER ||
         Context->RealBufferSample3 != NABU_REAL_MARKER) ? 1UL : 0UL;
    Context->RealPhase = 60;
    NabuOwnWriteDiagnostics(Context);

    if (InterlockedCompareExchange(
            &Context->PersistentMode, 0, 0) != 0 &&
        Context->RealBufferTailChanged != 0 &&
        Context->RealBufferChanged != 0) {
        PUCHAR completedBuffer = Buffer;
        PUCHAR nextBuffer = Buffer + NABU_REAR_FRAME_BYTES;
        ULONGLONG completedAddress = DeviceAddress;
        ULONGLONG nextAddress =
            DeviceAddress + (ULONGLONG)NABU_REAR_FRAME_BYTES;
        BOOLEAN completedPending = TRUE;
        BOOLEAN timerRequested = FALSE;

        /*
         * Keep two RAW slots inside one mapped 16 MiB DMA buffer.  The next
         * slot is armed before the previous slot is compacted for AVStream,
         * so CPU conversion no longer creates an avoidable gap between
         * optical frames.
         */
        NabuOwnWrite32(vfe, wm + 0x08, 0);
        wmStarted = FALSE;
        KeMemoryBarrier();
        Context->RealCompleted = 1;
        Context->FramePeriodAnchorTicks = 0;
        Context->FramePeriodUsLast = 0;
        Context->FramePeriodUsMin = 0;
        Context->FramePeriodUsMax = 0;
        /*
         * The frame wait below is bounded by the system timer tick, not by the
         * delay it asks for.  Raise the resolution only for the duration of the
         * capture session and give it back on the way out.
         */
        Context->TimerResolution100Ns = ExSetTimerResolution(
            NABU_REAR_TIMER_RESOLUTION_100NS, TRUE);
        timerRequested = TRUE;

        if (Context->DmaBufferLength <
                (NABU_REAR_FRAME_BYTES * 2UL) ||
            nextAddress > MAXULONG) {
            InterlockedExchange(
                &Context->PersistentTerminalStatus,
                (LONG)STATUS_BUFFER_TOO_SMALL);
        } else while (InterlockedCompareExchange(
                          &Context->PersistentStopRequested, 0, 0) == 0) {
            BOOLEAN captured = NabuOwnCaptureRearPersistentFrame(
                Context,
                nextBuffer,
                nextAddress,
                csid,
                vfe,
                completedPending ? completedBuffer : NULL);
            (VOID)NabuOwnApplyRearAutoExposure(Context, &client);
            completedPending = FALSE;
            if (!captured) {
                if (InterlockedCompareExchange(
                        &Context->PersistentStopRequested, 0, 0) == 0) {
                    ++consecutivePersistentFailures;
                    if (consecutivePersistentFailures > (ULONG)
                            InterlockedCompareExchange(
                                &Context->PersistentConsecutiveFailurePeak,
                                0,
                                0)) {
                        InterlockedExchange(
                            &Context->PersistentConsecutiveFailurePeak,
                            (LONG)consecutivePersistentFailures);
                    }
                    /*
                     * One missed DMA completion is recoverable: IRQ status
                     * and the write master are cleared and re-armed by the
                     * next iteration.  Tear down the complete sensor/CSI
                     * session only after a sustained series of misses.
                     * A full sensor/CSI restart costs roughly two seconds;
                     * individual DMA misses recover on the next re-arm.
                     */
                    if (consecutivePersistentFailures >= 8UL) {
                        InterlockedExchange(
                            &Context->PersistentTerminalStatus,
                            (LONG)(Context->RealWaitExitReason == 2UL ?
                                STATUS_IO_DEVICE_ERROR :
                                STATUS_IO_TIMEOUT));
                        break;
                    }
                }
            } else {
                PUCHAR swapBuffer = completedBuffer;
                ULONGLONG swapAddress = completedAddress;

                consecutivePersistentFailures = 0;
                completedBuffer = nextBuffer;
                completedAddress = nextAddress;
                nextBuffer = swapBuffer;
                nextAddress = swapAddress;
                completedPending = TRUE;
            }
        }
        if (timerRequested) {
            (VOID)ExSetTimerResolution(0, FALSE);
            timerRequested = FALSE;
        }
    }

    status = NabuOwnSensorWriteRetry(Context, &client, 0x0100, 0x00);
    Context->RealStreamStopStatus = (ULONG)status;
    sensorStarted = FALSE;
    NabuOwnWrite32(vfe, wm + 0x08, 0);
    wmStarted = FALSE;
    NabuOwnWrite32(csid, 0x308, 0);
    CsiphyLanesDisable(csiphy);
    KeMemoryBarrier();

    if (Context->RealBufferChanged != 0) {
#if !defined(AVSTREAM_BUILD) && !defined(NABU_REAR_CAPTURE_LIBRARY)
        UNICODE_STRING fileName;
        OBJECT_ATTRIBUTES attributes;
        IO_STATUS_BLOCK ioStatus;
        HANDLE file = NULL;

        RtlInitUnicodeString(&fileName,
                            L"\\??\\C:\\nabu_stage39_rear_2104x1560.raw16");
        InitializeObjectAttributes(&attributes, &fileName,
                                   OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                                   NULL, NULL);
        status = ZwCreateFile(&file, GENERIC_WRITE | SYNCHRONIZE,
                              &attributes, &ioStatus, NULL,
                              FILE_ATTRIBUTE_NORMAL, 0, FILE_OVERWRITE_IF,
                              FILE_SYNCHRONOUS_IO_NONALERT |
                              FILE_NON_DIRECTORY_FILE, NULL, 0);
        Context->RealFileStatus = (ULONG)status;
        if (NT_SUCCESS(status) && file != NULL) {
            status = ZwWriteFile(file, NULL, NULL, NULL, &ioStatus,
                                 Buffer, NABU_REAR_FRAME_BYTES,
                                 NULL, NULL);
            Context->RealFileStatus = (ULONG)status;
            if (NT_SUCCESS(status)) {
                Context->RealFileBytes = (ULONG)ioStatus.Information;
            }
            ZwClose(file);
        }
        Context->RealCompleted =
            (Context->RealBufferTailChanged != 0 && NT_SUCCESS(status) &&
             Context->RealFileBytes == NABU_REAR_FRAME_BYTES) ? 1UL : 0UL;
#else
        //
        // The AVStream path consumes the DMA buffer directly.  Do not create
        // files from an app-open operation.
        //
        Context->RealFileStatus = (ULONG)STATUS_NOT_SUPPORTED;
        Context->RealFileBytes = 0;
        Context->RealCompleted =
            (Context->RealBufferTailChanged != 0) ? 1UL : 0UL;
#endif
    }

Cleanup:
    if (sensorStarted) {
        Context->RealStreamStopStatus =
            (ULONG)NabuOwnSensorWriteRetry(Context, &client, 0x0100, 0x00);
    }
    if (wmStarted && vfe != NULL) {
        NabuOwnWrite32(vfe, wm + 0x08, 0);
        KeMemoryBarrier();
    }
    if (csid != NULL) {
        NabuOwnWrite32(csid, 0x308, 0);
    }
    if (csiphy != NULL) {
        CsiphyLanesDisable(csiphy);
        MmUnmapIoSpace(csiphy, NABU_CSIPHY0_BYTES);
    }
    if (vfe != NULL) {
        MmUnmapIoSpace(vfe, NABU_VFE0_BYTES);
    }
    if (csid != NULL) {
        MmUnmapIoSpace(csid, NABU_CSID0_BYTES);
    }
    if (bandwidthVoted) {
        Context->RealRtBandwidthReleaseStatus =
            (ULONG)PlatformRequestRtBandwidth(&client, 0, 0, 0);
    }
    if (camcc != NULL) {
        MmUnmapIoSpace(camcc, NABU_CAMCC_BYTES);
    }
    if (initialized) {
        Context->RealDeinitMasterStatus =
            (ULONG)PlatformDispatch(&client, PLAT_CMD_I2C_MASTER_DEINIT,
                                    &master, NULL, NULL);
    }
    if (acquired2) {
        Context->RealRelease2Status =
            (ULONG)PlatformDispatch(&client, NABU_PLAT_CMD_RELEASE,
                                    NULL, NULL, NULL);
    }
    if (acquired1) {
        Context->RealRelease1Status =
            (ULONG)PlatformDispatch(&client, NABU_PLAT_CMD_RELEASE,
                                    NULL, NULL, NULL);
    }
    if (opened) {
        PlatformClose(&client);
    }
    if (Context->RealCompleted != 0) {
        Context->RealPhase = 100;
    }
    NabuOwnWriteDiagnostics(Context);
}

static NTSTATUS
NabuOwnRearLegacyStateCallback(
    _In_opt_ PVOID ContextBank,
    _In_opt_ PVOID GetFaultInformation)
{
    UNREFERENCED_PARAMETER(ContextBank);
    UNREFERENCED_PARAMETER(GetFaultInformation);
    return STATUS_SUCCESS;
}

VOID
NabuOwnRunVcmProbe(_Inout_ PNABU_OWN_STAGE23_CONTEXT Context)
{
    PWSTR interfaceList = NULL;
    UNICODE_STRING interfaceName;
    PFILE_OBJECT fileObject = NULL;
    PDEVICE_OBJECT deviceObject = NULL;
    KEVENT event;
    IO_STATUS_BLOCK ioStatus;
    PIRP irp = NULL;
    PIO_STACK_LOCATION stack;
    NABU_SMMU_INTERFACE smmuInterface;
    UCHAR configuration[NABU_SMMU_CONFIG_BYTES];
    PVOID associations[NABU_SMMU_MAX_CONTEXT_BANKS];
    PVOID vcmRegion = NULL;
    PVOID guardReservation = NULL;
    PVOID reservation = NULL;
    PMDL mdl = NULL;
    BOOLEAN mdlPagesAllocated = FALSE;
    BOOLEAN mdlPagesMapped = FALSE;
    BOOLEAN usePageList;
#ifdef AVSTREAM_BUILD
    PVOID conversionAddress = NULL;
#endif
    ULONG associated = 0;
    ULONG activated = 0;
    ULONG terminalPhase = 0;
    BOOLEAN stateEnabled = FALSE;
    BOOLEAN legacyCallbackRegistered = FALSE;
    BOOLEAN iommuMode = FALSE;
    BOOLEAN backed = FALSE;
    ULONGLONG rawCount = 0;
    ULONGLONG contextHandle;
    ULONGLONG deviceAddress;
    ULONGLONG guardAddress;
    ULONGLONG reserveBytes;
    PHYSICAL_ADDRESS physicalAddress;
    PVOID virtualAddress = NULL;
    ULONG i;
    NTSTATUS status;
    PUCHAR table;
    NABU_SMMU_GET_CONFIG getConfig = NULL;
    NABU_SMMU_SET_STATE setState = NULL;
    NABU_SMMU_LEGACY_CALLBACK_REGISTER legacyCallbackRegister = NULL;
    NABU_SMMU_LEGACY_CALLBACK_REMOVE legacyCallbackRemove = NULL;
    NABU_SMMU_IS_IOMMU isIommu = NULL;
    NABU_VCM_CREATE vcmCreate = NULL;
    NABU_VCM_FREE vcmFree = NULL;
    NABU_VCM_RESERVE vcmReserve = NULL;
    NABU_VCM_HANDLE_STATUS vcmRelease = NULL;
    NABU_VCM_BACK vcmBack = NULL;
    NABU_VCM_HANDLE_STATUS vcmUnback = NULL;
    NABU_VCM_ASSOC vcmAssoc = NULL;
    NABU_VCM_HANDLE_STATUS vcmDeassoc = NULL;
    NABU_VCM_HANDLE_STATUS vcmActivate = NULL;
    NABU_VCM_HANDLE_STATUS vcmDeactivate = NULL;
    NABU_VCM_GET_DEV_ADDR vcmGetDevAddr = NULL;

    RtlZeroMemory(&smmuInterface, sizeof(smmuInterface));
    RtlZeroMemory(configuration, sizeof(configuration));
    RtlZeroMemory(associations, sizeof(associations));
    Context->SmmuEnumerateStatus = (ULONG)STATUS_NOT_SUPPORTED;
    Context->SmmuOpenStatus = (ULONG)STATUS_NOT_SUPPORTED;
    Context->SmmuQueryStatus = (ULONG)STATUS_NOT_SUPPORTED;
    Context->SmmuGetConfigStatus = (ULONG)STATUS_NOT_SUPPORTED;
    Context->SmmuConfigSelector = NABU_SMMU_CONFIG_SELECTOR;
    Context->VcmCreateStatus = (ULONG)STATUS_NOT_SUPPORTED;
    Context->VcmAssociationFailIndex = MAXULONG;
    Context->VcmActivationFailIndex = MAXULONG;
    Context->VcmSetStateStatus = (ULONG)STATUS_NOT_SUPPORTED;
    Context->VcmReserveStatus = (ULONG)STATUS_NOT_SUPPORTED;
    Context->VcmGuardReserveStatus = (ULONG)STATUS_NOT_SUPPORTED;
    Context->VcmGuardReleaseStatus = (ULONG)STATUS_NOT_SUPPORTED;
    Context->VcmBackStatus = (ULONG)STATUS_NOT_SUPPORTED;
    Context->VcmUnbackStatus = (ULONG)STATUS_NOT_SUPPORTED;
    Context->VcmReleaseStatus = (ULONG)STATUS_NOT_SUPPORTED;
    Context->VcmDeactivateStatus = (ULONG)STATUS_NOT_SUPPORTED;
    Context->VcmSetStateOffStatus = (ULONG)STATUS_NOT_SUPPORTED;
    Context->VcmDeassocStatus = (ULONG)STATUS_NOT_SUPPORTED;
    Context->VcmFreeStatus = (ULONG)STATUS_NOT_SUPPORTED;

    Context->VcmPhase = 1;
    NabuOwnWriteDiagnostics(Context);
    status = IoGetDeviceInterfaces(&NabuOwnSmmuDeviceInterfaceGuid,
                                   NULL, FALSE, &interfaceList);
    Context->SmmuEnumerateStatus = (ULONG)status;
    if (!NT_SUCCESS(status) || interfaceList == NULL || interfaceList[0] == L'\0') {
        goto Cleanup;
    }

    Context->SmmuDeviceFound = 1;
    Context->VcmPhase = 2;
    NabuOwnWriteDiagnostics(Context);
    RtlInitUnicodeString(&interfaceName, interfaceList);
    status = IoGetDeviceObjectPointer(&interfaceName,
                                      NABU_SMMU_DESIRED_ACCESS,
                                      &fileObject,
                                      &deviceObject);
    Context->SmmuOpenStatus = (ULONG)status;
    if (!NT_SUCCESS(status)) {
        goto Cleanup;
    }

    Context->VcmPhase = 3;
    NabuOwnWriteDiagnostics(Context);
    KeInitializeEvent(&event, NotificationEvent, FALSE);
    RtlZeroMemory(&ioStatus, sizeof(ioStatus));
    irp = IoBuildSynchronousFsdRequest(IRP_MJ_PNP, deviceObject,
                                       NULL, 0, NULL, &event, &ioStatus);
    if (irp == NULL) {
        Context->SmmuQueryStatus = (ULONG)STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }

    stack = IoGetNextIrpStackLocation(irp);
    stack->MinorFunction = IRP_MN_QUERY_INTERFACE;
    stack->Parameters.QueryInterface.InterfaceType =
        (LPGUID)&NabuOwnSmmuQueryInterfaceGuid;
    stack->Parameters.QueryInterface.Size = (USHORT)sizeof(smmuInterface);
    stack->Parameters.QueryInterface.Version = 0;
    stack->Parameters.QueryInterface.Interface = (PINTERFACE)&smmuInterface;
    stack->Parameters.QueryInterface.InterfaceSpecificData = NULL;

    status = IoCallDriver(deviceObject, irp);
    if (status == STATUS_PENDING) {
        (VOID)KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, NULL);
        status = ioStatus.Status;
    }
    Context->SmmuQueryStatus = (ULONG)status;
    if (!NT_SUCCESS(status)) {
        goto Cleanup;
    }

    Context->SmmuInterfaceSize = smmuInterface.Header.Size;
    Context->SmmuInterfaceVersion = smmuInterface.Header.Version;
    if (smmuInterface.Header.Size < NABU_SMMU_INTERFACE_BYTES) {
        Context->SmmuQueryStatus = (ULONG)STATUS_INFO_LENGTH_MISMATCH;
        goto Cleanup;
    }

    table = (PUCHAR)&smmuInterface;
    getConfig = (NABU_SMMU_GET_CONFIG)NabuOwnGetInterfaceFunction(
        table, NABU_SMMU_GET_CONFIG_OFFSET);
    if (getConfig == NULL) {
        Context->SmmuGetConfigStatus = (ULONG)STATUS_PROCEDURE_NOT_FOUND;
        goto Cleanup;
    }

    Context->VcmPhase = 4;
    NabuOwnWriteDiagnostics(Context);
    status = getConfig(smmuInterface.Header.Context,
                       NABU_SMMU_CONFIG_SELECTOR,
                       configuration);
    Context->SmmuGetConfigStatus = (ULONG)status;
    if (!NT_SUCCESS(status)) {
        goto Cleanup;
    }

    RtlCopyMemory(&rawCount, configuration, sizeof(rawCount));
    Context->SmmuContextCountRaw = (ULONG)rawCount;
    Context->SmmuContextCountAccepted = (ULONG)rawCount;
    if (Context->SmmuContextCountAccepted > NABU_SMMU_MAX_CONTEXT_BANKS) {
        Context->SmmuContextCountAccepted = NABU_SMMU_MAX_CONTEXT_BANKS;
    }
    for (i = 0; i < Context->SmmuContextCountAccepted; ++i) {
        PUCHAR entry = configuration + sizeof(rawCount) +
                       (i * NABU_SMMU_CONFIG_ENTRY_BYTES);
        Context->SmmuContextIndex[i] = entry[0];
        RtlCopyMemory(&contextHandle, entry + sizeof(ULONGLONG),
                      sizeof(contextHandle));
        Context->SmmuContextHandleLow[i] = (ULONG)contextHandle;
        Context->SmmuContextHandleHigh[i] = (ULONG)(contextHandle >> 32);
    }

    Context->VcmPhase = 5;
    NabuOwnWriteDiagnostics(Context);
    if (Context->VcmModeRequested == 1) {
        Context->VcmCompleted = 1;
        goto Cleanup;
    }
    if (rawCount == 0 || rawCount > NABU_SMMU_MAX_CONTEXT_BANKS) {
        Context->SmmuGetConfigStatus = (ULONG)STATUS_BUFFER_OVERFLOW;
        goto Cleanup;
    }

    setState = (NABU_SMMU_SET_STATE)NabuOwnGetInterfaceFunction(
        table, NABU_SMMU_SET_STATE_OFFSET);
    legacyCallbackRegister =
        (NABU_SMMU_LEGACY_CALLBACK_REGISTER)NabuOwnGetInterfaceFunction(
            table, NABU_SMMU_LEGACY_CB_OFFSET);
    legacyCallbackRemove =
        (NABU_SMMU_LEGACY_CALLBACK_REMOVE)NabuOwnGetInterfaceFunction(
            table, NABU_SMMU_LEGACY_CB_REMOVE_OFFSET);
    isIommu = (NABU_SMMU_IS_IOMMU)NabuOwnGetInterfaceFunction(
        table, NABU_IOMMU_IS_IOMMU_OFFSET);
    if (isIommu != NULL) {
        iommuMode = isIommu() ? TRUE : FALSE;
    }
    vcmCreate = (NABU_VCM_CREATE)NabuOwnGetInterfaceFunction(
        table, NABU_VCM_CREATE_OFFSET);
    vcmFree = (NABU_VCM_FREE)NabuOwnGetInterfaceFunction(
        table, NABU_VCM_FREE_OFFSET);
    vcmAssoc = (NABU_VCM_ASSOC)NabuOwnGetInterfaceFunction(
        table, NABU_VCM_ASSOC_OFFSET);
    vcmDeassoc = (NABU_VCM_HANDLE_STATUS)NabuOwnGetInterfaceFunction(
        table, NABU_VCM_DEASSOC_OFFSET);
    vcmActivate = (NABU_VCM_HANDLE_STATUS)NabuOwnGetInterfaceFunction(
        table, NABU_VCM_ACTIVATE_OFFSET);
    vcmDeactivate = (NABU_VCM_HANDLE_STATUS)NabuOwnGetInterfaceFunction(
        table, NABU_VCM_DEACTIVATE_OFFSET);
    if (setState == NULL || vcmCreate == NULL || vcmFree == NULL ||
        vcmAssoc == NULL || vcmDeassoc == NULL ||
        vcmActivate == NULL || vcmDeactivate == NULL) {
        Context->VcmCreateStatus = (ULONG)STATUS_PROCEDURE_NOT_FOUND;
        goto Cleanup;
    }

    Context->HardwareTouched = 1;
    Context->VcmCreateAttempted = 1;
    Context->VcmPhase = 10;
    NabuOwnWriteDiagnostics(Context);
    vcmRegion = vcmCreate(0, NABU_VCM_REGION_BYTES);
    Context->VcmRegionHandleLow = (ULONG)(ULONG_PTR)vcmRegion;
    Context->VcmCreateStatus = vcmRegion != NULL ?
        (ULONG)STATUS_SUCCESS : (ULONG)STATUS_UNSUCCESSFUL;
    if (vcmRegion == NULL) {
        goto Cleanup;
    }

    Context->VcmAssociationAttempted = 1;
    for (i = 0; i < Context->SmmuContextCountAccepted; ++i) {
        contextHandle = ((ULONGLONG)Context->SmmuContextHandleHigh[i] << 32) |
                        Context->SmmuContextHandleLow[i];
        Context->VcmPhase = 11;
        NabuOwnWriteDiagnostics(Context);
        associations[i] = vcmAssoc(vcmRegion,
                                   (PVOID)(ULONG_PTR)contextHandle,
                                   0);
        if (associations[i] == NULL) {
            Context->VcmAssociationFailIndex = i;
            goto Cleanup;
        }
        associated += 1;
        Context->VcmAssociationCount = associated;
    }

    Context->VcmPhase = 12;
    NabuOwnWriteDiagnostics(Context);
    if (!iommuMode) {
        if (legacyCallbackRegister == NULL ||
            legacyCallbackRemove == NULL) {
            Context->VcmSetStateStatus =
                (ULONG)STATUS_PROCEDURE_NOT_FOUND;
            goto Cleanup;
        }
        legacyCallbackRegister(
            smmuInterface.Header.Context,
            NABU_SMMU_CONFIG_SELECTOR,
            NabuOwnRearLegacyStateCallback,
            Context);
        legacyCallbackRegistered = TRUE;
        Context->VcmSetStateStatus = (ULONG)STATUS_NOT_SUPPORTED;
    } else {
        status = setState(
            smmuInterface.Header.Context,
            NABU_SMMU_CONFIG_SELECTOR);
        Context->VcmSetStateStatus = (ULONG)status;
        if (!NT_SUCCESS(status)) {
            goto Cleanup;
        }
        stateEnabled = TRUE;
    }

    for (i = 0; i < associated; ++i) {
        Context->VcmPhase = 13;
        NabuOwnWriteDiagnostics(Context);
        status = vcmActivate(associations[i]);
        if (!NT_SUCCESS(status)) {
            Context->VcmActivationFailIndex = i;
            Context->VcmDeactivateStatus = (ULONG)status;
            goto Cleanup;
        }
        activated += 1;
        Context->VcmActivationCount = activated;
    }

    if (Context->VcmModeRequested == 2) {
        Context->VcmCompleted = 1;
        goto Cleanup;
    }

    vcmReserve = (NABU_VCM_RESERVE)NabuOwnGetInterfaceFunction(
        table, NABU_VCM_RESERVE_OFFSET);
    vcmRelease = (NABU_VCM_HANDLE_STATUS)NabuOwnGetInterfaceFunction(
        table, NABU_VCM_RELEASE_OFFSET);
    vcmBack = (NABU_VCM_BACK)NabuOwnGetInterfaceFunction(
        table, NABU_VCM_BACK_OFFSET);
    vcmUnback = (NABU_VCM_HANDLE_STATUS)NabuOwnGetInterfaceFunction(
        table, NABU_VCM_UNBACK_OFFSET);
    vcmGetDevAddr = (NABU_VCM_GET_DEV_ADDR)NabuOwnGetInterfaceFunction(
        table, NABU_VCM_GET_DEV_ADDR_OFFSET);
    if (vcmReserve == NULL || vcmRelease == NULL || vcmBack == NULL ||
        vcmUnback == NULL || vcmGetDevAddr == NULL ||
        (Context->DmaCommonBuffer == NULL && Context->DmaPoolBuffer == NULL)) {
        Context->VcmReserveStatus = (ULONG)STATUS_PROCEDURE_NOT_FOUND;
        goto Cleanup;
    }

    virtualAddress = NabuOwnGetDmaVirtualAddress(Context);
    /*
     * A WDF common buffer always describes one contiguous range, which is the
     * MDL shape the Qualcomm VCM wrapper accepts.  The pool fallback does not:
     * a 16 MB non-paged block becomes multi-range as soon as it crosses an
     * allocation section, and the wrapper rejects that MDL with
     * STATUS_INVALID_PARAMETER - the pad then stalls before any pixel lands.
     * Build the Mdl32 shape the stock CamX stack hands over instead: locked,
     * non-cached pages below 4 GB.  Those need no contiguous memory (the SMMU
     * maps the page list as one IOVA), so they are also what a pad that has
     * already spent its contiguous block on the other camera can still offer.
     */
#ifdef AVSTREAM_BUILD
    conversionAddress = virtualAddress;
    usePageList = TRUE;
#else
    usePageList = (Context->DmaCommonBuffer == NULL) ? TRUE : FALSE;
#endif
    if (usePageList) {
        PHYSICAL_ADDRESS lowAddress;
        PHYSICAL_ADDRESS highAddress;
        PHYSICAL_ADDRESS skipBytes;

        lowAddress.QuadPart = 0;
        highAddress.QuadPart = MAXULONG;
        skipBytes.QuadPart = 0;
        mdl = MmAllocatePagesForMdlEx(
            lowAddress,
            highAddress,
            skipBytes,
            Context->DmaBufferLength,
            MmNonCached,
            NABU_VCM_PAGE_LIST_FLAGS);
        if (mdl == NULL) {
            Context->VcmBackStatus = (ULONG)STATUS_INSUFFICIENT_RESOURCES;
            goto Cleanup;
        }
        mdlPagesAllocated = TRUE;
        if (MmGetMdlByteCount(mdl) < Context->DmaBufferLength) {
            Context->VcmBackStatus = (ULONG)STATUS_BUFFER_TOO_SMALL;
            goto Cleanup;
        }
        virtualAddress = MmMapLockedPagesSpecifyCache(
            mdl,
            KernelMode,
            MmWriteCombined,
            NULL,
            FALSE,
            NormalPagePriority | MdlMappingNoExecute);
        if (virtualAddress == NULL) {
            Context->VcmBackStatus = (ULONG)STATUS_INSUFFICIENT_RESOURCES;
            goto Cleanup;
        }
        mdlPagesMapped = TRUE;
        Context->DmaPoolFallbackUsed = 2;
        RtlFillMemory(
            virtualAddress, Context->DmaBufferLength, (UCHAR)0xAB);
        KeMemoryBarrier();
    } else {
        mdl = IoAllocateMdl(virtualAddress,
                            Context->DmaBufferLength,
                            FALSE, FALSE, NULL);
        if (mdl == NULL) {
            Context->VcmBackStatus = (ULONG)STATUS_INSUFFICIENT_RESOURCES;
            goto Cleanup;
        }
        MmBuildMdlForNonPagedPool(mdl);
    }
    physicalAddress = MmGetPhysicalAddress(virtualAddress);

    reserveBytes = ((ULONGLONG)Context->DmaBufferLength + 0x3fffULL) &
                   ~0x3fffULL;
    if (Context->VcmModeRequested >= 4) {
        Context->VcmGuardReserveAttempted = 1;
        Context->VcmPhase = 19;
        NabuOwnWriteDiagnostics(Context);
        guardReservation = vcmReserve(vcmRegion,
                                      NABU_VCM_GUARD_BYTES,
                                      NABU_VCM_RESERVE_FLAGS);
        Context->VcmGuardReserveStatus = guardReservation != NULL ?
            (ULONG)STATUS_SUCCESS : (ULONG)STATUS_UNSUCCESSFUL;
        if (guardReservation == NULL) {
            goto Cleanup;
        }
        guardAddress = vcmGetDevAddr(guardReservation);
        Context->VcmGuardAddressLow = (ULONG)guardAddress;
        Context->VcmGuardAddressHigh = (ULONG)(guardAddress >> 32);
        if (guardAddress == 1) {
            goto Cleanup;
        }
    }

    Context->VcmReserveAttempted = 1;
    Context->VcmPhase = 20;
    NabuOwnWriteDiagnostics(Context);
    reservation = vcmReserve(vcmRegion, reserveBytes, NABU_VCM_RESERVE_FLAGS);
    Context->VcmReserveStatus = reservation != NULL ?
        (ULONG)STATUS_SUCCESS : (ULONG)STATUS_UNSUCCESSFUL;
    if (reservation == NULL) {
        goto Cleanup;
    }

    Context->VcmBackAttempted = 1;
    Context->VcmPhase = 21;
    NabuOwnWriteDiagnostics(Context);
    KeFlushIoBuffers(mdl, TRUE, TRUE);
    status = vcmBack(reservation, mdl);
    Context->VcmBackStatus = (ULONG)status;
    if (!NT_SUCCESS(status)) {
        goto Cleanup;
    }
    backed = TRUE;

    Context->VcmGetDevAddrAttempted = 1;
    Context->VcmPhase = 22;
    NabuOwnWriteDiagnostics(Context);
    deviceAddress = vcmGetDevAddr(reservation);
    Context->VcmDeviceAddressLow = (ULONG)deviceAddress;
    Context->VcmDeviceAddressHigh = (ULONG)(deviceAddress >> 32);
    // The stock wrapper reserves value 1 for failure. Address 0 is valid for
    // the first allocation in a VCM region whose base is zero. Mode 4 occupies
    // that first slot with a guard, so the tested buffer must then be nonzero.
    if (deviceAddress == 1 ||
        (Context->VcmModeRequested >= 4 && deviceAddress == 0)) {
        goto Cleanup;
    }
    Context->VcmDeviceDiffersFromPhysical =
        (deviceAddress != (ULONGLONG)physicalAddress.QuadPart) ? 1UL : 0UL;
    Context->VcmCompleted = 1;
    if (Context->VcmModeRequested == 5) {
        NabuOwnRunRealCapture(Context, (PUCHAR)virtualAddress, deviceAddress);
#ifdef AVSTREAM_BUILD
        KeFlushIoBuffers(mdl, TRUE, TRUE);
        if (Context->RealCompleted != 0 && conversionAddress != NULL) {
            RtlCopyMemory(
                conversionAddress,
                virtualAddress,
                NABU_REAR_FRAME_BYTES);
            KeMemoryBarrier();
        }
#endif
    }

Cleanup:
    terminalPhase = Context->VcmPhase;
    if (backed && vcmUnback != NULL) {
        Context->VcmPhase = 80;
        NabuOwnWriteDiagnostics(Context);
        Context->VcmUnbackStatus = (ULONG)vcmUnback(reservation);
    }
    if (reservation != NULL && vcmRelease != NULL) {
        Context->VcmPhase = 81;
        NabuOwnWriteDiagnostics(Context);
        Context->VcmReleaseStatus = (ULONG)vcmRelease(reservation);
    }
    if (guardReservation != NULL && vcmRelease != NULL) {
        Context->VcmPhase = 81;
        NabuOwnWriteDiagnostics(Context);
        Context->VcmGuardReleaseStatus = (ULONG)vcmRelease(guardReservation);
    }
    if (mdl != NULL) {
        if (mdlPagesMapped) {
            MmUnmapLockedPages(virtualAddress, mdl);
            mdlPagesMapped = FALSE;
        }
        if (mdlPagesAllocated) {
            MmFreePagesFromMdl(mdl);
            ExFreePool(mdl);
            mdlPagesAllocated = FALSE;
        } else {
            IoFreeMdl(mdl);
        }
        mdl = NULL;
    }
    while (activated != 0 && vcmDeactivate != NULL) {
        activated -= 1;
        Context->VcmPhase = 82;
        NabuOwnWriteDiagnostics(Context);
        status = vcmDeactivate(associations[activated]);
        if (!NT_SUCCESS(status)) {
            Context->VcmDeactivateStatus = (ULONG)status;
        } else if (Context->VcmDeactivateStatus == (ULONG)STATUS_NOT_SUPPORTED) {
            Context->VcmDeactivateStatus = (ULONG)STATUS_SUCCESS;
        }
    }
    if (stateEnabled && setState != NULL) {
        Context->VcmPhase = 83;
        NabuOwnWriteDiagnostics(Context);
        Context->VcmSetStateOffStatus =
            (ULONG)setState(smmuInterface.Header.Context, 0);
    }
    if (legacyCallbackRegistered && legacyCallbackRemove != NULL) {
        legacyCallbackRemove(
            smmuInterface.Header.Context,
            NABU_SMMU_CONFIG_SELECTOR);
        legacyCallbackRegistered = FALSE;
    }
    while (associated != 0 && vcmDeassoc != NULL) {
        associated -= 1;
        Context->VcmPhase = 84;
        NabuOwnWriteDiagnostics(Context);
        status = vcmDeassoc(associations[associated]);
        if (!NT_SUCCESS(status)) {
            Context->VcmDeassocStatus = (ULONG)status;
        } else if (Context->VcmDeassocStatus == (ULONG)STATUS_NOT_SUPPORTED) {
            Context->VcmDeassocStatus = (ULONG)STATUS_SUCCESS;
        }
    }
    if (vcmRegion != NULL && vcmFree != NULL) {
        Context->VcmPhase = 85;
        NabuOwnWriteDiagnostics(Context);
        Context->VcmFreeStatus = (ULONG)vcmFree(vcmRegion);
    }
    if (smmuInterface.Header.InterfaceDereference != NULL) {
        smmuInterface.Header.InterfaceDereference(smmuInterface.Header.Context);
    }
    if (fileObject != NULL) {
        ObDereferenceObject(fileObject);
    }
    if (interfaceList != NULL) {
        ExFreePool(interfaceList);
    }

    Context->VcmPhase = Context->VcmCompleted ? 100UL : terminalPhase;
    NabuOwnWriteDiagnostics(Context);
}

static BOOLEAN
NabuOwnBuildIndexedName(
    _Out_writes_(NameChars) WCHAR *Name,
    _In_ SIZE_T NameChars,
    _In_z_ const WCHAR *Prefix,
    _In_ ULONG Index,
    _In_z_ const WCHAR *Suffix)
{
    SIZE_T position = 0;

    if (Name == NULL || NameChars == 0 || Prefix == NULL ||
        Suffix == NULL || Index > 99UL) {
        return FALSE;
    }
    while (*Prefix != L'\0') {
        if (position + 1 >= NameChars) {
            return FALSE;
        }
        Name[position++] = *Prefix++;
    }
    if (position + 2 >= NameChars) {
        return FALSE;
    }
    Name[position++] = (WCHAR)(L'0' + ((Index / 10UL) % 10UL));
    Name[position++] = (WCHAR)(L'0' + (Index % 10UL));
    while (*Suffix != L'\0') {
        if (position + 1 >= NameChars) {
            return FALSE;
        }
        Name[position++] = *Suffix++;
    }
    Name[position] = L'\0';
    return TRUE;
}

VOID
NabuOwnWriteDiagnostics(_In_ PNABU_OWN_STAGE23_CONTEXT Context)
{
#ifdef AVSTREAM_BUILD
    //
    // WdfDeviceMiniportCreate handles cannot be passed to
    // WdfDeviceOpenRegistryKey.  AVStream owns PnP in this build, so keep
    // diagnostics in memory and do not touch the registry from the backend.
    //
    UNREFERENCED_PARAMETER(Context);
    return;
#else
    WDFKEY key = NULL;
    ULONG i;
    WCHAR wordName[32];

    if (!NT_SUCCESS(WdfDeviceOpenRegistryKey(Context->Device,
                                              PLUGPLAY_REGKEY_DEVICE,
                                              KEY_SET_VALUE,
                                              WDF_NO_OBJECT_ATTRIBUTES,
                                              &key))) {
        return;
    }

    NabuOwnWriteValue(key, L"NabuOwn_BuildTag", NABU_OWN_STAGE39_BUILD_TAG);
    NabuOwnWriteValue(key, L"NabuOwn_Stage", 39);

    // Values below belonged to pre-Stage-19 experiments.  Remove them so a
    // NabuOwn_Cci* query can never make old state look like a Stage 20 result.
    NabuOwnRemoveValue(key, L"NabuOwn_CciMapSucceeded");
    NabuOwnRemoveValue(key, L"NabuOwn_CciHwVersion");
    NabuOwnRemoveValue(key, L"NabuOwn_CciClockSettleDelayMs");
    NabuOwnRemoveValue(key, L"NabuOwn_CciSourceConfigureAttempted");
    NabuOwnRemoveValue(key, L"NabuOwn_CciSourceCmdBefore");
    NabuOwnRemoveValue(key, L"NabuOwn_CciSourceCfgBefore");
    NabuOwnRemoveValue(key, L"NabuOwn_CciSourceCmdAfter");
    NabuOwnRemoveValue(key, L"NabuOwn_CciSourceCfgAfter");
    NabuOwnRemoveValue(key, L"NabuOwn_CciSourceUpdateSucceeded");
    NabuOwnWriteValue(key, L"NabuOwn_HardwareTouched", Context->HardwareTouched);
    NabuOwnWriteValue(key, L"NabuOwn_PrepareCount", Context->PrepareCount);
    NabuOwnWriteValue(key, L"NabuOwn_D0EntryCount", Context->D0EntryCount);
    NabuOwnWriteValue(key, L"NabuOwn_ArmRequested", Context->ArmRequested);
    NabuOwnWriteValue(key, L"NabuOwn_SafetyRejectedArm", Context->SafetyRejectedArm);
    NabuOwnWriteValue(key, L"NabuOwn_DmaEnablerCreateStatus", Context->DmaEnablerCreateStatus);
    NabuOwnWriteValue(key, L"NabuOwn_DmaBufferAttempted", Context->DmaBufferAttempted);
    NabuOwnWriteValue(key, L"NabuOwn_DmaBufferCreateStatus", Context->DmaBufferCreateStatus);
    NabuOwnWriteValue(key, L"NabuOwn_DmaCommonBufferCreateStatus", Context->DmaCommonBufferCreateStatus);
    NabuOwnWriteValue(key, L"NabuOwn_DmaPoolFallbackUsed", Context->DmaPoolFallbackUsed);
    NabuOwnWriteValue(key, L"NabuOwn_DmaBufferReleased", Context->DmaBufferReleased);
    NabuOwnWriteValue(key, L"NabuOwn_DmaBufferAlreadyPresent", Context->DmaBufferAlreadyPresent);
    NabuOwnWriteValue(key, L"NabuOwn_DmaPhase", Context->DmaPhase);
    NabuOwnWriteValue(key, L"NabuOwn_DmaBufferLength", Context->DmaBufferLength);
    NabuOwnWriteValue(key, L"NabuOwn_DmaLogicalAddressLow", Context->DmaLogicalAddressLow);
    NabuOwnWriteValue(key, L"NabuOwn_DmaLogicalAddressHigh", Context->DmaLogicalAddressHigh);
    NabuOwnWriteValue(key, L"NabuOwn_DmaVirtualAddressLow", Context->DmaVirtualAddressLow);
    NabuOwnWriteValue(key, L"NabuOwn_DmaPhysicalAddressLow", Context->DmaPhysicalAddressLow);
    NabuOwnWriteValue(key, L"NabuOwn_DmaPhysicalAddressHigh", Context->DmaPhysicalAddressHigh);
    NabuOwnWriteValue(key, L"NabuOwn_DmaLogicalDiffersFromPhysical", Context->DmaLogicalDiffersFromPhysical);
    NabuOwnWriteValue(key, L"NabuOwn_FrameCachedScratchStatus", Context->FrameCachedScratchStatus);
    NabuOwnWriteValue(key, L"NabuOwn_FrameCachedScratchUsed", Context->FrameCachedScratchUsed);
    NabuOwnWriteValue(key, L"NabuOwn_CycleCount", Context->CycleCount);
    NabuOwnWriteValue(key, L"NabuOwn_PublishUsLast", Context->PublishUsLast);
    NabuOwnWriteValue(key, L"NabuOwn_PublishUsMax", Context->PublishUsMax);
    NabuOwnWriteValue(key, L"NabuOwn_PublishUsPerCycle", Context->CycleCount != 0 ? Context->PublishUsTotal / Context->CycleCount : 0UL);
    NabuOwnWriteValue(key, L"NabuOwn_WaitUsLast", Context->WaitUsLast);
    NabuOwnWriteValue(key, L"NabuOwn_WaitUsMax", Context->WaitUsMax);
    NabuOwnWriteValue(key, L"NabuOwn_WaitUsPerCycle", Context->CycleCount != 0 ? Context->WaitUsTotal / Context->CycleCount : 0UL);
    NabuOwnWriteValue(key, L"NabuOwn_FramePeriodUsLast", Context->FramePeriodUsLast);
    NabuOwnWriteValue(key, L"NabuOwn_FramePeriodUsMin", Context->FramePeriodUsMin);
    NabuOwnWriteValue(key, L"NabuOwn_FramePeriodUsMax", Context->FramePeriodUsMax);
    NabuOwnWriteValue(key, L"NabuOwn_WaitSleepPolls", Context->WaitSleepPolls);
    NabuOwnWriteValue(key, L"NabuOwn_WaitStallPolls", Context->WaitStallPolls);
    NabuOwnWriteValue(key, L"NabuOwn_TimerResolution100Ns", Context->TimerResolution100Ns);

    NabuOwnWriteValue(key, L"NabuOwn_VcmModeRequested", Context->VcmModeRequested);
    NabuOwnWriteValue(key, L"NabuOwn_VcmPhase", Context->VcmPhase);
    NabuOwnWriteValue(key, L"NabuOwn_SmmuEnumerateStatus", Context->SmmuEnumerateStatus);
    NabuOwnWriteValue(key, L"NabuOwn_SmmuDeviceFound", Context->SmmuDeviceFound);
    NabuOwnWriteValue(key, L"NabuOwn_SmmuOpenStatus", Context->SmmuOpenStatus);
    NabuOwnWriteValue(key, L"NabuOwn_SmmuQueryStatus", Context->SmmuQueryStatus);
    NabuOwnWriteValue(key, L"NabuOwn_SmmuInterfaceSize", Context->SmmuInterfaceSize);
    NabuOwnWriteValue(key, L"NabuOwn_SmmuInterfaceVersion", Context->SmmuInterfaceVersion);
    NabuOwnWriteValue(key, L"NabuOwn_SmmuGetConfigStatus", Context->SmmuGetConfigStatus);
    NabuOwnWriteValue(key, L"NabuOwn_SmmuConfigSelector", Context->SmmuConfigSelector);
    NabuOwnWriteValue(key, L"NabuOwn_SmmuContextCountRaw", Context->SmmuContextCountRaw);
    NabuOwnWriteValue(key, L"NabuOwn_SmmuContextCountAccepted", Context->SmmuContextCountAccepted);
    for (i = 0; i < NABU_SMMU_MAX_CONTEXT_BANKS; ++i) {
        if (NabuOwnBuildIndexedName(wordName,
                                    RTL_NUMBER_OF(wordName),
                                    L"NabuOwn_SmmuCb", i, L"Index")) {
            NabuOwnWriteValue(key, wordName, Context->SmmuContextIndex[i]);
        }
        if (NabuOwnBuildIndexedName(wordName,
                                    RTL_NUMBER_OF(wordName),
                                    L"NabuOwn_SmmuCb", i, L"HandleLo")) {
            NabuOwnWriteValue(key, wordName, Context->SmmuContextHandleLow[i]);
        }
        if (NabuOwnBuildIndexedName(wordName,
                                    RTL_NUMBER_OF(wordName),
                                    L"NabuOwn_SmmuCb", i, L"HandleHi")) {
            NabuOwnWriteValue(key, wordName, Context->SmmuContextHandleHigh[i]);
        }
    }
    NabuOwnWriteValue(key, L"NabuOwn_VcmCreateAttempted", Context->VcmCreateAttempted);
    NabuOwnWriteValue(key, L"NabuOwn_VcmCreateStatus", Context->VcmCreateStatus);
    NabuOwnWriteValue(key, L"NabuOwn_VcmRegionHandleLow", Context->VcmRegionHandleLow);
    NabuOwnWriteValue(key, L"NabuOwn_VcmAssociationAttempted", Context->VcmAssociationAttempted);
    NabuOwnWriteValue(key, L"NabuOwn_VcmAssociationCount", Context->VcmAssociationCount);
    NabuOwnWriteValue(key, L"NabuOwn_VcmAssociationFailIndex", Context->VcmAssociationFailIndex);
    NabuOwnWriteValue(key, L"NabuOwn_VcmSetStateStatus", Context->VcmSetStateStatus);
    NabuOwnWriteValue(key, L"NabuOwn_VcmActivationCount", Context->VcmActivationCount);
    NabuOwnWriteValue(key, L"NabuOwn_VcmActivationFailIndex", Context->VcmActivationFailIndex);
    NabuOwnWriteValue(key, L"NabuOwn_VcmReserveAttempted", Context->VcmReserveAttempted);
    NabuOwnWriteValue(key, L"NabuOwn_VcmReserveStatus", Context->VcmReserveStatus);
    NabuOwnWriteValue(key, L"NabuOwn_VcmGuardReserveAttempted", Context->VcmGuardReserveAttempted);
    NabuOwnWriteValue(key, L"NabuOwn_VcmGuardReserveStatus", Context->VcmGuardReserveStatus);
    NabuOwnWriteValue(key, L"NabuOwn_VcmGuardAddressLow", Context->VcmGuardAddressLow);
    NabuOwnWriteValue(key, L"NabuOwn_VcmGuardAddressHigh", Context->VcmGuardAddressHigh);
    NabuOwnWriteValue(key, L"NabuOwn_VcmGuardReleaseStatus", Context->VcmGuardReleaseStatus);
    NabuOwnWriteValue(key, L"NabuOwn_VcmBackAttempted", Context->VcmBackAttempted);
    NabuOwnWriteValue(key, L"NabuOwn_VcmBackStatus", Context->VcmBackStatus);
    NabuOwnWriteValue(key, L"NabuOwn_VcmGetDevAddrAttempted", Context->VcmGetDevAddrAttempted);
    NabuOwnWriteValue(key, L"NabuOwn_VcmDeviceAddressLow", Context->VcmDeviceAddressLow);
    NabuOwnWriteValue(key, L"NabuOwn_VcmDeviceAddressHigh", Context->VcmDeviceAddressHigh);
    NabuOwnWriteValue(key, L"NabuOwn_VcmDeviceDiffersFromPhysical", Context->VcmDeviceDiffersFromPhysical);
    NabuOwnWriteValue(key, L"NabuOwn_VcmUnbackStatus", Context->VcmUnbackStatus);
    NabuOwnWriteValue(key, L"NabuOwn_VcmReleaseStatus", Context->VcmReleaseStatus);
    NabuOwnWriteValue(key, L"NabuOwn_VcmDeactivateStatus", Context->VcmDeactivateStatus);
    NabuOwnWriteValue(key, L"NabuOwn_VcmSetStateOffStatus", Context->VcmSetStateOffStatus);
    NabuOwnWriteValue(key, L"NabuOwn_VcmDeassocStatus", Context->VcmDeassocStatus);
    NabuOwnWriteValue(key, L"NabuOwn_VcmFreeStatus", Context->VcmFreeStatus);
    NabuOwnWriteValue(key, L"NabuOwn_VcmCompleted", Context->VcmCompleted);
    NabuOwnWriteValue(key, L"NabuOwn_TpgPhase", Context->TpgPhase);
    NabuOwnWriteValue(key, L"NabuOwn_TpgAttempted", Context->TpgAttempted);
    NabuOwnWriteValue(key, L"NabuOwn_TpgCompleted", Context->TpgCompleted);
    NabuOwnWriteValue(key, L"NabuOwn_TpgPlatformOpenStatus", Context->TpgPlatformOpenStatus);
    NabuOwnWriteValue(key, L"NabuOwn_TpgPlatformAcquireStatus", Context->TpgPlatformAcquireStatus);
    NabuOwnWriteValue(key, L"NabuOwn_TpgPlatformPowerStatus", Context->TpgPlatformPowerStatus);
    NabuOwnWriteValue(key, L"NabuOwn_TpgPlatformReleaseStatus", Context->TpgPlatformReleaseStatus);
    NabuOwnWriteValue(key, L"NabuOwn_TpgRtBandwidthStatus", Context->TpgRtBandwidthStatus);
    NabuOwnWriteValue(key, L"NabuOwn_TpgRtBandwidthReleaseStatus", Context->TpgRtBandwidthReleaseStatus);
    NabuOwnWriteValue(key, L"NabuOwn_TpgIfeGdscBefore", Context->TpgIfeGdscBefore);
    NabuOwnWriteValue(key, L"NabuOwn_TpgIfeGdscAfter", Context->TpgIfeGdscAfter);
    NabuOwnWriteValue(key, L"NabuOwn_TpgIfeGdscReady", Context->TpgIfeGdscReady);
    NabuOwnWriteValue(key, L"NabuOwn_TpgIfeClockCbcr", Context->TpgIfeClockCbcr);
    NabuOwnWriteValue(key, L"NabuOwn_TpgIfeAxiCbcr", Context->TpgIfeAxiCbcr);
    NabuOwnWriteValue(key, L"NabuOwn_TpgIfeCsidCbcr", Context->TpgIfeCsidCbcr);
    NabuOwnWriteValue(key, L"NabuOwn_TpgCamnocAxiCbcr", Context->TpgCamnocAxiCbcr);
    NabuOwnWriteValue(key, L"NabuOwn_TpgVfeResetStatus", Context->TpgVfeResetStatus);
    NabuOwnWriteValue(key, L"NabuOwn_TpgVfeResetPolls", Context->TpgVfeResetPolls);
    NabuOwnWriteValue(key, L"NabuOwn_TpgIovaLow", Context->TpgIovaLow);
    NabuOwnWriteValue(key, L"NabuOwn_TpgIovaHigh", Context->TpgIovaHigh);
    NabuOwnWriteValue(key, L"NabuOwn_TpgWmCfg", Context->TpgWmCfg);
    NabuOwnWriteValue(key, L"NabuOwn_TpgWmImageBefore", Context->TpgWmImageBefore);
    NabuOwnWriteValue(key, L"NabuOwn_TpgWmImageAfter", Context->TpgWmImageAfter);
    NabuOwnWriteValue(key, L"NabuOwn_TpgVfeBusIrq0", Context->TpgVfeBusIrq0);
    NabuOwnWriteValue(key, L"NabuOwn_TpgVfeBusIrq1", Context->TpgVfeBusIrq1);
    NabuOwnWriteValue(key, L"NabuOwn_TpgVfeBusIrq2", Context->TpgVfeBusIrq2);
    NabuOwnWriteValue(key, L"NabuOwn_TpgVfeBusDebug0", Context->TpgVfeBusDebug0);
    NabuOwnWriteValue(key, L"NabuOwn_TpgCsidRdiIrq", Context->TpgCsidRdiIrq);
    NabuOwnWriteValue(key, L"NabuOwn_TpgCsidTotalPackets", Context->TpgCsidTotalPackets);
    NabuOwnWriteValue(key, L"NabuOwn_TpgBufferChanged", Context->TpgBufferChanged);
    NabuOwnWriteValue(key, L"NabuOwn_TpgBufferSample0", Context->TpgBufferSample0);
    NabuOwnWriteValue(key, L"NabuOwn_TpgBufferSample1", Context->TpgBufferSample1);
    NabuOwnWriteValue(key, L"NabuOwn_TpgBufferSample2", Context->TpgBufferSample2);
    NabuOwnWriteValue(key, L"NabuOwn_TpgBufferSample3", Context->TpgBufferSample3);
    NabuOwnWriteValue(key, L"NabuOwn_TpgFileStatus", Context->TpgFileStatus);
    NabuOwnWriteValue(key, L"NabuOwn_TpgFileBytes", Context->TpgFileBytes);

    NabuOwnWriteValue(key, L"NabuOwn_RealPhase", Context->RealPhase);
    NabuOwnWriteValue(key, L"NabuOwn_RealAttempted", Context->RealAttempted);
    NabuOwnWriteValue(key, L"NabuOwn_RealCompleted", Context->RealCompleted);
    NabuOwnWriteValue(key, L"NabuOwn_RealPlatformOpenStatus", Context->RealPlatformOpenStatus);
    NabuOwnWriteValue(key, L"NabuOwn_RealAcquire1Status", Context->RealAcquire1Status);
    NabuOwnWriteValue(key, L"NabuOwn_RealPlatformPowerStatus", Context->RealPlatformPowerStatus);
    NabuOwnWriteValue(key, L"NabuOwn_RealPlatformPinStatus", Context->RealPlatformPinStatus);
    NabuOwnWriteValue(key, L"NabuOwn_RealInitMasterStatus", Context->RealInitMasterStatus);
    NabuOwnWriteValue(key, L"NabuOwn_RealAcquire2Status", Context->RealAcquire2Status);
    NabuOwnWriteValue(key, L"NabuOwn_RealDeinitMasterStatus", Context->RealDeinitMasterStatus);
    NabuOwnWriteValue(key, L"NabuOwn_RealRelease1Status", Context->RealRelease1Status);
    NabuOwnWriteValue(key, L"NabuOwn_RealRelease2Status", Context->RealRelease2Status);
    NabuOwnWriteValue(key, L"NabuOwn_RealRtBandwidthStatus", Context->RealRtBandwidthStatus);
    NabuOwnWriteValue(key, L"NabuOwn_RealRtBandwidthReleaseStatus", Context->RealRtBandwidthReleaseStatus);
    NabuOwnWriteValue(key, L"NabuOwn_RealPll0LockOk", Context->RealPll0LockOk);
    NabuOwnWriteValue(key, L"NabuOwn_RealPll3ModeBefore", Context->RealPll3ModeBefore);
    NabuOwnWriteValue(key, L"NabuOwn_RealPll3LBefore", Context->RealPll3LBefore);
    NabuOwnWriteValue(key, L"NabuOwn_RealPll3AlphaBefore", Context->RealPll3AlphaBefore);
    NabuOwnWriteValue(key, L"NabuOwn_RealPll3UserBefore", Context->RealPll3UserBefore);
    NabuOwnWriteValue(key, L"NabuOwn_RealPll3OpmodeBefore", Context->RealPll3OpmodeBefore);
    NabuOwnWriteValue(key, L"NabuOwn_RealPll3ModeAfter", Context->RealPll3ModeAfter);
    NabuOwnWriteValue(key, L"NabuOwn_RealPll3LAfter", Context->RealPll3LAfter);
    NabuOwnWriteValue(key, L"NabuOwn_RealPll3AlphaAfter", Context->RealPll3AlphaAfter);
    NabuOwnWriteValue(key, L"NabuOwn_RealPll3UserAfter", Context->RealPll3UserAfter);
    NabuOwnWriteValue(key, L"NabuOwn_RealPll3OpmodeAfter", Context->RealPll3OpmodeAfter);
    NabuOwnWriteValue(key, L"NabuOwn_RealPll3LockOk", Context->RealPll3LockOk);
    NabuOwnWriteValue(key, L"NabuOwn_RealIfeClkCmdAfter", Context->RealIfeClkCmdAfter);
    NabuOwnWriteValue(key, L"NabuOwn_RealIfeClkCfgAfter", Context->RealIfeClkCfgAfter);
    NabuOwnWriteValue(key, L"NabuOwn_RealIfeClkCbcrAfter", Context->RealIfeClkCbcrAfter);
    NabuOwnWriteValue(key, L"NabuOwn_RealMclk0Cmd", Context->RealMclk0Cmd);
    NabuOwnWriteValue(key, L"NabuOwn_RealMclk0Cfg", Context->RealMclk0Cfg);
    NabuOwnWriteValue(key, L"NabuOwn_RealMclk0Cbcr", Context->RealMclk0Cbcr);
    NabuOwnWriteValue(key, L"NabuOwn_RealClockTreeReady", Context->RealClockTreeReady);
    NabuOwnWriteValue(key, L"NabuOwn_RealPowerRailsAttempted", Context->RealPowerRailsAttempted);
    NabuOwnWriteValue(key, L"NabuOwn_RealPowerRailsReady", Context->RealPowerRailsReady);
    NabuOwnWriteValue(key, L"NabuOwn_RealSpmiInitStatus", Context->RealSpmiInitStatus);
    NabuOwnWriteValue(key, L"NabuOwn_RealSpmiHwVersion", Context->RealSpmiHwVersion);
    NabuOwnWriteValue(key, L"NabuOwn_RealSpmiApidCount", Context->RealSpmiApidCount);
    NabuOwnWriteValue(key, L"NabuOwn_RealVioFindStatus", Context->RealVioFindStatus);
    NabuOwnWriteValue(key, L"NabuOwn_RealVioApid", Context->RealVioApid);
    NabuOwnWriteValue(key, L"NabuOwn_RealVioWriteEe", Context->RealVioWriteEe);
    NabuOwnWriteValue(key, L"NabuOwn_RealVioType", Context->RealVioType);
    NabuOwnWriteValue(key, L"NabuOwn_RealVioVsetMv", Context->RealVioVsetMv);
    NabuOwnWriteValue(key, L"NabuOwn_RealVioEnBefore", Context->RealVioEnBefore);
    NabuOwnWriteValue(key, L"NabuOwn_RealVioEnAfter", Context->RealVioEnAfter);
    NabuOwnWriteValue(key, L"NabuOwn_RealVioStatusBefore", Context->RealVioStatusBefore);
    NabuOwnWriteValue(key, L"NabuOwn_RealVioStatusAfter", Context->RealVioStatusAfter);
    NabuOwnWriteValue(key, L"NabuOwn_RealVioWriteStatus", Context->RealVioWriteStatus);
    NabuOwnWriteValue(key, L"NabuOwn_RealVdigFindStatus", Context->RealVdigFindStatus);
    NabuOwnWriteValue(key, L"NabuOwn_RealVdigApid", Context->RealVdigApid);
    NabuOwnWriteValue(key, L"NabuOwn_RealVdigWriteEe", Context->RealVdigWriteEe);
    NabuOwnWriteValue(key, L"NabuOwn_RealVdigType", Context->RealVdigType);
    NabuOwnWriteValue(key, L"NabuOwn_RealVdigSubtype", Context->RealVdigSubtype);
    NabuOwnWriteValue(key, L"NabuOwn_RealVdigModeBefore", Context->RealVdigModeBefore);
    NabuOwnWriteValue(key, L"NabuOwn_RealVdigModeAfter", Context->RealVdigModeAfter);
    NabuOwnWriteValue(key, L"NabuOwn_RealVdigOutBefore", Context->RealVdigOutBefore);
    NabuOwnWriteValue(key, L"NabuOwn_RealVdigOutAfter", Context->RealVdigOutAfter);
    NabuOwnWriteValue(key, L"NabuOwn_RealVdigEnBefore", Context->RealVdigEnBefore);
    NabuOwnWriteValue(key, L"NabuOwn_RealVdigEnAfter", Context->RealVdigEnAfter);
    NabuOwnWriteValue(key, L"NabuOwn_RealVdigModeWriteStatus", Context->RealVdigModeWriteStatus);
    NabuOwnWriteValue(key, L"NabuOwn_RealVdigOutWriteStatus", Context->RealVdigOutWriteStatus);
    NabuOwnWriteValue(key, L"NabuOwn_RealVdigEnWriteStatus", Context->RealVdigEnWriteStatus);
    NabuOwnWriteValue(key, L"NabuOwn_RealBobFindStatus", Context->RealBobFindStatus);
    NabuOwnWriteValue(key, L"NabuOwn_RealBobApid", Context->RealBobApid);
    NabuOwnWriteValue(key, L"NabuOwn_RealBobWriteEe", Context->RealBobWriteEe);
    NabuOwnWriteValue(key, L"NabuOwn_RealBobType", Context->RealBobType);
    NabuOwnWriteValue(key, L"NabuOwn_RealBobEn", Context->RealBobEn);
    NabuOwnWriteValue(key, L"NabuOwn_RealBobStatus", Context->RealBobStatus);
    NabuOwnWriteValue(key, L"NabuOwn_RealChipIdStatus", Context->RealChipIdStatus);
    NabuOwnWriteValue(key, L"NabuOwn_RealChipId", Context->RealChipId);
    NabuOwnWriteValue(key, L"NabuOwn_RealInitStatus", Context->RealInitStatus);
    NabuOwnWriteValue(key, L"NabuOwn_RealInitFailIndex", Context->RealInitFailIndex);
    NabuOwnWriteValue(key, L"NabuOwn_RealInitFailReg", Context->RealInitFailReg);
    NabuOwnWriteValue(key, L"NabuOwn_RealModeStatus", Context->RealModeStatus);
    NabuOwnWriteValue(key, L"NabuOwn_RealModeFailIndex", Context->RealModeFailIndex);
    NabuOwnWriteValue(key, L"NabuOwn_RealModeFailReg", Context->RealModeFailReg);
    NabuOwnWriteValue(key, L"NabuOwn_RealStreamWriteStatus", Context->RealStreamWriteStatus);
    NabuOwnWriteValue(key, L"NabuOwn_RealStreamReadStatus", Context->RealStreamReadStatus);
    NabuOwnWriteValue(key, L"NabuOwn_RealStreamReadback", Context->RealStreamReadback);
    NabuOwnWriteValue(key, L"NabuOwn_RealStreamStopStatus", Context->RealStreamStopStatus);
    NabuOwnWriteValue(key, L"NabuOwn_RealCsiphyHwVersion", Context->RealCsiphyHwVersion);
    NabuOwnWriteValue(key, L"NabuOwn_RealCsiphyIrq5", Context->RealCsiphyIrq5);
    NabuOwnWriteValue(key, L"NabuOwn_RealCsiphyIrq8", Context->RealCsiphyIrq8);
    NabuOwnWriteValue(key, L"NabuOwn_RealCsidRxCfg0", Context->RealCsidRxCfg0);
    NabuOwnWriteValue(key, L"NabuOwn_RealCsidRxCfg1", Context->RealCsidRxCfg1);
    NabuOwnWriteValue(key, L"NabuOwn_RealCsidRdiCfg0", Context->RealCsidRdiCfg0);
    NabuOwnWriteValue(key, L"NabuOwn_RealCsidRdiIrq", Context->RealCsidRdiIrq);
    NabuOwnWriteValue(key, L"NabuOwn_RealCsidTotalPackets", Context->RealCsidTotalPackets);
    NabuOwnWriteValue(key, L"NabuOwn_RealWaitPolls", Context->RealWaitPolls);
    NabuOwnWriteValue(key, L"NabuOwn_RealWaitExitReason", Context->RealWaitExitReason);
    NabuOwnWriteValue(key, L"NabuOwn_RealWaitPacketsFirst", Context->RealWaitPacketsFirst);
    NabuOwnWriteValue(key, L"NabuOwn_RealWaitPacketsLast", Context->RealWaitPacketsLast);
    NabuOwnWriteValue(key, L"NabuOwn_RealWaitCsidIrqLast", Context->RealWaitCsidIrqLast);
    NabuOwnWriteValue(key, L"NabuOwn_RealWaitVfeTopIrqLast", Context->RealWaitVfeTopIrqLast);
    NabuOwnWriteValue(key, L"NabuOwn_RealWaitBusIrqLast", Context->RealWaitBusIrqLast);
    NabuOwnWriteValue(key, L"NabuOwn_RealIovaLow", Context->RealIovaLow);
    NabuOwnWriteValue(key, L"NabuOwn_RealIovaHigh", Context->RealIovaHigh);
    NabuOwnWriteValue(key, L"NabuOwn_RealWmCfg", Context->RealWmCfg);
    NabuOwnWriteValue(key, L"NabuOwn_RealWmStatus0", Context->RealWmStatus0);
    NabuOwnWriteValue(key, L"NabuOwn_RealWmStatus1", Context->RealWmStatus1);
    NabuOwnWriteValue(key, L"NabuOwn_RealWmImageBefore", Context->RealWmImageBefore);
    NabuOwnWriteValue(key, L"NabuOwn_RealWmImageAfter", Context->RealWmImageAfter);
    NabuOwnWriteValue(key, L"NabuOwn_RealVfeTopIrq0", Context->RealVfeTopIrq0);
    NabuOwnWriteValue(key, L"NabuOwn_RealVfeTopIrq1", Context->RealVfeTopIrq1);
    NabuOwnWriteValue(key, L"NabuOwn_RealVfeBusIrq0", Context->RealVfeBusIrq0);
    NabuOwnWriteValue(key, L"NabuOwn_RealVfeBusIrq1", Context->RealVfeBusIrq1);
    NabuOwnWriteValue(key, L"NabuOwn_RealVfeBusIrq2", Context->RealVfeBusIrq2);
    NabuOwnWriteValue(key, L"NabuOwn_RealVfeBusDebug0", Context->RealVfeBusDebug0);
    NabuOwnWriteValue(key, L"NabuOwn_RealBufferChanged", Context->RealBufferChanged);
    NabuOwnWriteValue(key, L"NabuOwn_RealBufferSample0", Context->RealBufferSample0);
    NabuOwnWriteValue(key, L"NabuOwn_RealBufferSample1", Context->RealBufferSample1);
    NabuOwnWriteValue(key, L"NabuOwn_RealBufferSample2", Context->RealBufferSample2);
    NabuOwnWriteValue(key, L"NabuOwn_RealBufferSample3", Context->RealBufferSample3);
    NabuOwnWriteValue(key, L"NabuOwn_RealFrameBytesExpected", Context->RealFrameBytesExpected);
    NabuOwnWriteValue(key, L"NabuOwn_RealBufferTailChanged", Context->RealBufferTailChanged);
    NabuOwnWriteValue(key, L"NabuOwn_RealBufferTailSample0", Context->RealBufferTailSample0);
    NabuOwnWriteValue(key, L"NabuOwn_RealBufferTailSample1", Context->RealBufferTailSample1);
    NabuOwnWriteValue(key, L"NabuOwn_RealFileStatus", Context->RealFileStatus);
    NabuOwnWriteValue(key, L"NabuOwn_RealFileBytes", Context->RealFileBytes);

    NabuOwnWriteValue(key, L"NabuOwn_ScfgAttempted", Context->ScfgAttempted);
    NabuOwnWriteValue(key, L"NabuOwn_ScfgStatus", Context->ScfgStatus);
    NabuOwnWriteValue(key, L"NabuOwn_ScfgBytesReturned", Context->ScfgBytesReturned);
    NabuOwnWriteValue(key, L"NabuOwn_ScfgOutputSignature", Context->ScfgOutputSignature);
    NabuOwnWriteValue(key, L"NabuOwn_ScfgOutputLength", Context->ScfgOutputLength);
    NabuOwnWriteValue(key, L"NabuOwn_ScfgOutputCount", Context->ScfgOutputCount);
    NabuOwnWriteValue(key, L"NabuOwn_ScfgPackageType", Context->ScfgPackageType);
    NabuOwnWriteValue(key, L"NabuOwn_ScfgPackageDataLength", Context->ScfgPackageDataLength);
    NabuOwnWriteValue(key, L"NabuOwn_ScfgTailByte0", Context->ScfgTailByte0);
    NabuOwnWriteValue(key, L"NabuOwn_ScfgTailByte1", Context->ScfgTailByte1);
    NabuOwnWriteValue(key, L"NabuOwn_ScfgConfig0", Context->ScfgConfig0);
    NabuOwnWriteValue(key, L"NabuOwn_ScfgConfig1", Context->ScfgConfig1);
    NabuOwnWriteValue(key, L"NabuOwn_ScfgConfig2", Context->ScfgConfig2);
    for (i = 0; i < NABU_OWN_SCFG_WORD_COUNT; ++i) {
        if (NabuOwnBuildIndexedName(wordName,
                                    RTL_NUMBER_OF(wordName),
                                    L"NabuOwn_ScfgWord", i, L"")) {
            NabuOwnWriteValue(key, wordName, Context->ScfgWords[i]);
        }
    }

    NabuOwnWriteValue(key, L"NabuOwn_CciConfigValid", Context->CciConfigValid);
    NabuOwnWriteValue(key, L"NabuOwn_CciMaster", Context->CciMaster);
    NabuOwnWriteValue(key, L"NabuOwn_CciFrequencyKHz", Context->CciFrequencyKHz);
    NabuOwnWriteValue(key, L"NabuOwn_CciSlaveAddress", Context->CciSlaveAddress);
    NabuOwnWriteValue(key, L"NabuOwn_CciAddressType", Context->CciAddressType);
    NabuOwnWriteValue(key, L"NabuOwn_CciDataLength", Context->CciDataLength);
    NabuOwnWriteValue(key, L"NabuOwn_CciRegister", Context->CciRegister);
    NabuOwnWriteValue(key, L"NabuOwn_CciExpectedId", Context->CciExpectedId);
    NabuOwnWriteValue(key, L"NabuOwn_CciAttempted", Context->CciAttempted);
    NabuOwnWriteValue(key, L"NabuOwn_CciGuardRejected", Context->CciGuardRejected);
    NabuOwnWriteValue(key, L"NabuOwn_CciPhase", Context->CciPhase);
    NabuOwnWriteValue(key, L"NabuOwn_CciPlatformOpenStatus", Context->CciPlatformOpenStatus);
    NabuOwnWriteValue(key, L"NabuOwn_CciAcquire1Status", Context->CciAcquire1Status);
    NabuOwnWriteValue(key, L"NabuOwn_CciInitMasterStatus", Context->CciInitMasterStatus);
    NabuOwnWriteValue(key, L"NabuOwn_CciAcquire2Status", Context->CciAcquire2Status);
    NabuOwnWriteValue(key, L"NabuOwn_CciStopIdleStatus", Context->CciStopIdleStatus);
    NabuOwnWriteValue(key, L"NabuOwn_CciStopIdleSkippedForD0", Context->CciStopIdleSkippedForD0);
    NabuOwnWriteValue(key, L"NabuOwn_CciD0EntryConfirmed", Context->CciD0EntryConfirmed);
    NabuOwnWriteValue(key, L"NabuOwn_CciReadStatus", Context->CciReadStatus);
    NabuOwnWriteValue(key, L"NabuOwn_CciReadAttemptCount", Context->CciReadAttemptCount);
    NabuOwnWriteValue(key, L"NabuOwn_CciRxByte0", Context->CciRxByte0);
    NabuOwnWriteValue(key, L"NabuOwn_CciRxByte1", Context->CciRxByte1);
    NabuOwnWriteValue(key, L"NabuOwn_CciChipId", Context->CciChipId);
    NabuOwnWriteValue(key, L"NabuOwn_CciIdMatchesExpected", Context->CciIdMatchesExpected);
    NabuOwnWriteValue(key, L"NabuOwn_CciDeinitStatus", Context->CciDeinitStatus);
    NabuOwnWriteValue(key, L"NabuOwn_CciRelease1Status", Context->CciRelease1Status);
    NabuOwnWriteValue(key, L"NabuOwn_CciResumeIdleCalled", Context->CciResumeIdleCalled);
    NabuOwnWriteValue(key, L"NabuOwn_CciRelease2Status", Context->CciRelease2Status);

    WdfRegistryClose(key);
#endif
}
#if !defined(AVSTREAM_BUILD) && !defined(NABU_REAR_CAPTURE_LIBRARY)

NTSTATUS
NabuOwnEvtPrepareHardware(
    _In_ WDFDEVICE Device,
    _In_ WDFCMRESLIST ResourcesRaw,
    _In_ WDFCMRESLIST ResourcesTranslated)
{
    PNABU_OWN_STAGE23_CONTEXT context = NabuOwnGetContext(Device);

    UNREFERENCED_PARAMETER(ResourcesRaw);
    UNREFERENCED_PARAMETER(ResourcesTranslated);

    context->PrepareCount += 1;
    NabuOwnQueryScfg(context);
    NabuOwnReadAndClearArm(context);

    if (context->ArmRequested != 0 &&
        context->ArmRequested != NABU_OWN_ARM_REAL_CAPTURE) {
        context->SafetyRejectedArm = context->ArmRequested;
    }
    NabuOwnWriteDiagnostics(context);

    return STATUS_SUCCESS;
}

NTSTATUS
NabuOwnEvtD0Entry(
    _In_ WDFDEVICE Device,
    _In_ WDF_POWER_DEVICE_STATE PreviousState)
{
    PNABU_OWN_STAGE23_CONTEXT context = NabuOwnGetContext(Device);

    UNREFERENCED_PARAMETER(PreviousState);

    context->D0EntryCount += 1;
    NabuOwnWriteDiagnostics(context);

    // The arm is cleared before D0. Map the VCM buffer first, then capture one
    // rear-sensor frame while that camera IOVA remains active.
    if (context->ArmRequested == NABU_OWN_ARM_REAL_CAPTURE) {
        NabuOwnCreateDmaBuffer(context);
        NabuOwnRunVcmProbe(context);
        NabuOwnReleaseDmaBuffer(context);
    }

    return STATUS_SUCCESS;
}

NTSTATUS
NabuOwnEvtReleaseHardware(
    _In_ WDFDEVICE Device,
    _In_ WDFCMRESLIST ResourcesTranslated)
{
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(ResourcesTranslated);
    return STATUS_SUCCESS;
}

NTSTATUS
NabuOwnEvtDeviceAdd(
    _In_ WDFDRIVER Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit)
{
    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_PNPPOWER_EVENT_CALLBACKS pnpCallbacks;
    WDF_DMA_ENABLER_CONFIG dmaConfig;
    WDFDEVICE device;
    PNABU_OWN_STAGE23_CONTEXT context;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(Driver);

    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpCallbacks);
    pnpCallbacks.EvtDevicePrepareHardware = NabuOwnEvtPrepareHardware;
    pnpCallbacks.EvtDeviceReleaseHardware = NabuOwnEvtReleaseHardware;
    pnpCallbacks.EvtDeviceD0Entry = NabuOwnEvtD0Entry;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpCallbacks);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, NABU_OWN_STAGE23_CONTEXT);
    status = WdfDeviceCreate(&DeviceInit, &attributes, &device);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    context = NabuOwnGetContext(device);
    RtlZeroMemory(context, sizeof(*context));
    context->Device = device;

    // This creates only the framework DMA relationship. The one-shot common
    // buffer, VCM actions and TPG frame remain gated by Arm=25 in D0 entry.
    WDF_DMA_ENABLER_CONFIG_INIT(&dmaConfig,
                                WdfDmaProfileScatterGather64Duplex,
                                NABU_OWN_DMA_BUFFER_BYTES);
    status = WdfDmaEnablerCreate(device,
                                 &dmaConfig,
                                 WDF_NO_OBJECT_ATTRIBUTES,
                                 &context->DmaEnabler);
    context->DmaEnablerCreateStatus = (ULONG)status;

    // A platform without a WDF DMA adapter must remain usable for diagnostics;
    // the one-shot test will report this status and allocate nothing.
    return STATUS_SUCCESS;
}

NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    WDF_DRIVER_CONFIG config;

    WDF_DRIVER_CONFIG_INIT(&config, NabuOwnEvtDeviceAdd);
    return WdfDriverCreate(DriverObject,
                           RegistryPath,
                           WDF_NO_OBJECT_ATTRIBUTES,
                           &config,
                           WDF_NO_HANDLE);
}
#endif /* !AVSTREAM_BUILD && !NABU_REAR_CAPTURE_LIBRARY */
