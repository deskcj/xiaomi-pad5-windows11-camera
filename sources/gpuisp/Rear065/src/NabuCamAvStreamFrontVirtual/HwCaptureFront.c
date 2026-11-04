//
// NabuCamOwnStage70.c
//
// Manual one-shot front OV8856 RAW capture through the Stage-68-proven
// CSIPHY1/CSID0 path and the rear-camera-proven VFE0/VCM DMA path.
// No boot trigger, automatic hardware touch, device restart, or hardware IRQ.
//
#include <ntddk.h>
#include <wdf.h>
#include "NabuCamOwnIoctl.h"
#include "HwCaptureFront.h"
#include "Platform.h"
#include "Spmi.h"

#define NABU_OWN_STAGE70_BUILD_TAG 20261071UL

#define NABU_PLAT_CMD_ACQUIRE       0x232017UL
#define NABU_PLAT_CMD_RELEASE       0x23201BUL
#define NABU_FRONT_CCI_MASTER       1UL
#define NABU_CCI_READ_BYTES         0x40UL
#define NABU_OWN_RT_BANDWIDTH       3000000000ULL

#define NABU_CAMCC_PHYS             0x0AD00000ULL
#define NABU_CAMCC_BYTES            0x00010000UL
#define NABU_TITAN_TOP_GDSCR        0xC1BCUL
#define NABU_CPHY_RX_CMD_RCGR       0xA064UL
#define NABU_CPHY_RX_CFG_RCGR       0xA068UL
#define NABU_CSI1PHYTIMER_CMD_RCGR  0x6028UL
#define NABU_CSI1PHYTIMER_CFG_RCGR  0x602CUL
#define NABU_CSI1PHYTIMER_CBCR      0x6040UL
#define NABU_CSIPHY1_CBCR           0x6044UL
#define NABU_CSIPHY1_PHYS           0x0AC66000ULL
#define NABU_CSIPHY1_BYTES          0x00001000UL
#define NABU_FRONT_LANE_MASK        0x07UL
#define NABU_FRONT_LANE_ENABLE      0x85UL
#define NABU_FRONT_SETTLE_COUNT     0x0EUL
#define NABU_MCLK1_CMD_RCGR         0x5024UL
#define NABU_MCLK1_CFG_RCGR         0x5028UL
#define NABU_MCLK1_CBCR             0x503CUL
#define NABU_TLMM_NORTH_PHYS        0x03900000ULL
#define NABU_TLMM_NORTH_BYTES       0x00020000UL
#define NABU_TLMM_EAST_PHYS         0x03500000ULL
#define NABU_TLMM_EAST_BYTES        0x00020000UL
#define NABU_TLMM_CTL(gpio)         (0x1000UL * (ULONG)(gpio))
#define NABU_TLMM_IO(gpio)          (NABU_TLMM_CTL(gpio) + 4UL)
#define NABU_FRONT_VANA_GPIO        31UL
#define NABU_FRONT_MCLK_GPIO        14UL
#define NABU_FRONT_RESET_GPIO       30UL

#define NABU_IFE0_GDSCR             0xA004UL
#define NABU_IFE0_GDSCR_STATUS      0xA008UL
#define NABU_IFE0_CLK_CMD           0xA010UL
#define NABU_IFE0_CLK_CFG           0xA014UL
#define NABU_IFE0_CLK_CBCR          0xA028UL
#define NABU_IFE0_CSID_CMD_RCGR     0xA03CUL
#define NABU_IFE0_CSID_CFG_RCGR     0xA040UL
#define NABU_IFE0_CSID_CBCR         0xA054UL
#define NABU_IFE0_CPHY_RX_CBCR      0xA07CUL
#define NABU_IFE0_AXI_CBCR          0xA080UL
#define NABU_CMD_UPDATE             0x00000001UL
#define NABU_CMD_ROOT_ENABLE        0x00000002UL
#define NABU_CBCR_ENABLE            0x00000001UL
#define NABU_CBCR_CLOCK_OFF         0x80000000UL
#define NABU_GDSC_POWER_UP          0x00010000UL

#define NABU_CSID0_PHYS             0x0ACB3000ULL
#define NABU_CSID0_BYTES            0x00001000UL
#define NABU_CSID_HW_VERSION        0x0000UL
#define NABU_CSID_RST_STROBES       0x0010UL
#define NABU_CSID_RX_IRQ_STATUS     0x0020UL
#define NABU_CSID_RX_IRQ_MASK       0x0024UL
#define NABU_CSID_RX_IRQ_CLEAR      0x0028UL
#define NABU_CSID_RDI0_IRQ_STATUS   0x0040UL
#define NABU_CSID_RDI0_IRQ_MASK     0x0044UL
#define NABU_CSID_RDI0_IRQ_CLEAR    0x0048UL
#define NABU_CSID_TOP_IRQ_STATUS    0x0070UL
#define NABU_CSID_TOP_IRQ_MASK      0x0074UL
#define NABU_CSID_TOP_IRQ_CLEAR     0x0078UL
#define NABU_CSID_IRQ_CMD           0x0080UL
#define NABU_CSID_RX_CFG0           0x0100UL
#define NABU_CSID_RX_CFG1           0x0104UL
#define NABU_CSID_RX_CAPTURE        0x0108UL
#define NABU_CSID_LONG_PKT0         0x0130UL
#define NABU_CSID_LONG_PKT1         0x0134UL
#define NABU_CSID_TOTAL_PKTS        0x0160UL
#define NABU_CSID_ECC_STATS         0x0164UL
#define NABU_CSID_CRC_ERRORS        0x0168UL
#define NABU_CSID_RDI0_CFG0         0x0300UL
#define NABU_CSID_RDI0_CFG1         0x0304UL
#define NABU_CSID_RDI0_CTRL         0x0308UL
#define NABU_CSID_RDI0_DROP_PATTERN 0x030CUL
#define NABU_CSID_RDI0_DROP_PERIOD  0x0310UL
#define NABU_CSID_RDI0_IRQ_PATTERN  0x0314UL
#define NABU_CSID_RDI0_IRQ_PERIOD   0x0318UL
#define NABU_CSID_RDI0_PIX_PATTERN  0x0324UL
#define NABU_CSID_RDI0_PIX_PERIOD   0x0328UL
#define NABU_CSID_RDI0_LINE_PATTERN 0x032CUL
#define NABU_CSID_RDI0_LINE_PERIOD  0x0330UL
#define NABU_CSID_RDI0_STATUS       0x0350UL
#define NABU_CSID_RDI0_MEASURE0     0x0378UL
#define NABU_CSID_RDI0_MEASURE1     0x037CUL
#define NABU_CSID_RDI0_MEASURE2     0x0380UL
#define NABU_CSID_RDI0_BYTES_PING   0x03E0UL
#define NABU_CSID_RDI0_BYTES_PONG   0x03E4UL
#define NABU_CSID_RESET_RETAIN_SW   0x0000001EUL
#define NABU_CSID_RESET_DONE        0x00000001UL
#define NABU_FRONT_RX_CFG0          0x00100101UL
#define NABU_FRONT_RX_CFG1          0x00000041UL
#define NABU_FRONT_RDI_CFG0         0x802B2407UL
#define NABU_FRONT_RX_CAPTURE       0x000002B1UL
#define NABU_CSID_PATH_INPUT_EOF    0x00000200UL
#define NABU_CSID_PATH_INPUT_SOF    0x00001000UL
#define NABU_CSID_PATH_ERROR_MASK   0x00006004UL
#define NABU_CSID_RX_ERROR_MASK     0x05FFF800UL

#define NABU_VFE0_PHYS              0x0ACAF000ULL
#define NABU_VFE0_BYTES             0x00004000UL
#define NABU_VFE_WM0                0x00002200UL
#define NABU_VFE_HW_VERSION         0x00000000UL
#ifndef NABU_FRONT_WIDTH
#define NABU_FRONT_WIDTH            3264UL
#endif
#ifndef NABU_FRONT_HEIGHT
#define NABU_FRONT_HEIGHT           2448UL
#endif
#define NABU_FRONT_STRIDE           (NABU_FRONT_WIDTH * sizeof(USHORT))
#ifndef NABU_FRONT_FRAME_BYTES
#define NABU_FRONT_FRAME_BYTES      (NABU_FRONT_STRIDE * NABU_FRONT_HEIGHT)
#endif
#define NABU_FRONT_BUFFER_BYTES     0x01000000UL
#define NABU_FRONT_RAW_MARKER       0xABABABABUL
#define NABU_FRONT_RAW_POOL_TAG     '96RN'

#define NABU_SMMU_INTERFACE_BYTES    0x190UL
#define NABU_SMMU_DESIRED_ACCESS     0x001F01FFUL
#define NABU_SMMU_CONFIG_BYTES       0x128UL
#define NABU_SMMU_CONFIG_SELECTOR    1UL
#define NABU_SMMU_CONFIG_ENTRY_BYTES 0x18UL
#define NABU_SMMU_MAX_CONTEXT_BANKS  12UL
#define NABU_VCM_REGION_BYTES        0x80000000ULL
#define NABU_VCM_RESERVE_FLAGS       0x1E1UL
#define NABU_VCM_GUARD_BYTES         0x01000000ULL
#define NABU_VCM_STOCK_GUARD_BYTES   0x00600000ULL
#define NABU_VCM_STOCK_GUARD_FLAGS   0x1A1UL
#define NABU_SMMU_SET_STATE_OFFSET   0x28UL
#define NABU_SMMU_GET_CONFIG_OFFSET  0x30UL
#define NABU_SMMU_LEGACY_CB_OFFSET   0x38UL
#define NABU_SMMU_LEGACY_CB_REMOVE_OFFSET 0x40UL
#define NABU_VCM_CREATE_OFFSET       0x50UL
#define NABU_VCM_FREE_OFFSET         0x58UL
#define NABU_VCM_RESERVE_OFFSET      0x60UL
#define NABU_VCM_RELEASE_OFFSET      0x78UL
#define NABU_VCM_BACK_OFFSET         0x90UL
#define NABU_VCM_UNBACK_OFFSET       0x98UL
#define NABU_VCM_ASSOC_OFFSET        0xA0UL
#define NABU_VCM_DEASSOC_OFFSET      0xA8UL
#define NABU_VCM_ACTIVATE_OFFSET     0xB0UL
#define NABU_VCM_DEACTIVATE_OFFSET   0xB8UL
#define NABU_VCM_GET_DEV_ADDR_OFFSET 0xC0UL
#define NABU_IOMMU_GET_CONFIG_OFFSET 0x170UL
#define NABU_IOMMU_CB_OFFSET         0x178UL
#define NABU_IOMMU_IS_IOMMU_OFFSET   0x180UL

static const GUID NabuOwnSmmuDeviceInterfaceGuid =
    { 0x36079ae4, 0x78e8, 0x452d,
      { 0xaf, 0x50, 0x0c, 0xff, 0x78, 0xb2, 0xf1, 0xca } };
static const GUID NabuOwnSmmuQueryInterfaceGuid =
    { 0xb7ddb2a6, 0x7c10, 0x4129,
      { 0x81, 0xcc, 0x4b, 0xc5, 0x2b, 0x48, 0xed, 0x95 } };

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

typedef struct _NABU_SENSOR_REG8 {
    USHORT Register;
    UCHAR Value;
} NABU_SENSOR_REG8;

#include "NabuOv8856Stock.h"

typedef struct _NABU_OWN_DEVICE_CONTEXT {
    WDFKEY DiagnosticKey;
    ULONG DiagnosticKeyOpenStatus;
    ULONG PrepareCount;
    volatile LONG DevicePrepared;
    volatile LONG D0EntryCount;
    volatile LONG D0ExitCount;
    volatile LONG DeviceInD0;
    volatile LONG CommandInProgress;
    volatile LONG CommandRunCount;
    volatile LONG LastCommandStatus;
    volatile LONG D0ProbeAttempted;
    volatile LONG D0StopIdleStatus;
    volatile LONG D0ResumeIdleCalled;
    volatile LONG D0ProbeCompleted;
    volatile LONG HardwareTouched;
    volatile LONG FrontIdAttempted;
    volatile LONG FrontIdPhase;
    volatile LONG FrontIdCompleted;
    volatile LONG FrontPlatformOpenStatus;
    volatile LONG FrontAcquire1Status;
    volatile LONG FrontPowerStatus;
    volatile LONG FrontPinStatus;
    volatile LONG FrontInitMasterStatus;
    volatile LONG FrontAcquire2Status;
    volatile LONG FrontRailsReady;
    volatile LONG FrontSpmiInitStatus;
    volatile LONG FrontVioEnAfter;
    volatile LONG FrontVioStatusAfter;
    volatile LONG FrontVdigModeAfter;
    volatile LONG FrontVdigOutAfter;
    volatile LONG FrontVdigEnAfter;
    volatile LONG FrontPrepareReady;
    volatile LONG FrontMclk1Cmd;
    volatile LONG FrontMclk1Cfg;
    volatile LONG FrontMclk1Cbcr;
    volatile LONG FrontResetIo;
    volatile LONG FrontCciReadStatus;
    volatile LONG FrontCciReadAttemptCount;
    volatile LONG FrontCciRxByte0;
    volatile LONG FrontCciRxByte1;
    volatile LONG FrontCciChipId;
    volatile LONG FrontCciIdMatchesExpected;
    volatile LONG FrontDeinitStatus;
    volatile LONG FrontRelease1Status;
    volatile LONG FrontRelease2Status;
    volatile LONG FrontProgramAttempted;
    volatile LONG FrontProgramPhase;
    volatile LONG FrontProgramCompleted;
    volatile LONG FrontInitStatus;
    volatile LONG FrontInitFailIndex;
    volatile LONG FrontInitFailReg;
    volatile LONG FrontModeStatus;
    volatile LONG FrontModeFailIndex;
    volatile LONG FrontModeFailReg;
    volatile LONG FrontGeometryStatus;
    volatile LONG FrontWidth;
    volatile LONG FrontHeight;
    volatile LONG FrontStreamWriteStatus;
    volatile LONG FrontStreamReadStatus;
    volatile LONG FrontStreamReadback;
    volatile LONG FrontStreamStopStatus;
    volatile LONG FrontProgramLastTable;
    volatile LONG FrontProgramLastIndex;
    volatile LONG FrontProgramLastReg;
    volatile LONG FrontProgramLastStatus;
    volatile LONG FrontRtBandwidthStatus;
    volatile LONG FrontRtBandwidthReleaseStatus;
    volatile LONG FrontTitanGdscBefore;
    volatile LONG FrontTitanGdscAfter;
    volatile LONG FrontTitanGdscCfgAfter;
    volatile LONG FrontPll0LockOk;
    volatile LONG FrontCphyRxCmd;
    volatile LONG FrontCphyRxCfg;
    volatile LONG FrontPhyTimerCmd;
    volatile LONG FrontPhyTimerCfg;
    volatile LONG FrontPhyTimerCbcr;
    volatile LONG FrontPhyClockCbcr;
    volatile LONG FrontClockTreeReady;
    volatile LONG FrontPhyAttempted;
    volatile LONG FrontPhyPhase;
    volatile LONG FrontPhyCompleted;
    volatile LONG FrontPhyHwVersion;
    volatile LONG FrontPhyLaneMask;
    volatile LONG FrontPhyLaneEnable;
    volatile LONG FrontPhySettleCount;
    volatile LONG FrontPhyIrq5Before;
    volatile LONG FrontPhyIrq8Before;
    volatile LONG FrontPhyIrq5After;
    volatile LONG FrontPhyIrq8After;
    volatile LONG FrontPhyActivityDetected;
    volatile LONG FrontCsidAttempted;
    volatile LONG FrontCsidPhase;
    volatile LONG FrontCsidCompleted;
    volatile LONG FrontCsidHwVersion;
    volatile LONG FrontCsidResetTopIrq;
    volatile LONG FrontCsidResetPolls;
    volatile LONG FrontCsidRxCfg0;
    volatile LONG FrontCsidRxCfg1;
    volatile LONG FrontCsidRdiCfg0;
    volatile LONG FrontCsidPacketsBefore;
    volatile LONG FrontCsidPacketsAfter;
    volatile LONG FrontCsidRxIrqAfter;
    volatile LONG FrontCsidRdiIrqAfter;
    volatile LONG FrontCsidTopIrqAfter;
    volatile LONG FrontCsidEccStats;
    volatile LONG FrontCsidCrcErrors;
    volatile LONG FrontCsidLongPkt0;
    volatile LONG FrontCsidLongPkt1;
    volatile LONG FrontCsidRdiBytesPing;
    volatile LONG FrontCsidRdiBytesPong;
    volatile LONG FrontCsidRdiStatus;
    volatile LONG FrontCsidPolls;
    volatile LONG FrontCsidRestoreCompleted;
    volatile LONG FrontFrameSofSeen;
    volatile LONG FrontFrameEofSeen;
    volatile LONG FrontFrameComplete;
    volatile LONG FrontFramePacketDelta;
    volatile LONG FrontFrameCaptureCtrl;
    volatile LONG FrontFrameVirtualChannel;
    volatile LONG FrontFrameDataType;
    volatile LONG FrontFrameWordCount;
    volatile LONG FrontFrameLongPacketMatches;
    volatile LONG FrontFrameRxErrorMask;
    volatile LONG FrontFrameRdiErrorMask;
    volatile LONG FrontFrameMeasure0;
    volatile LONG FrontFrameMeasure1;
    volatile LONG FrontFrameMeasure2;
    volatile LONG FrontRawAttempted;
    volatile LONG FrontRawPhase;
    volatile LONG FrontRawCompleted;
    volatile LONG FrontRawBufferAllocStatus;
    volatile LONG FrontRawBufferBytes;
    volatile LONG FrontRawSmmuEnumerateStatus;
    volatile LONG FrontRawSmmuOpenStatus;
    volatile LONG FrontRawSmmuQueryStatus;
    volatile LONG FrontRawSmmuGetConfigStatus;
    volatile LONG FrontRawSmmuContextCount;
    volatile LONG FrontRawSmmuInterfaceSize;
    volatile LONG FrontRawSmmuInterfaceVersion;
    volatile LONG FrontRawSmmuContextLow;
    volatile LONG FrontRawSmmuContextHigh;
    volatile LONG FrontRawLegacyCallbackPresent;
    volatile LONG FrontRawLegacyCallbackRegistered;
    volatile LONG FrontRawLegacyCallbackRemoved;
    volatile LONG FrontRawIommuGetConfigPresent;
    volatile LONG FrontRawIommuCallbackPresent;
    volatile LONG FrontRawIommuIsPresent;
    volatile LONG FrontRawIommuIsResult;
    volatile LONG FrontRawConfigEntry0Index;
    volatile LONG FrontRawConfigEntry0HandleLow;
    volatile LONG FrontRawConfigEntry0HandleHigh;
    volatile LONG FrontRawConfigEntry1Index;
    volatile LONG FrontRawConfigEntry1HandleLow;
    volatile LONG FrontRawConfigEntry1HandleHigh;
    volatile LONG FrontRawConfigEntry2Index;
    volatile LONG FrontRawConfigEntry2HandleLow;
    volatile LONG FrontRawConfigEntry2HandleHigh;
    volatile LONG FrontRawVcmCreateStatus;
    volatile LONG FrontRawVcmAssociationCount;
    volatile LONG FrontRawVcmSetStateStatus;
    volatile LONG FrontRawVcmActivationCount;
    volatile LONG FrontRawVcmReserveStatus;
    volatile LONG FrontRawVcmBackStatus;
    volatile LONG FrontRawIovaLow;
    volatile LONG FrontRawIovaHigh;
    volatile LONG FrontRawPll3LockOk;
    volatile LONG FrontRawVfeHwVersion;
    volatile LONG FrontRawVfeResetStatus;
    volatile LONG FrontRawVfeResetPolls;
    volatile LONG FrontRawWmCfg;
    volatile LONG FrontRawWmImageAddress;
    volatile LONG FrontRawWmStatus0;
    volatile LONG FrontRawWmStatus1;
    volatile LONG FrontRawVfeTopIrq0;
    volatile LONG FrontRawVfeTopIrq1;
    volatile LONG FrontRawVfeBusIrq0;
    volatile LONG FrontRawVfeBusIrq1;
    volatile LONG FrontRawVfeBusIrq2;
    volatile LONG FrontRawVfeBusDebug0;
    volatile LONG FrontRawWaitPolls;
    volatile LONG FrontRawWaitExitReason;
    volatile LONG FrontRawBufferSample0;
    volatile LONG FrontRawBufferSample1;
    volatile LONG FrontRawBufferSample2;
    volatile LONG FrontRawBufferSample3;
    volatile LONG FrontRawBufferTail0;
    volatile LONG FrontRawBufferTail1;
    volatile LONG FrontRawBufferChanged;
    volatile LONG FrontRawBufferTailChanged;
    volatile LONG FrontRawFileStatus;
    volatile LONG FrontRawFileBytes;
    volatile LONG FrontRawVcmUnbackStatus;
    volatile LONG FrontRawVcmReleaseStatus;
    volatile LONG FrontRawVcmDeactivateStatus;
    volatile LONG FrontRawVcmSetStateOffStatus;
    volatile LONG FrontRawVcmDeassocStatus;
    volatile LONG FrontRawVcmFreeStatus;
#ifdef AVSTREAM_BUILD
    volatile LONG AvVcmSessionState;
    volatile LONG AvVcmSessionStatus;
    volatile LONG AvVcmPersistent;
    volatile LONG AvVcmStockGuardStatus;
    volatile LONG AvRawMdlFlags;
    volatile LONG AvRawMdlByteCount;
    volatile LONG AvRawMdlByteOffset;
    volatile LONG AvRawFirstPfnLow;
    volatile LONG AvRawFirstPfnHigh;
    volatile LONG AvRawLastPfnLow;
    volatile LONG AvRawLastPfnHigh;
    PWSTR AvSmmuInterfaceList;
    PFILE_OBJECT AvSmmuFileObject;
    NABU_SMMU_INTERFACE AvSmmuInterface;
    PVOID AvVcmRegion;
    PVOID AvVcmStockGuard;
    PVOID AvVcmAssociations[NABU_SMMU_MAX_CONTEXT_BANKS];
    ULONG AvVcmAssociated;
    ULONG AvVcmActivated;
    BOOLEAN AvLegacyCallbackRegistered;
    BOOLEAN AvSmmuStateEnabled;
    NABU_SMMU_SET_STATE AvSetState;
    NABU_SMMU_LEGACY_CALLBACK_REMOVE AvLegacyCallbackRemove;
    NABU_VCM_FREE AvVcmFree;
    NABU_VCM_RESERVE AvVcmReserve;
    NABU_VCM_HANDLE_STATUS AvVcmRelease;
    NABU_VCM_BACK AvVcmBack;
    NABU_VCM_HANDLE_STATUS AvVcmUnback;
    NABU_VCM_HANDLE_STATUS AvVcmDeassoc;
    NABU_VCM_HANDLE_STATUS AvVcmDeactivate;
    NABU_VCM_GET_DEV_ADDR AvVcmGetDevAddr;
#endif
} NABU_OWN_DEVICE_CONTEXT, *PNABU_OWN_DEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(NABU_OWN_DEVICE_CONTEXT,
                                   NabuOwnGetDeviceContext);

#ifndef AVSTREAM_BUILD
typedef struct _NABU_OWN_CONTROL_CONTEXT {
    WDFDEVICE TargetDevice;
    WDFWAITLOCK TargetLock;
} NABU_OWN_CONTROL_CONTEXT, *PNABU_OWN_CONTROL_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(NABU_OWN_CONTROL_CONTEXT,
                                   NabuOwnGetControlContext);

static WDFDEVICE g_NabuOwnControlDevice;

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD NabuOwnEvtDeviceAdd;
EVT_WDF_DEVICE_PREPARE_HARDWARE NabuOwnEvtPrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE NabuOwnEvtReleaseHardware;
EVT_WDF_DEVICE_D0_ENTRY NabuOwnEvtDeviceD0Entry;
EVT_WDF_DEVICE_D0_EXIT NabuOwnEvtDeviceD0Exit;
EVT_WDF_OBJECT_CONTEXT_CLEANUP NabuOwnEvtFdoCleanup;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL NabuOwnEvtIoDeviceControl;
#else
static NABU_FRONT_RAW_CONSUMER g_NabuFrontRawConsumer;
static PVOID g_NabuFrontRawConsumerContext;
#endif

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

static ULONG
NabuOwnReadLong(_In_ volatile LONG *Value)
{
    return (ULONG)InterlockedCompareExchange(Value, 0, 0);
}

static VOID
NabuOwnWriteDiagnostics(_In_ PNABU_OWN_DEVICE_CONTEXT Context)
{
#ifdef AVSTREAM_BUILD
    //
    // AVStream owns PnP.  A WdfDeviceMiniportCreate handle cannot be used
    // with WdfDeviceOpenRegistryKey, so the preview backend keeps status only
    // in memory.
    //
    UNREFERENCED_PARAMETER(Context);
    return;
#else
    WDFKEY key = Context->DiagnosticKey;

    if (key == NULL) {
        return;
    }

    NabuOwnWriteValue(key, L"NabuOwn_BuildTag",
                      NABU_OWN_STAGE70_BUILD_TAG);
    NabuOwnWriteValue(key, L"NabuOwn_Stage", 70);
    NabuOwnWriteValue(key, L"NabuOwn_HardwareTouched",
                      NabuOwnReadLong(&Context->HardwareTouched));
    NabuOwnWriteValue(key, L"NabuOwn_ManualInterfaceReady", 1);
    NabuOwnWriteValue(key, L"NabuOwn_ManualDevicePrepared",
                      (ULONG)InterlockedCompareExchange(
                          &Context->DevicePrepared, 0, 0));
    NabuOwnWriteValue(key, L"NabuOwn_ManualCommandInProgress",
                      (ULONG)InterlockedCompareExchange(
                          &Context->CommandInProgress, 0, 0));
    NabuOwnWriteValue(key, L"NabuOwn_ManualCommandRunCount",
                      (ULONG)InterlockedCompareExchange(
                          &Context->CommandRunCount, 0, 0));
    NabuOwnWriteValue(key, L"NabuOwn_ManualLastCommandStatus",
                      (ULONG)InterlockedCompareExchange(
                          &Context->LastCommandStatus, 0, 0));
    NabuOwnWriteValue(key, L"NabuOwn_DiagnosticKeyOpenStatus",
                      Context->DiagnosticKeyOpenStatus);
    NabuOwnWriteValue(key, L"NabuOwn_PrepareCount",
                      Context->PrepareCount);
    NabuOwnWriteValue(key, L"NabuOwn_D0EntryCount",
                      (ULONG)InterlockedCompareExchange(
                          &Context->D0EntryCount, 0, 0));
    NabuOwnWriteValue(key, L"NabuOwn_D0ExitCount",
                      (ULONG)InterlockedCompareExchange(
                          &Context->D0ExitCount, 0, 0));
    NabuOwnWriteValue(key, L"NabuOwn_DeviceInD0",
                      (ULONG)InterlockedCompareExchange(
                          &Context->DeviceInD0, 0, 0));
    NabuOwnWriteValue(key, L"NabuOwn_D0ProbeAttempted",
                      (ULONG)InterlockedCompareExchange(
                          &Context->D0ProbeAttempted, 0, 0));
    NabuOwnWriteValue(key, L"NabuOwn_D0StopIdleStatus",
                      (ULONG)InterlockedCompareExchange(
                          &Context->D0StopIdleStatus, 0, 0));
    NabuOwnWriteValue(key, L"NabuOwn_D0ResumeIdleCalled",
                      (ULONG)InterlockedCompareExchange(
                          &Context->D0ResumeIdleCalled, 0, 0));
    NabuOwnWriteValue(key, L"NabuOwn_D0ProbeCompleted",
                      (ULONG)InterlockedCompareExchange(
                          &Context->D0ProbeCompleted, 0, 0));
    NabuOwnWriteValue(key, L"NabuOwn_FrontIdAttempted",
                      NabuOwnReadLong(&Context->FrontIdAttempted));
    NabuOwnWriteValue(key, L"NabuOwn_FrontIdPhase",
                      NabuOwnReadLong(&Context->FrontIdPhase));
    NabuOwnWriteValue(key, L"NabuOwn_FrontIdCompleted",
                      NabuOwnReadLong(&Context->FrontIdCompleted));
    NabuOwnWriteValue(key, L"NabuOwn_FrontPlatformOpenStatus",
                      NabuOwnReadLong(&Context->FrontPlatformOpenStatus));
    NabuOwnWriteValue(key, L"NabuOwn_FrontAcquire1Status",
                      NabuOwnReadLong(&Context->FrontAcquire1Status));
    NabuOwnWriteValue(key, L"NabuOwn_FrontPowerStatus",
                      NabuOwnReadLong(&Context->FrontPowerStatus));
    NabuOwnWriteValue(key, L"NabuOwn_FrontPinStatus",
                      NabuOwnReadLong(&Context->FrontPinStatus));
    NabuOwnWriteValue(key, L"NabuOwn_FrontInitMasterStatus",
                      NabuOwnReadLong(&Context->FrontInitMasterStatus));
    NabuOwnWriteValue(key, L"NabuOwn_FrontAcquire2Status",
                      NabuOwnReadLong(&Context->FrontAcquire2Status));
    NabuOwnWriteValue(key, L"NabuOwn_FrontRailsReady",
                      NabuOwnReadLong(&Context->FrontRailsReady));
    NabuOwnWriteValue(key, L"NabuOwn_FrontSpmiInitStatus",
                      NabuOwnReadLong(&Context->FrontSpmiInitStatus));
    NabuOwnWriteValue(key, L"NabuOwn_FrontVioEnAfter",
                      NabuOwnReadLong(&Context->FrontVioEnAfter));
    NabuOwnWriteValue(key, L"NabuOwn_FrontVioStatusAfter",
                      NabuOwnReadLong(&Context->FrontVioStatusAfter));
    NabuOwnWriteValue(key, L"NabuOwn_FrontVdigModeAfter",
                      NabuOwnReadLong(&Context->FrontVdigModeAfter));
    NabuOwnWriteValue(key, L"NabuOwn_FrontVdigOutAfter",
                      NabuOwnReadLong(&Context->FrontVdigOutAfter));
    NabuOwnWriteValue(key, L"NabuOwn_FrontVdigEnAfter",
                      NabuOwnReadLong(&Context->FrontVdigEnAfter));
    NabuOwnWriteValue(key, L"NabuOwn_FrontPrepareReady",
                      NabuOwnReadLong(&Context->FrontPrepareReady));
    NabuOwnWriteValue(key, L"NabuOwn_FrontMclk1Cmd",
                      NabuOwnReadLong(&Context->FrontMclk1Cmd));
    NabuOwnWriteValue(key, L"NabuOwn_FrontMclk1Cfg",
                      NabuOwnReadLong(&Context->FrontMclk1Cfg));
    NabuOwnWriteValue(key, L"NabuOwn_FrontMclk1Cbcr",
                      NabuOwnReadLong(&Context->FrontMclk1Cbcr));
    NabuOwnWriteValue(key, L"NabuOwn_FrontResetIo",
                      NabuOwnReadLong(&Context->FrontResetIo));
    NabuOwnWriteValue(key, L"NabuOwn_FrontCciReadStatus",
                      NabuOwnReadLong(&Context->FrontCciReadStatus));
    NabuOwnWriteValue(key, L"NabuOwn_FrontCciReadAttemptCount",
                      NabuOwnReadLong(&Context->FrontCciReadAttemptCount));
    NabuOwnWriteValue(key, L"NabuOwn_FrontCciRxByte0",
                      NabuOwnReadLong(&Context->FrontCciRxByte0));
    NabuOwnWriteValue(key, L"NabuOwn_FrontCciRxByte1",
                      NabuOwnReadLong(&Context->FrontCciRxByte1));
    NabuOwnWriteValue(key, L"NabuOwn_FrontCciChipId",
                      NabuOwnReadLong(&Context->FrontCciChipId));
    NabuOwnWriteValue(key, L"NabuOwn_FrontCciIdMatchesExpected",
                      NabuOwnReadLong(&Context->FrontCciIdMatchesExpected));
    NabuOwnWriteValue(key, L"NabuOwn_FrontDeinitStatus",
                      NabuOwnReadLong(&Context->FrontDeinitStatus));
    NabuOwnWriteValue(key, L"NabuOwn_FrontRelease1Status",
                      NabuOwnReadLong(&Context->FrontRelease1Status));
    NabuOwnWriteValue(key, L"NabuOwn_FrontRelease2Status",
                      NabuOwnReadLong(&Context->FrontRelease2Status));
    NabuOwnWriteValue(key, L"NabuOwn_FrontProgramAttempted",
                      NabuOwnReadLong(&Context->FrontProgramAttempted));
    NabuOwnWriteValue(key, L"NabuOwn_FrontProgramPhase",
                      NabuOwnReadLong(&Context->FrontProgramPhase));
    NabuOwnWriteValue(key, L"NabuOwn_FrontProgramCompleted",
                      NabuOwnReadLong(&Context->FrontProgramCompleted));
    NabuOwnWriteValue(key, L"NabuOwn_FrontInitStatus",
                      NabuOwnReadLong(&Context->FrontInitStatus));
    NabuOwnWriteValue(key, L"NabuOwn_FrontInitFailIndex",
                      NabuOwnReadLong(&Context->FrontInitFailIndex));
    NabuOwnWriteValue(key, L"NabuOwn_FrontInitFailReg",
                      NabuOwnReadLong(&Context->FrontInitFailReg));
    NabuOwnWriteValue(key, L"NabuOwn_FrontModeStatus",
                      NabuOwnReadLong(&Context->FrontModeStatus));
    NabuOwnWriteValue(key, L"NabuOwn_FrontModeFailIndex",
                      NabuOwnReadLong(&Context->FrontModeFailIndex));
    NabuOwnWriteValue(key, L"NabuOwn_FrontModeFailReg",
                      NabuOwnReadLong(&Context->FrontModeFailReg));
    NabuOwnWriteValue(key, L"NabuOwn_FrontGeometryStatus",
                      NabuOwnReadLong(&Context->FrontGeometryStatus));
    NabuOwnWriteValue(key, L"NabuOwn_FrontWidth",
                      NabuOwnReadLong(&Context->FrontWidth));
    NabuOwnWriteValue(key, L"NabuOwn_FrontHeight",
                      NabuOwnReadLong(&Context->FrontHeight));
    NabuOwnWriteValue(key, L"NabuOwn_FrontStreamWriteStatus",
                      NabuOwnReadLong(&Context->FrontStreamWriteStatus));
    NabuOwnWriteValue(key, L"NabuOwn_FrontStreamReadStatus",
                      NabuOwnReadLong(&Context->FrontStreamReadStatus));
    NabuOwnWriteValue(key, L"NabuOwn_FrontStreamReadback",
                      NabuOwnReadLong(&Context->FrontStreamReadback));
    NabuOwnWriteValue(key, L"NabuOwn_FrontStreamStopStatus",
                      NabuOwnReadLong(&Context->FrontStreamStopStatus));
    NabuOwnWriteValue(key, L"NabuOwn_FrontProgramLastTable",
                      NabuOwnReadLong(&Context->FrontProgramLastTable));
    NabuOwnWriteValue(key, L"NabuOwn_FrontProgramLastIndex",
                      NabuOwnReadLong(&Context->FrontProgramLastIndex));
    NabuOwnWriteValue(key, L"NabuOwn_FrontProgramLastReg",
                      NabuOwnReadLong(&Context->FrontProgramLastReg));
    NabuOwnWriteValue(key, L"NabuOwn_FrontProgramLastStatus",
                      NabuOwnReadLong(&Context->FrontProgramLastStatus));
    NabuOwnWriteValue(key, L"NabuOwn_FrontRtBandwidthStatus",
                      NabuOwnReadLong(&Context->FrontRtBandwidthStatus));
    NabuOwnWriteValue(key, L"NabuOwn_FrontRtBandwidthReleaseStatus",
                      NabuOwnReadLong(
                          &Context->FrontRtBandwidthReleaseStatus));
    NabuOwnWriteValue(key, L"NabuOwn_FrontTitanGdscBefore",
                      NabuOwnReadLong(&Context->FrontTitanGdscBefore));
    NabuOwnWriteValue(key, L"NabuOwn_FrontTitanGdscAfter",
                      NabuOwnReadLong(&Context->FrontTitanGdscAfter));
    NabuOwnWriteValue(key, L"NabuOwn_FrontTitanGdscCfgAfter",
                      NabuOwnReadLong(&Context->FrontTitanGdscCfgAfter));
    NabuOwnWriteValue(key, L"NabuOwn_FrontPll0LockOk",
                      NabuOwnReadLong(&Context->FrontPll0LockOk));
    NabuOwnWriteValue(key, L"NabuOwn_FrontCphyRxCmd",
                      NabuOwnReadLong(&Context->FrontCphyRxCmd));
    NabuOwnWriteValue(key, L"NabuOwn_FrontCphyRxCfg",
                      NabuOwnReadLong(&Context->FrontCphyRxCfg));
    NabuOwnWriteValue(key, L"NabuOwn_FrontPhyTimerCmd",
                      NabuOwnReadLong(&Context->FrontPhyTimerCmd));
    NabuOwnWriteValue(key, L"NabuOwn_FrontPhyTimerCfg",
                      NabuOwnReadLong(&Context->FrontPhyTimerCfg));
    NabuOwnWriteValue(key, L"NabuOwn_FrontPhyTimerCbcr",
                      NabuOwnReadLong(&Context->FrontPhyTimerCbcr));
    NabuOwnWriteValue(key, L"NabuOwn_FrontPhyClockCbcr",
                      NabuOwnReadLong(&Context->FrontPhyClockCbcr));
    NabuOwnWriteValue(key, L"NabuOwn_FrontClockTreeReady",
                      NabuOwnReadLong(&Context->FrontClockTreeReady));
    NabuOwnWriteValue(key, L"NabuOwn_FrontPhyAttempted",
                      NabuOwnReadLong(&Context->FrontPhyAttempted));
    NabuOwnWriteValue(key, L"NabuOwn_FrontPhyPhase",
                      NabuOwnReadLong(&Context->FrontPhyPhase));
    NabuOwnWriteValue(key, L"NabuOwn_FrontPhyCompleted",
                      NabuOwnReadLong(&Context->FrontPhyCompleted));
    NabuOwnWriteValue(key, L"NabuOwn_FrontPhyHwVersion",
                      NabuOwnReadLong(&Context->FrontPhyHwVersion));
    NabuOwnWriteValue(key, L"NabuOwn_FrontPhyLaneMask",
                      NabuOwnReadLong(&Context->FrontPhyLaneMask));
    NabuOwnWriteValue(key, L"NabuOwn_FrontPhyLaneEnable",
                      NabuOwnReadLong(&Context->FrontPhyLaneEnable));
    NabuOwnWriteValue(key, L"NabuOwn_FrontPhySettleCount",
                      NabuOwnReadLong(&Context->FrontPhySettleCount));
    NabuOwnWriteValue(key, L"NabuOwn_FrontPhyIrq5Before",
                      NabuOwnReadLong(&Context->FrontPhyIrq5Before));
    NabuOwnWriteValue(key, L"NabuOwn_FrontPhyIrq8Before",
                      NabuOwnReadLong(&Context->FrontPhyIrq8Before));
    NabuOwnWriteValue(key, L"NabuOwn_FrontPhyIrq5After",
                      NabuOwnReadLong(&Context->FrontPhyIrq5After));
    NabuOwnWriteValue(key, L"NabuOwn_FrontPhyIrq8After",
                      NabuOwnReadLong(&Context->FrontPhyIrq8After));
    NabuOwnWriteValue(key, L"NabuOwn_FrontPhyActivityDetected",
                      NabuOwnReadLong(
                          &Context->FrontPhyActivityDetected));
    NabuOwnWriteValue(key, L"NabuOwn_FrontCsidAttempted",
                      NabuOwnReadLong(&Context->FrontCsidAttempted));
    NabuOwnWriteValue(key, L"NabuOwn_FrontCsidPhase",
                      NabuOwnReadLong(&Context->FrontCsidPhase));
    NabuOwnWriteValue(key, L"NabuOwn_FrontCsidCompleted",
                      NabuOwnReadLong(&Context->FrontCsidCompleted));
    NabuOwnWriteValue(key, L"NabuOwn_FrontCsidHwVersion",
                      NabuOwnReadLong(&Context->FrontCsidHwVersion));
    NabuOwnWriteValue(key, L"NabuOwn_FrontCsidResetTopIrq",
                      NabuOwnReadLong(&Context->FrontCsidResetTopIrq));
    NabuOwnWriteValue(key, L"NabuOwn_FrontCsidResetPolls",
                      NabuOwnReadLong(&Context->FrontCsidResetPolls));
    NabuOwnWriteValue(key, L"NabuOwn_FrontCsidRxCfg0",
                      NabuOwnReadLong(&Context->FrontCsidRxCfg0));
    NabuOwnWriteValue(key, L"NabuOwn_FrontCsidRxCfg1",
                      NabuOwnReadLong(&Context->FrontCsidRxCfg1));
    NabuOwnWriteValue(key, L"NabuOwn_FrontCsidRdiCfg0",
                      NabuOwnReadLong(&Context->FrontCsidRdiCfg0));
    NabuOwnWriteValue(key, L"NabuOwn_FrontCsidPacketsBefore",
                      NabuOwnReadLong(&Context->FrontCsidPacketsBefore));
    NabuOwnWriteValue(key, L"NabuOwn_FrontCsidPacketsAfter",
                      NabuOwnReadLong(&Context->FrontCsidPacketsAfter));
    NabuOwnWriteValue(key, L"NabuOwn_FrontCsidRxIrqAfter",
                      NabuOwnReadLong(&Context->FrontCsidRxIrqAfter));
    NabuOwnWriteValue(key, L"NabuOwn_FrontCsidRdiIrqAfter",
                      NabuOwnReadLong(&Context->FrontCsidRdiIrqAfter));
    NabuOwnWriteValue(key, L"NabuOwn_FrontCsidTopIrqAfter",
                      NabuOwnReadLong(&Context->FrontCsidTopIrqAfter));
    NabuOwnWriteValue(key, L"NabuOwn_FrontCsidEccStats",
                      NabuOwnReadLong(&Context->FrontCsidEccStats));
    NabuOwnWriteValue(key, L"NabuOwn_FrontCsidCrcErrors",
                      NabuOwnReadLong(&Context->FrontCsidCrcErrors));
    NabuOwnWriteValue(key, L"NabuOwn_FrontCsidLongPkt0",
                      NabuOwnReadLong(&Context->FrontCsidLongPkt0));
    NabuOwnWriteValue(key, L"NabuOwn_FrontCsidLongPkt1",
                      NabuOwnReadLong(&Context->FrontCsidLongPkt1));
    NabuOwnWriteValue(key, L"NabuOwn_FrontCsidRdiBytesPing",
                      NabuOwnReadLong(&Context->FrontCsidRdiBytesPing));
    NabuOwnWriteValue(key, L"NabuOwn_FrontCsidRdiBytesPong",
                      NabuOwnReadLong(&Context->FrontCsidRdiBytesPong));
    NabuOwnWriteValue(key, L"NabuOwn_FrontCsidRdiStatus",
                      NabuOwnReadLong(&Context->FrontCsidRdiStatus));
    NabuOwnWriteValue(key, L"NabuOwn_FrontCsidPolls",
                      NabuOwnReadLong(&Context->FrontCsidPolls));
    NabuOwnWriteValue(key, L"NabuOwn_FrontCsidRestoreCompleted",
                      NabuOwnReadLong(
                          &Context->FrontCsidRestoreCompleted));
    NabuOwnWriteValue(key, L"NabuOwn_FrontFrameSofSeen",
                      NabuOwnReadLong(&Context->FrontFrameSofSeen));
    NabuOwnWriteValue(key, L"NabuOwn_FrontFrameEofSeen",
                      NabuOwnReadLong(&Context->FrontFrameEofSeen));
    NabuOwnWriteValue(key, L"NabuOwn_FrontFrameComplete",
                      NabuOwnReadLong(&Context->FrontFrameComplete));
    NabuOwnWriteValue(key, L"NabuOwn_FrontFramePacketDelta",
                      NabuOwnReadLong(&Context->FrontFramePacketDelta));
    NabuOwnWriteValue(key, L"NabuOwn_FrontFrameCaptureCtrl",
                      NabuOwnReadLong(&Context->FrontFrameCaptureCtrl));
    NabuOwnWriteValue(key, L"NabuOwn_FrontFrameVirtualChannel",
                      NabuOwnReadLong(
                          &Context->FrontFrameVirtualChannel));
    NabuOwnWriteValue(key, L"NabuOwn_FrontFrameDataType",
                      NabuOwnReadLong(&Context->FrontFrameDataType));
    NabuOwnWriteValue(key, L"NabuOwn_FrontFrameWordCount",
                      NabuOwnReadLong(&Context->FrontFrameWordCount));
    NabuOwnWriteValue(key, L"NabuOwn_FrontFrameLongPktMatches",
                      NabuOwnReadLong(
                          &Context->FrontFrameLongPacketMatches));
    NabuOwnWriteValue(key, L"NabuOwn_FrontFrameRxErrorMask",
                      NabuOwnReadLong(&Context->FrontFrameRxErrorMask));
    NabuOwnWriteValue(key, L"NabuOwn_FrontFrameRdiErrorMask",
                      NabuOwnReadLong(&Context->FrontFrameRdiErrorMask));
    NabuOwnWriteValue(key, L"NabuOwn_FrontFrameMeasure0",
                      NabuOwnReadLong(&Context->FrontFrameMeasure0));
    NabuOwnWriteValue(key, L"NabuOwn_FrontFrameMeasure1",
                      NabuOwnReadLong(&Context->FrontFrameMeasure1));
    NabuOwnWriteValue(key, L"NabuOwn_FrontFrameMeasure2",
                      NabuOwnReadLong(&Context->FrontFrameMeasure2));
    NabuOwnWriteValue(key, L"NabuOwn_FrontRawAttempted",
                      NabuOwnReadLong(&Context->FrontRawAttempted));
    NabuOwnWriteValue(key, L"NabuOwn_FrontRawPhase",
                      NabuOwnReadLong(&Context->FrontRawPhase));
    NabuOwnWriteValue(key, L"NabuOwn_FrontRawCompleted",
                      NabuOwnReadLong(&Context->FrontRawCompleted));
    NabuOwnWriteValue(key, L"NabuOwn_FrontRawBufferAllocStatus",
                      NabuOwnReadLong(&Context->FrontRawBufferAllocStatus));
    NabuOwnWriteValue(key, L"NabuOwn_FrontRawBufferBytes",
                      NabuOwnReadLong(&Context->FrontRawBufferBytes));
    NabuOwnWriteValue(key, L"NabuOwn_FrontRawSmmuEnumerateStatus",
                      NabuOwnReadLong(&Context->FrontRawSmmuEnumerateStatus));
    NabuOwnWriteValue(key, L"NabuOwn_FrontRawSmmuOpenStatus",
                      NabuOwnReadLong(&Context->FrontRawSmmuOpenStatus));
    NabuOwnWriteValue(key, L"NabuOwn_FrontRawSmmuQueryStatus",
                      NabuOwnReadLong(&Context->FrontRawSmmuQueryStatus));
    NabuOwnWriteValue(key, L"NabuOwn_FrontRawSmmuGetConfigStatus",
                      NabuOwnReadLong(&Context->FrontRawSmmuGetConfigStatus));
    NabuOwnWriteValue(key, L"NabuOwn_FrontRawSmmuContextCount",
                      NabuOwnReadLong(&Context->FrontRawSmmuContextCount));
    NabuOwnWriteValue(key, L"NabuOwn_FrontRawSmmuInterfaceSize",
                      NabuOwnReadLong(&Context->FrontRawSmmuInterfaceSize));
    NabuOwnWriteValue(key, L"NabuOwn_FrontRawSmmuInterfaceVersion",
                      NabuOwnReadLong(&Context->FrontRawSmmuInterfaceVersion));
    NabuOwnWriteValue(key, L"NabuOwn_FrontRawSmmuContextLow",
                      NabuOwnReadLong(&Context->FrontRawSmmuContextLow));
    NabuOwnWriteValue(key, L"NabuOwn_FrontRawSmmuContextHigh",
                      NabuOwnReadLong(&Context->FrontRawSmmuContextHigh));
    NabuOwnWriteValue(key, L"NabuOwn_FrontRawLegacyCallbackPresent",
                      NabuOwnReadLong(&Context->FrontRawLegacyCallbackPresent));
    NabuOwnWriteValue(key, L"NabuOwn_FrontRawLegacyCallbackRegistered",
                      NabuOwnReadLong(&Context->FrontRawLegacyCallbackRegistered));
    NabuOwnWriteValue(key, L"NabuOwn_FrontRawLegacyCallbackRemoved",
                      NabuOwnReadLong(&Context->FrontRawLegacyCallbackRemoved));
    NabuOwnWriteValue(key, L"NabuOwn_FrontRawIommuGetConfigPresent",
                      NabuOwnReadLong(&Context->FrontRawIommuGetConfigPresent));
    NabuOwnWriteValue(key, L"NabuOwn_FrontRawIommuCallbackPresent",
                      NabuOwnReadLong(&Context->FrontRawIommuCallbackPresent));
    NabuOwnWriteValue(key, L"NabuOwn_FrontRawIommuIsPresent",
                      NabuOwnReadLong(&Context->FrontRawIommuIsPresent));
    NabuOwnWriteValue(key, L"NabuOwn_FrontRawIommuIsResult",
                      NabuOwnReadLong(&Context->FrontRawIommuIsResult));
    NabuOwnWriteValue(key, L"NabuOwn_FrontRawConfigEntry0Index",
                      NabuOwnReadLong(&Context->FrontRawConfigEntry0Index));
    NabuOwnWriteValue(key, L"NabuOwn_FrontRawConfigEntry0HandleLow",
                      NabuOwnReadLong(&Context->FrontRawConfigEntry0HandleLow));
    NabuOwnWriteValue(key, L"NabuOwn_FrontRawConfigEntry0HandleHigh",
                      NabuOwnReadLong(&Context->FrontRawConfigEntry0HandleHigh));
    NabuOwnWriteValue(key, L"NabuOwn_FrontRawConfigEntry1Index",
                      NabuOwnReadLong(&Context->FrontRawConfigEntry1Index));
    NabuOwnWriteValue(key, L"NabuOwn_FrontRawConfigEntry1HandleLow",
                      NabuOwnReadLong(&Context->FrontRawConfigEntry1HandleLow));
    NabuOwnWriteValue(key, L"NabuOwn_FrontRawConfigEntry1HandleHigh",
                      NabuOwnReadLong(&Context->FrontRawConfigEntry1HandleHigh));
    NabuOwnWriteValue(key, L"NabuOwn_FrontRawConfigEntry2Index",
                      NabuOwnReadLong(&Context->FrontRawConfigEntry2Index));
    NabuOwnWriteValue(key, L"NabuOwn_FrontRawConfigEntry2HandleLow",
                      NabuOwnReadLong(&Context->FrontRawConfigEntry2HandleLow));
    NabuOwnWriteValue(key, L"NabuOwn_FrontRawConfigEntry2HandleHigh",
                      NabuOwnReadLong(&Context->FrontRawConfigEntry2HandleHigh));
    NabuOwnWriteValue(key, L"NabuOwn_FrontRawVcmCreateStatus",
                      NabuOwnReadLong(&Context->FrontRawVcmCreateStatus));
    NabuOwnWriteValue(key, L"NabuOwn_FrontRawVcmAssociationCount",
                      NabuOwnReadLong(&Context->FrontRawVcmAssociationCount));
    NabuOwnWriteValue(key, L"NabuOwn_FrontRawVcmSetStateStatus",
                      NabuOwnReadLong(&Context->FrontRawVcmSetStateStatus));
    NabuOwnWriteValue(key, L"NabuOwn_FrontRawVcmActivationCount",
                      NabuOwnReadLong(&Context->FrontRawVcmActivationCount));
    NabuOwnWriteValue(key, L"NabuOwn_FrontRawVcmReserveStatus",
                      NabuOwnReadLong(&Context->FrontRawVcmReserveStatus));
    NabuOwnWriteValue(key, L"NabuOwn_FrontRawVcmBackStatus",
                      NabuOwnReadLong(&Context->FrontRawVcmBackStatus));
    NabuOwnWriteValue(key, L"NabuOwn_FrontRawIovaLow",
                      NabuOwnReadLong(&Context->FrontRawIovaLow));
    NabuOwnWriteValue(key, L"NabuOwn_FrontRawIovaHigh",
                      NabuOwnReadLong(&Context->FrontRawIovaHigh));
    NabuOwnWriteValue(key, L"NabuOwn_FrontRawPll3LockOk",
                      NabuOwnReadLong(&Context->FrontRawPll3LockOk));
    NabuOwnWriteValue(key, L"NabuOwn_FrontRawVfeHwVersion",
                      NabuOwnReadLong(&Context->FrontRawVfeHwVersion));
    NabuOwnWriteValue(key, L"NabuOwn_FrontRawVfeResetStatus",
                      NabuOwnReadLong(&Context->FrontRawVfeResetStatus));
    NabuOwnWriteValue(key, L"NabuOwn_FrontRawVfeResetPolls",
                      NabuOwnReadLong(&Context->FrontRawVfeResetPolls));
    NabuOwnWriteValue(key, L"NabuOwn_FrontRawWmCfg",
                      NabuOwnReadLong(&Context->FrontRawWmCfg));
    NabuOwnWriteValue(key, L"NabuOwn_FrontRawWmImageAddress",
                      NabuOwnReadLong(&Context->FrontRawWmImageAddress));
    NabuOwnWriteValue(key, L"NabuOwn_FrontRawWmStatus0",
                      NabuOwnReadLong(&Context->FrontRawWmStatus0));
    NabuOwnWriteValue(key, L"NabuOwn_FrontRawWmStatus1",
                      NabuOwnReadLong(&Context->FrontRawWmStatus1));
    NabuOwnWriteValue(key, L"NabuOwn_FrontRawVfeTopIrq0",
                      NabuOwnReadLong(&Context->FrontRawVfeTopIrq0));
    NabuOwnWriteValue(key, L"NabuOwn_FrontRawVfeTopIrq1",
                      NabuOwnReadLong(&Context->FrontRawVfeTopIrq1));
    NabuOwnWriteValue(key, L"NabuOwn_FrontRawVfeBusIrq0",
                      NabuOwnReadLong(&Context->FrontRawVfeBusIrq0));
    NabuOwnWriteValue(key, L"NabuOwn_FrontRawVfeBusIrq1",
                      NabuOwnReadLong(&Context->FrontRawVfeBusIrq1));
    NabuOwnWriteValue(key, L"NabuOwn_FrontRawVfeBusIrq2",
                      NabuOwnReadLong(&Context->FrontRawVfeBusIrq2));
    NabuOwnWriteValue(key, L"NabuOwn_FrontRawVfeBusDebug0",
                      NabuOwnReadLong(&Context->FrontRawVfeBusDebug0));
    NabuOwnWriteValue(key, L"NabuOwn_FrontRawWaitPolls",
                      NabuOwnReadLong(&Context->FrontRawWaitPolls));
    NabuOwnWriteValue(key, L"NabuOwn_FrontRawWaitExitReason",
                      NabuOwnReadLong(&Context->FrontRawWaitExitReason));
    NabuOwnWriteValue(key, L"NabuOwn_FrontRawBufferSample0",
                      NabuOwnReadLong(&Context->FrontRawBufferSample0));
    NabuOwnWriteValue(key, L"NabuOwn_FrontRawBufferSample1",
                      NabuOwnReadLong(&Context->FrontRawBufferSample1));
    NabuOwnWriteValue(key, L"NabuOwn_FrontRawBufferSample2",
                      NabuOwnReadLong(&Context->FrontRawBufferSample2));
    NabuOwnWriteValue(key, L"NabuOwn_FrontRawBufferSample3",
                      NabuOwnReadLong(&Context->FrontRawBufferSample3));
    NabuOwnWriteValue(key, L"NabuOwn_FrontRawBufferTail0",
                      NabuOwnReadLong(&Context->FrontRawBufferTail0));
    NabuOwnWriteValue(key, L"NabuOwn_FrontRawBufferTail1",
                      NabuOwnReadLong(&Context->FrontRawBufferTail1));
    NabuOwnWriteValue(key, L"NabuOwn_FrontRawBufferChanged",
                      NabuOwnReadLong(&Context->FrontRawBufferChanged));
    NabuOwnWriteValue(key, L"NabuOwn_FrontRawBufferTailChanged",
                      NabuOwnReadLong(&Context->FrontRawBufferTailChanged));
    NabuOwnWriteValue(key, L"NabuOwn_FrontRawFileStatus",
                      NabuOwnReadLong(&Context->FrontRawFileStatus));
    NabuOwnWriteValue(key, L"NabuOwn_FrontRawFileBytes",
                      NabuOwnReadLong(&Context->FrontRawFileBytes));
    NabuOwnWriteValue(key, L"NabuOwn_FrontRawVcmUnbackStatus",
                      NabuOwnReadLong(&Context->FrontRawVcmUnbackStatus));
    NabuOwnWriteValue(key, L"NabuOwn_FrontRawVcmReleaseStatus",
                      NabuOwnReadLong(&Context->FrontRawVcmReleaseStatus));
    NabuOwnWriteValue(key, L"NabuOwn_FrontRawVcmDeactivateStatus",
                      NabuOwnReadLong(&Context->FrontRawVcmDeactivateStatus));
    NabuOwnWriteValue(key, L"NabuOwn_FrontRawVcmSetStateOffStatus",
                      NabuOwnReadLong(&Context->FrontRawVcmSetStateOffStatus));
    NabuOwnWriteValue(key, L"NabuOwn_FrontRawVcmDeassocStatus",
                      NabuOwnReadLong(&Context->FrontRawVcmDeassocStatus));
    NabuOwnWriteValue(key, L"NabuOwn_FrontRawVcmFreeStatus",
                      NabuOwnReadLong(&Context->FrontRawVcmFreeStatus));
#endif
}

static VOID
NabuOwnFillStatus(
    _In_ PNABU_OWN_DEVICE_CONTEXT Context,
    _Out_ PNABU_CAM_MANUAL_STATUS Status)
{
    RtlZeroMemory(Status, sizeof(*Status));
    Status->Size = sizeof(*Status);
    Status->ProtocolVersion = NABU_CAM_MANUAL_PROTOCOL_VERSION;
    Status->BuildTag = NABU_OWN_STAGE70_BUILD_TAG;
    Status->Stage = 70;
    Status->DevicePrepared =
        (ULONG)InterlockedCompareExchange(&Context->DevicePrepared, 0, 0);
    Status->HardwareTouched =
        NabuOwnReadLong(&Context->HardwareTouched);
    Status->CommandInProgress =
        (ULONG)InterlockedCompareExchange(
            &Context->CommandInProgress, 0, 0);
    Status->CommandRunCount =
        (ULONG)InterlockedCompareExchange(
            &Context->CommandRunCount, 0, 0);
    Status->LastCommandStatus =
        (ULONG)InterlockedCompareExchange(
            &Context->LastCommandStatus, 0, 0);
    Status->D0EntryCount =
        (ULONG)InterlockedCompareExchange(
            &Context->D0EntryCount, 0, 0);
    Status->D0ExitCount =
        (ULONG)InterlockedCompareExchange(
            &Context->D0ExitCount, 0, 0);
    Status->DeviceInD0 =
        (ULONG)InterlockedCompareExchange(
            &Context->DeviceInD0, 0, 0);
    Status->D0ProbeAttempted =
        (ULONG)InterlockedCompareExchange(
            &Context->D0ProbeAttempted, 0, 0);
    Status->D0StopIdleStatus =
        (ULONG)InterlockedCompareExchange(
            &Context->D0StopIdleStatus, 0, 0);
    Status->D0ResumeIdleCalled =
        (ULONG)InterlockedCompareExchange(
            &Context->D0ResumeIdleCalled, 0, 0);
    Status->D0ProbeCompleted =
        (ULONG)InterlockedCompareExchange(
            &Context->D0ProbeCompleted, 0, 0);
    Status->FrontIdAttempted =
        NabuOwnReadLong(&Context->FrontIdAttempted);
    Status->FrontIdPhase =
        NabuOwnReadLong(&Context->FrontIdPhase);
    Status->FrontIdCompleted =
        NabuOwnReadLong(&Context->FrontIdCompleted);
    Status->FrontPlatformOpenStatus =
        NabuOwnReadLong(&Context->FrontPlatformOpenStatus);
    Status->FrontAcquire1Status =
        NabuOwnReadLong(&Context->FrontAcquire1Status);
    Status->FrontPowerStatus =
        NabuOwnReadLong(&Context->FrontPowerStatus);
    Status->FrontPinStatus =
        NabuOwnReadLong(&Context->FrontPinStatus);
    Status->FrontInitMasterStatus =
        NabuOwnReadLong(&Context->FrontInitMasterStatus);
    Status->FrontAcquire2Status =
        NabuOwnReadLong(&Context->FrontAcquire2Status);
    Status->FrontRailsReady =
        NabuOwnReadLong(&Context->FrontRailsReady);
    Status->FrontSpmiInitStatus =
        NabuOwnReadLong(&Context->FrontSpmiInitStatus);
    Status->FrontVioEnAfter =
        NabuOwnReadLong(&Context->FrontVioEnAfter);
    Status->FrontVioStatusAfter =
        NabuOwnReadLong(&Context->FrontVioStatusAfter);
    Status->FrontVdigModeAfter =
        NabuOwnReadLong(&Context->FrontVdigModeAfter);
    Status->FrontVdigOutAfter =
        NabuOwnReadLong(&Context->FrontVdigOutAfter);
    Status->FrontVdigEnAfter =
        NabuOwnReadLong(&Context->FrontVdigEnAfter);
    Status->FrontPrepareReady =
        NabuOwnReadLong(&Context->FrontPrepareReady);
    Status->FrontMclk1Cmd =
        NabuOwnReadLong(&Context->FrontMclk1Cmd);
    Status->FrontMclk1Cfg =
        NabuOwnReadLong(&Context->FrontMclk1Cfg);
    Status->FrontMclk1Cbcr =
        NabuOwnReadLong(&Context->FrontMclk1Cbcr);
    Status->FrontResetIo =
        NabuOwnReadLong(&Context->FrontResetIo);
    Status->FrontCciReadStatus =
        NabuOwnReadLong(&Context->FrontCciReadStatus);
    Status->FrontCciReadAttemptCount =
        NabuOwnReadLong(&Context->FrontCciReadAttemptCount);
    Status->FrontCciRxByte0 =
        NabuOwnReadLong(&Context->FrontCciRxByte0);
    Status->FrontCciRxByte1 =
        NabuOwnReadLong(&Context->FrontCciRxByte1);
    Status->FrontCciChipId =
        NabuOwnReadLong(&Context->FrontCciChipId);
    Status->FrontCciIdMatchesExpected =
        NabuOwnReadLong(&Context->FrontCciIdMatchesExpected);
    Status->FrontDeinitStatus =
        NabuOwnReadLong(&Context->FrontDeinitStatus);
    Status->FrontRelease1Status =
        NabuOwnReadLong(&Context->FrontRelease1Status);
    Status->FrontRelease2Status =
        NabuOwnReadLong(&Context->FrontRelease2Status);
    Status->FrontProgramAttempted =
        NabuOwnReadLong(&Context->FrontProgramAttempted);
    Status->FrontProgramPhase =
        NabuOwnReadLong(&Context->FrontProgramPhase);
    Status->FrontProgramCompleted =
        NabuOwnReadLong(&Context->FrontProgramCompleted);
    Status->FrontInitStatus =
        NabuOwnReadLong(&Context->FrontInitStatus);
    Status->FrontInitFailIndex =
        NabuOwnReadLong(&Context->FrontInitFailIndex);
    Status->FrontInitFailReg =
        NabuOwnReadLong(&Context->FrontInitFailReg);
    Status->FrontModeStatus =
        NabuOwnReadLong(&Context->FrontModeStatus);
    Status->FrontModeFailIndex =
        NabuOwnReadLong(&Context->FrontModeFailIndex);
    Status->FrontModeFailReg =
        NabuOwnReadLong(&Context->FrontModeFailReg);
    Status->FrontGeometryStatus =
        NabuOwnReadLong(&Context->FrontGeometryStatus);
    Status->FrontWidth =
        NabuOwnReadLong(&Context->FrontWidth);
    Status->FrontHeight =
        NabuOwnReadLong(&Context->FrontHeight);
    Status->FrontStreamWriteStatus =
        NabuOwnReadLong(&Context->FrontStreamWriteStatus);
    Status->FrontStreamReadStatus =
        NabuOwnReadLong(&Context->FrontStreamReadStatus);
    Status->FrontStreamReadback =
        NabuOwnReadLong(&Context->FrontStreamReadback);
    Status->FrontStreamStopStatus =
        NabuOwnReadLong(&Context->FrontStreamStopStatus);
    Status->FrontProgramLastTable =
        NabuOwnReadLong(&Context->FrontProgramLastTable);
    Status->FrontProgramLastIndex =
        NabuOwnReadLong(&Context->FrontProgramLastIndex);
    Status->FrontProgramLastReg =
        NabuOwnReadLong(&Context->FrontProgramLastReg);
    Status->FrontProgramLastStatus =
        NabuOwnReadLong(&Context->FrontProgramLastStatus);
    Status->FrontRtBandwidthStatus =
        NabuOwnReadLong(&Context->FrontRtBandwidthStatus);
    Status->FrontRtBandwidthReleaseStatus =
        NabuOwnReadLong(&Context->FrontRtBandwidthReleaseStatus);
    Status->FrontTitanGdscBefore =
        NabuOwnReadLong(&Context->FrontTitanGdscBefore);
    Status->FrontTitanGdscAfter =
        NabuOwnReadLong(&Context->FrontTitanGdscAfter);
    Status->FrontTitanGdscCfgAfter =
        NabuOwnReadLong(&Context->FrontTitanGdscCfgAfter);
    Status->FrontPll0LockOk =
        NabuOwnReadLong(&Context->FrontPll0LockOk);
    Status->FrontCphyRxCmd =
        NabuOwnReadLong(&Context->FrontCphyRxCmd);
    Status->FrontCphyRxCfg =
        NabuOwnReadLong(&Context->FrontCphyRxCfg);
    Status->FrontPhyTimerCmd =
        NabuOwnReadLong(&Context->FrontPhyTimerCmd);
    Status->FrontPhyTimerCfg =
        NabuOwnReadLong(&Context->FrontPhyTimerCfg);
    Status->FrontPhyTimerCbcr =
        NabuOwnReadLong(&Context->FrontPhyTimerCbcr);
    Status->FrontPhyClockCbcr =
        NabuOwnReadLong(&Context->FrontPhyClockCbcr);
    Status->FrontClockTreeReady =
        NabuOwnReadLong(&Context->FrontClockTreeReady);
    Status->FrontPhyAttempted =
        NabuOwnReadLong(&Context->FrontPhyAttempted);
    Status->FrontPhyPhase =
        NabuOwnReadLong(&Context->FrontPhyPhase);
    Status->FrontPhyCompleted =
        NabuOwnReadLong(&Context->FrontPhyCompleted);
    Status->FrontPhyHwVersion =
        NabuOwnReadLong(&Context->FrontPhyHwVersion);
    Status->FrontPhyLaneMask =
        NabuOwnReadLong(&Context->FrontPhyLaneMask);
    Status->FrontPhyLaneEnable =
        NabuOwnReadLong(&Context->FrontPhyLaneEnable);
    Status->FrontPhySettleCount =
        NabuOwnReadLong(&Context->FrontPhySettleCount);
    Status->FrontPhyIrq5Before =
        NabuOwnReadLong(&Context->FrontPhyIrq5Before);
    Status->FrontPhyIrq8Before =
        NabuOwnReadLong(&Context->FrontPhyIrq8Before);
    Status->FrontPhyIrq5After =
        NabuOwnReadLong(&Context->FrontPhyIrq5After);
    Status->FrontPhyIrq8After =
        NabuOwnReadLong(&Context->FrontPhyIrq8After);
    Status->FrontPhyActivityDetected =
        NabuOwnReadLong(&Context->FrontPhyActivityDetected);
    Status->FrontCsidAttempted =
        NabuOwnReadLong(&Context->FrontCsidAttempted);
    Status->FrontCsidPhase =
        NabuOwnReadLong(&Context->FrontCsidPhase);
    Status->FrontCsidCompleted =
        NabuOwnReadLong(&Context->FrontCsidCompleted);
    Status->FrontCsidHwVersion =
        NabuOwnReadLong(&Context->FrontCsidHwVersion);
    Status->FrontCsidResetTopIrq =
        NabuOwnReadLong(&Context->FrontCsidResetTopIrq);
    Status->FrontCsidResetPolls =
        NabuOwnReadLong(&Context->FrontCsidResetPolls);
    Status->FrontCsidRxCfg0 =
        NabuOwnReadLong(&Context->FrontCsidRxCfg0);
    Status->FrontCsidRxCfg1 =
        NabuOwnReadLong(&Context->FrontCsidRxCfg1);
    Status->FrontCsidRdiCfg0 =
        NabuOwnReadLong(&Context->FrontCsidRdiCfg0);
    Status->FrontCsidPacketsBefore =
        NabuOwnReadLong(&Context->FrontCsidPacketsBefore);
    Status->FrontCsidPacketsAfter =
        NabuOwnReadLong(&Context->FrontCsidPacketsAfter);
    Status->FrontCsidRxIrqAfter =
        NabuOwnReadLong(&Context->FrontCsidRxIrqAfter);
    Status->FrontCsidRdiIrqAfter =
        NabuOwnReadLong(&Context->FrontCsidRdiIrqAfter);
    Status->FrontCsidTopIrqAfter =
        NabuOwnReadLong(&Context->FrontCsidTopIrqAfter);
    Status->FrontCsidEccStats =
        NabuOwnReadLong(&Context->FrontCsidEccStats);
    Status->FrontCsidCrcErrors =
        NabuOwnReadLong(&Context->FrontCsidCrcErrors);
    Status->FrontCsidLongPkt0 =
        NabuOwnReadLong(&Context->FrontCsidLongPkt0);
    Status->FrontCsidLongPkt1 =
        NabuOwnReadLong(&Context->FrontCsidLongPkt1);
    Status->FrontCsidRdiBytesPing =
        NabuOwnReadLong(&Context->FrontCsidRdiBytesPing);
    Status->FrontCsidRdiBytesPong =
        NabuOwnReadLong(&Context->FrontCsidRdiBytesPong);
    Status->FrontCsidRdiStatus =
        NabuOwnReadLong(&Context->FrontCsidRdiStatus);
    Status->FrontCsidPolls =
        NabuOwnReadLong(&Context->FrontCsidPolls);
    Status->FrontCsidRestoreCompleted =
        NabuOwnReadLong(&Context->FrontCsidRestoreCompleted);
    Status->FrontFrameSofSeen =
        NabuOwnReadLong(&Context->FrontFrameSofSeen);
    Status->FrontFrameEofSeen =
        NabuOwnReadLong(&Context->FrontFrameEofSeen);
    Status->FrontFrameComplete =
        NabuOwnReadLong(&Context->FrontFrameComplete);
    Status->FrontFramePacketDelta =
        NabuOwnReadLong(&Context->FrontFramePacketDelta);
    Status->FrontFrameCaptureCtrl =
        NabuOwnReadLong(&Context->FrontFrameCaptureCtrl);
    Status->FrontFrameVirtualChannel =
        NabuOwnReadLong(&Context->FrontFrameVirtualChannel);
    Status->FrontFrameDataType =
        NabuOwnReadLong(&Context->FrontFrameDataType);
    Status->FrontFrameWordCount =
        NabuOwnReadLong(&Context->FrontFrameWordCount);
    Status->FrontFrameLongPacketMatches =
        NabuOwnReadLong(&Context->FrontFrameLongPacketMatches);
    Status->FrontFrameRxErrorMask =
        NabuOwnReadLong(&Context->FrontFrameRxErrorMask);
    Status->FrontFrameRdiErrorMask =
        NabuOwnReadLong(&Context->FrontFrameRdiErrorMask);
    Status->FrontFrameMeasure0 =
        NabuOwnReadLong(&Context->FrontFrameMeasure0);
    Status->FrontFrameMeasure1 =
        NabuOwnReadLong(&Context->FrontFrameMeasure1);
    Status->FrontFrameMeasure2 =
        NabuOwnReadLong(&Context->FrontFrameMeasure2);
    Status->FrontRawAttempted = NabuOwnReadLong(&Context->FrontRawAttempted);
    Status->FrontRawPhase = NabuOwnReadLong(&Context->FrontRawPhase);
    Status->FrontRawCompleted = NabuOwnReadLong(&Context->FrontRawCompleted);
    Status->FrontRawBufferAllocStatus =
        NabuOwnReadLong(&Context->FrontRawBufferAllocStatus);
    Status->FrontRawBufferBytes =
        NabuOwnReadLong(&Context->FrontRawBufferBytes);
    Status->FrontRawSmmuEnumerateStatus =
        NabuOwnReadLong(&Context->FrontRawSmmuEnumerateStatus);
    Status->FrontRawSmmuOpenStatus =
        NabuOwnReadLong(&Context->FrontRawSmmuOpenStatus);
    Status->FrontRawSmmuQueryStatus =
        NabuOwnReadLong(&Context->FrontRawSmmuQueryStatus);
    Status->FrontRawSmmuGetConfigStatus =
        NabuOwnReadLong(&Context->FrontRawSmmuGetConfigStatus);
    Status->FrontRawSmmuContextCount =
        NabuOwnReadLong(&Context->FrontRawSmmuContextCount);
    Status->FrontRawVcmCreateStatus =
        NabuOwnReadLong(&Context->FrontRawVcmCreateStatus);
    Status->FrontRawVcmAssociationCount =
        NabuOwnReadLong(&Context->FrontRawVcmAssociationCount);
    Status->FrontRawVcmSetStateStatus =
        NabuOwnReadLong(&Context->FrontRawVcmSetStateStatus);
    Status->FrontRawVcmActivationCount =
        NabuOwnReadLong(&Context->FrontRawVcmActivationCount);
    Status->FrontRawVcmReserveStatus =
        NabuOwnReadLong(&Context->FrontRawVcmReserveStatus);
    Status->FrontRawVcmBackStatus =
        NabuOwnReadLong(&Context->FrontRawVcmBackStatus);
    Status->FrontRawIovaLow = NabuOwnReadLong(&Context->FrontRawIovaLow);
    Status->FrontRawIovaHigh = NabuOwnReadLong(&Context->FrontRawIovaHigh);
    Status->FrontRawPll3LockOk =
        NabuOwnReadLong(&Context->FrontRawPll3LockOk);
    Status->FrontRawVfeHwVersion =
        NabuOwnReadLong(&Context->FrontRawVfeHwVersion);
    Status->FrontRawVfeResetStatus =
        NabuOwnReadLong(&Context->FrontRawVfeResetStatus);
    Status->FrontRawVfeResetPolls =
        NabuOwnReadLong(&Context->FrontRawVfeResetPolls);
    Status->FrontRawWmCfg = NabuOwnReadLong(&Context->FrontRawWmCfg);
    Status->FrontRawWmImageAddress =
        NabuOwnReadLong(&Context->FrontRawWmImageAddress);
    Status->FrontRawWmStatus0 =
        NabuOwnReadLong(&Context->FrontRawWmStatus0);
    Status->FrontRawWmStatus1 =
        NabuOwnReadLong(&Context->FrontRawWmStatus1);
    Status->FrontRawVfeTopIrq0 =
        NabuOwnReadLong(&Context->FrontRawVfeTopIrq0);
    Status->FrontRawVfeTopIrq1 =
        NabuOwnReadLong(&Context->FrontRawVfeTopIrq1);
    Status->FrontRawVfeBusIrq0 =
        NabuOwnReadLong(&Context->FrontRawVfeBusIrq0);
    Status->FrontRawVfeBusIrq1 =
        NabuOwnReadLong(&Context->FrontRawVfeBusIrq1);
    Status->FrontRawVfeBusIrq2 =
        NabuOwnReadLong(&Context->FrontRawVfeBusIrq2);
    Status->FrontRawVfeBusDebug0 =
        NabuOwnReadLong(&Context->FrontRawVfeBusDebug0);
    Status->FrontRawWaitPolls =
        NabuOwnReadLong(&Context->FrontRawWaitPolls);
    Status->FrontRawWaitExitReason =
        NabuOwnReadLong(&Context->FrontRawWaitExitReason);
    Status->FrontRawBufferSample0 =
        NabuOwnReadLong(&Context->FrontRawBufferSample0);
    Status->FrontRawBufferSample1 =
        NabuOwnReadLong(&Context->FrontRawBufferSample1);
    Status->FrontRawBufferSample2 =
        NabuOwnReadLong(&Context->FrontRawBufferSample2);
    Status->FrontRawBufferSample3 =
        NabuOwnReadLong(&Context->FrontRawBufferSample3);
    Status->FrontRawBufferTail0 =
        NabuOwnReadLong(&Context->FrontRawBufferTail0);
    Status->FrontRawBufferTail1 =
        NabuOwnReadLong(&Context->FrontRawBufferTail1);
    Status->FrontRawBufferChanged =
        NabuOwnReadLong(&Context->FrontRawBufferChanged);
    Status->FrontRawBufferTailChanged =
        NabuOwnReadLong(&Context->FrontRawBufferTailChanged);
    Status->FrontRawFileStatus =
        NabuOwnReadLong(&Context->FrontRawFileStatus);
    Status->FrontRawFileBytes =
        NabuOwnReadLong(&Context->FrontRawFileBytes);
    Status->FrontRawVcmUnbackStatus =
        NabuOwnReadLong(&Context->FrontRawVcmUnbackStatus);
    Status->FrontRawVcmReleaseStatus =
        NabuOwnReadLong(&Context->FrontRawVcmReleaseStatus);
    Status->FrontRawVcmDeactivateStatus =
        NabuOwnReadLong(&Context->FrontRawVcmDeactivateStatus);
    Status->FrontRawVcmSetStateOffStatus =
        NabuOwnReadLong(&Context->FrontRawVcmSetStateOffStatus);
    Status->FrontRawVcmDeassocStatus =
        NabuOwnReadLong(&Context->FrontRawVcmDeassocStatus);
    Status->FrontRawVcmFreeStatus =
        NabuOwnReadLong(&Context->FrontRawVcmFreeStatus);
}

static ULONG
NabuOwnRead32(_In_ PUCHAR Base, _In_ ULONG Offset)
{
    return *(volatile ULONG *)(Base + Offset);
}

static VOID
NabuOwnWrite32(
    _In_ PUCHAR Base,
    _In_ ULONG Offset,
    _In_ ULONG Value)
{
    *(volatile ULONG *)(Base + Offset) = Value;
    KeMemoryBarrier();
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
NabuOwnWaitMask(
    _In_ PUCHAR Base,
    _In_ ULONG Offset,
    _In_ ULONG Mask,
    _In_ BOOLEAN Set,
    _In_ ULONG Attempts,
    _Out_opt_ PULONG LastValue)
{
    ULONG attempt;
    ULONG value = 0;

    for (attempt = 0; attempt < Attempts; ++attempt) {
        value = NabuOwnRead32(Base, Offset);
        if (((value & Mask) != 0) == Set) {
            if (LastValue != NULL) {
                *LastValue = value;
            }
            return TRUE;
        }
        KeStallExecutionProcessor(10);
    }
    if (LastValue != NULL) {
        *LastValue = value;
    }
    return FALSE;
}

static VOID
NabuOwnSetFrontCsidPhase(
    _Inout_ PNABU_OWN_DEVICE_CONTEXT Context,
    _In_ LONG Phase)
{
    InterlockedExchange(&Context->FrontCsidPhase, Phase);
    NabuOwnWriteDiagnostics(Context);
    if (Context->DiagnosticKey != NULL) {
        (VOID)ZwFlushKey(
            WdfRegistryWdmGetHandle(Context->DiagnosticKey));
    }
}

typedef struct _NABU_IFE_CLOCK_STATE {
    ULONG Gdsc;
    ULONG GdscStatus;
    ULONG MainCmd;
    ULONG MainCfg;
    ULONG MainCbcr;
    ULONG AxiCbcr;
    ULONG CphyCmd;
    ULONG CphyCfg;
    ULONG CphyCbcr;
    ULONG CsidCmd;
    ULONG CsidCfg;
    ULONG CsidCbcr;
    ULONG Pll3[15];
    BOOLEAN Pll3Touched;
    BOOLEAN Captured;
} NABU_IFE_CLOCK_STATE, *PNABU_IFE_CLOCK_STATE;

static const ULONG NabuOwnPll3Offsets[15] = {
    0x00, 0x04, 0x08, 0x0C, 0x10,
    0x14, 0x18, 0x1C, 0x20, 0x24,
    0x28, 0x2C, 0x38, 0x40, 0x44
};

static VOID
NabuOwnCaptureIfeClockState(
    _In_ PUCHAR Camcc,
    _Out_ PNABU_IFE_CLOCK_STATE State)
{
    ULONG i;

    RtlZeroMemory(State, sizeof(*State));
    State->Gdsc = NabuOwnRead32(Camcc, NABU_IFE0_GDSCR);
    State->GdscStatus =
        NabuOwnRead32(Camcc, NABU_IFE0_GDSCR_STATUS);
    State->MainCmd = NabuOwnRead32(Camcc, NABU_IFE0_CLK_CMD);
    State->MainCfg = NabuOwnRead32(Camcc, NABU_IFE0_CLK_CFG);
    State->MainCbcr = NabuOwnRead32(Camcc, NABU_IFE0_CLK_CBCR);
    State->AxiCbcr = NabuOwnRead32(Camcc, NABU_IFE0_AXI_CBCR);
    State->CphyCmd = NabuOwnRead32(Camcc, NABU_CPHY_RX_CMD_RCGR);
    State->CphyCfg = NabuOwnRead32(Camcc, NABU_CPHY_RX_CFG_RCGR);
    State->CphyCbcr =
        NabuOwnRead32(Camcc, NABU_IFE0_CPHY_RX_CBCR);
    State->CsidCmd =
        NabuOwnRead32(Camcc, NABU_IFE0_CSID_CMD_RCGR);
    State->CsidCfg =
        NabuOwnRead32(Camcc, NABU_IFE0_CSID_CFG_RCGR);
    State->CsidCbcr = NabuOwnRead32(Camcc, NABU_IFE0_CSID_CBCR);
    for (i = 0; i < ARRAYSIZE(NabuOwnPll3Offsets); ++i) {
        State->Pll3[i] =
            NabuOwnRead32(Camcc, 0x3000UL + NabuOwnPll3Offsets[i]);
    }
    State->Captured = TRUE;
}

static BOOLEAN
NabuOwnConfigureIfePll3For558Mhz(
    _Inout_ PNABU_OWN_DEVICE_CONTEXT Context,
    _In_ PUCHAR Camcc)
{
    const ULONG pll = 0x3000UL;
    ULONG mode;
    ULONG user;
    ULONG elapsed;

    mode = NabuOwnRead32(Camcc, pll + 0x00) & ~1UL;
    NabuOwnWrite32(Camcc, pll + 0x00, mode);
    user = NabuOwnRead32(Camcc, pll + 0x0C) & ~7UL;
    NabuOwnWrite32(Camcc, pll + 0x0C, user);
    NabuOwnWrite32(Camcc, pll + 0x38, 0);
    NabuOwnWrite32(
        Camcc, pll + 0x00, (mode | (1UL << 23)) & ~(1UL << 2));
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
    NabuOwnWrite32(
        Camcc, pll + 0x00,
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
    if ((mode & (1UL << 31)) == 0) {
        Context->FrontRawPll3LockOk = 0;
        return FALSE;
    }

    NabuOwnWrite32(
        Camcc, pll + 0x0C,
        NabuOwnRead32(Camcc, pll + 0x0C) | 7UL);
    NabuOwnWrite32(
        Camcc, pll + 0x00,
        NabuOwnRead32(Camcc, pll + 0x00) | 1UL);
    KeMemoryBarrier();
    KeStallExecutionProcessor(100);
    Context->FrontRawPll3LockOk =
        ((NabuOwnRead32(Camcc, pll + 0x00) &
          ((1UL << 31) | 1UL)) == ((1UL << 31) | 1UL) &&
         (NabuOwnRead32(Camcc, pll + 0x38) & 1UL) != 0) ? 1 : 0;
    return Context->FrontRawPll3LockOk != 0;
}

static BOOLEAN
NabuOwnRaiseIfeClockForRaw(
    _Inout_ PNABU_OWN_DEVICE_CONTEXT Context,
    _In_ PUCHAR Camcc,
    _Inout_ PNABU_IFE_CLOCK_STATE State)
{
    ULONG value;

    NabuOwnWrite32(
        Camcc, NABU_IFE0_CLK_CBCR,
        NabuOwnRead32(Camcc, NABU_IFE0_CLK_CBCR) &
            ~NABU_CBCR_ENABLE);
    State->Pll3Touched = TRUE;
    if (!NabuOwnConfigureIfePll3For558Mhz(Context, Camcc)) {
        return FALSE;
    }
    NabuOwnWrite32(Camcc, NABU_IFE0_CLK_CFG, (6UL << 8) | 1UL);
    NabuOwnWrite32(
        Camcc, NABU_IFE0_CLK_CMD,
        NabuOwnRead32(Camcc, NABU_IFE0_CLK_CMD) |
            NABU_CMD_ROOT_ENABLE | NABU_CMD_UPDATE);
    if (!NabuOwnWaitMask(
            Camcc, NABU_IFE0_CLK_CMD,
            NABU_CMD_UPDATE, FALSE, 500, &value)) {
        return FALSE;
    }
    NabuOwnWrite32(
        Camcc, NABU_IFE0_CLK_CBCR,
        NabuOwnRead32(Camcc, NABU_IFE0_CLK_CBCR) |
            NABU_CBCR_ENABLE);
    return NabuOwnWaitMask(
        Camcc, NABU_IFE0_CLK_CBCR,
        NABU_CBCR_CLOCK_OFF, FALSE, 500, &value);
}

static BOOLEAN
NabuOwnEnableIfeBase(_In_ PUCHAR Camcc)
{
    ULONG value;

    NabuOwnWrite32(
        Camcc, NABU_IFE0_GDSCR,
        NabuOwnRead32(Camcc, NABU_IFE0_GDSCR) & ~1UL);
    if (!NabuOwnWaitMask(
            Camcc, NABU_IFE0_GDSCR_STATUS,
            NABU_GDSC_POWER_UP, TRUE, 2000, &value)) {
        return FALSE;
    }

    NabuOwnWrite32(
        Camcc, NABU_IFE0_CLK_CMD,
        NabuOwnRead32(Camcc, NABU_IFE0_CLK_CMD) |
            NABU_CMD_ROOT_ENABLE | NABU_CMD_UPDATE);
    if (!NabuOwnWaitMask(
            Camcc, NABU_IFE0_CLK_CMD,
            NABU_CMD_UPDATE, FALSE, 500, &value)) {
        return FALSE;
    }

    NabuOwnWrite32(
        Camcc, NABU_IFE0_CLK_CBCR,
        NabuOwnRead32(Camcc, NABU_IFE0_CLK_CBCR) |
            NABU_CBCR_ENABLE);
    if (!NabuOwnWaitMask(
            Camcc, NABU_IFE0_CLK_CBCR,
            NABU_CBCR_CLOCK_OFF, FALSE, 500, &value)) {
        return FALSE;
    }

    NabuOwnWrite32(
        Camcc, NABU_IFE0_AXI_CBCR,
        NabuOwnRead32(Camcc, NABU_IFE0_AXI_CBCR) |
            NABU_CBCR_ENABLE);
    return NabuOwnWaitMask(
        Camcc, NABU_IFE0_AXI_CBCR,
        NABU_CBCR_CLOCK_OFF, FALSE, 500, &value);
}

static BOOLEAN
NabuOwnEnableIfeReceive(_In_ PUCHAR Camcc)
{
    ULONG value;

    NabuOwnWrite32(
        Camcc, NABU_IFE0_CPHY_RX_CBCR,
        NabuOwnRead32(Camcc, NABU_IFE0_CPHY_RX_CBCR) &
            ~NABU_CBCR_ENABLE);
    NabuOwnWrite32(
        Camcc, NABU_IFE0_CSID_CBCR,
        NabuOwnRead32(Camcc, NABU_IFE0_CSID_CBCR) &
            ~NABU_CBCR_ENABLE);

    NabuOwnWrite32(Camcc, NABU_CPHY_RX_CFG_RCGR, 0x301UL);
    NabuOwnWrite32(
        Camcc, NABU_CPHY_RX_CMD_RCGR,
        NabuOwnRead32(Camcc, NABU_CPHY_RX_CMD_RCGR) |
            NABU_CMD_ROOT_ENABLE | NABU_CMD_UPDATE);
    if (!NabuOwnWaitMask(
            Camcc, NABU_CPHY_RX_CMD_RCGR,
            NABU_CMD_UPDATE, FALSE, 500, &value)) {
        return FALSE;
    }

    NabuOwnWrite32(Camcc, NABU_IFE0_CSID_CFG_RCGR, 0x103UL);
    NabuOwnWrite32(
        Camcc, NABU_IFE0_CSID_CMD_RCGR,
        NabuOwnRead32(Camcc, NABU_IFE0_CSID_CMD_RCGR) |
            NABU_CMD_ROOT_ENABLE | NABU_CMD_UPDATE);
    if (!NabuOwnWaitMask(
            Camcc, NABU_IFE0_CSID_CMD_RCGR,
            NABU_CMD_UPDATE, FALSE, 500, &value)) {
        return FALSE;
    }

    NabuOwnWrite32(
        Camcc, NABU_IFE0_CPHY_RX_CBCR,
        NabuOwnRead32(Camcc, NABU_IFE0_CPHY_RX_CBCR) |
            NABU_CBCR_ENABLE);
    if (!NabuOwnWaitMask(
            Camcc, NABU_IFE0_CPHY_RX_CBCR,
            NABU_CBCR_CLOCK_OFF, FALSE, 500, &value)) {
        return FALSE;
    }
    NabuOwnWrite32(
        Camcc, NABU_IFE0_CSID_CBCR,
        NabuOwnRead32(Camcc, NABU_IFE0_CSID_CBCR) |
            NABU_CBCR_ENABLE);
    return NabuOwnWaitMask(
        Camcc, NABU_IFE0_CSID_CBCR,
        NABU_CBCR_CLOCK_OFF, FALSE, 500, &value);
}

static BOOLEAN
NabuOwnRestoreIfeClockState(
    _In_ PUCHAR Camcc,
    _In_ PNABU_IFE_CLOCK_STATE State)
{
    ULONG value;
    ULONG i;
    BOOLEAN ok = TRUE;

    if (!State->Captured) {
        return TRUE;
    }

    NabuOwnWrite32(
        Camcc, NABU_IFE0_CPHY_RX_CBCR,
        NabuOwnRead32(Camcc, NABU_IFE0_CPHY_RX_CBCR) &
            ~NABU_CBCR_ENABLE);
    NabuOwnWrite32(
        Camcc, NABU_IFE0_CSID_CBCR,
        NabuOwnRead32(Camcc, NABU_IFE0_CSID_CBCR) &
            ~NABU_CBCR_ENABLE);

    NabuOwnWrite32(Camcc, NABU_CPHY_RX_CFG_RCGR, State->CphyCfg);
    value = NabuOwnRead32(Camcc, NABU_CPHY_RX_CMD_RCGR);
    value = (value & ~NABU_CMD_ROOT_ENABLE) |
        (State->CphyCmd & NABU_CMD_ROOT_ENABLE) |
        NABU_CMD_UPDATE;
    NabuOwnWrite32(Camcc, NABU_CPHY_RX_CMD_RCGR, value);
    ok = NabuOwnWaitMask(
        Camcc, NABU_CPHY_RX_CMD_RCGR,
        NABU_CMD_UPDATE, FALSE, 500, &value) && ok;

    NabuOwnWrite32(Camcc, NABU_IFE0_CSID_CFG_RCGR, State->CsidCfg);
    value = NabuOwnRead32(Camcc, NABU_IFE0_CSID_CMD_RCGR);
    value = (value & ~NABU_CMD_ROOT_ENABLE) |
        (State->CsidCmd & NABU_CMD_ROOT_ENABLE) |
        NABU_CMD_UPDATE;
    NabuOwnWrite32(Camcc, NABU_IFE0_CSID_CMD_RCGR, value);
    ok = NabuOwnWaitMask(
        Camcc, NABU_IFE0_CSID_CMD_RCGR,
        NABU_CMD_UPDATE, FALSE, 500, &value) && ok;

    if ((State->CphyCbcr & NABU_CBCR_ENABLE) != 0) {
        NabuOwnWrite32(
            Camcc, NABU_IFE0_CPHY_RX_CBCR,
            NabuOwnRead32(Camcc, NABU_IFE0_CPHY_RX_CBCR) |
                NABU_CBCR_ENABLE);
    }
    if ((State->CsidCbcr & NABU_CBCR_ENABLE) != 0) {
        NabuOwnWrite32(
            Camcc, NABU_IFE0_CSID_CBCR,
            NabuOwnRead32(Camcc, NABU_IFE0_CSID_CBCR) |
                NABU_CBCR_ENABLE);
    }

    if ((State->AxiCbcr & NABU_CBCR_ENABLE) == 0) {
        NabuOwnWrite32(
            Camcc, NABU_IFE0_AXI_CBCR,
            NabuOwnRead32(Camcc, NABU_IFE0_AXI_CBCR) &
                ~NABU_CBCR_ENABLE);
    }
    NabuOwnWrite32(
        Camcc, NABU_IFE0_CLK_CBCR,
        NabuOwnRead32(Camcc, NABU_IFE0_CLK_CBCR) &
            ~NABU_CBCR_ENABLE);
    NabuOwnWrite32(Camcc, NABU_IFE0_CLK_CFG, State->MainCfg);
    value = NabuOwnRead32(Camcc, NABU_IFE0_CLK_CMD);
    value = (value & ~NABU_CMD_ROOT_ENABLE) |
        (State->MainCmd & NABU_CMD_ROOT_ENABLE) |
        NABU_CMD_UPDATE;
    NabuOwnWrite32(Camcc, NABU_IFE0_CLK_CMD, value);
    ok = NabuOwnWaitMask(
        Camcc, NABU_IFE0_CLK_CMD,
        NABU_CMD_UPDATE, FALSE, 500, &value) && ok;

    if (State->Pll3Touched) {
        NabuOwnWrite32(
            Camcc, 0x3000UL,
            NabuOwnRead32(Camcc, 0x3000UL) & ~1UL);
        NabuOwnWrite32(Camcc, 0x3038UL, 0);
        for (i = 1; i < ARRAYSIZE(NabuOwnPll3Offsets); ++i) {
            if (NabuOwnPll3Offsets[i] != 0x38UL) {
                NabuOwnWrite32(
                    Camcc, 0x3000UL + NabuOwnPll3Offsets[i],
                    State->Pll3[i]);
            }
        }
        NabuOwnWrite32(Camcc, 0x3038UL, State->Pll3[12]);
        NabuOwnWrite32(Camcc, 0x3000UL, State->Pll3[0]);
        KeMemoryBarrier();
    }

    if ((State->MainCbcr & NABU_CBCR_ENABLE) != 0) {
        NabuOwnWrite32(
            Camcc, NABU_IFE0_CLK_CBCR,
            NabuOwnRead32(Camcc, NABU_IFE0_CLK_CBCR) |
                NABU_CBCR_ENABLE);
    }

    if ((State->Gdsc & 1UL) != 0) {
        NabuOwnWrite32(
            Camcc, NABU_IFE0_GDSCR,
            NabuOwnRead32(Camcc, NABU_IFE0_GDSCR) | 1UL);
        ok = NabuOwnWaitMask(
            Camcc, NABU_IFE0_GDSCR_STATUS,
            NABU_GDSC_POWER_UP, FALSE, 2000, &value) && ok;
    }

    ok = ((NabuOwnRead32(Camcc, NABU_IFE0_CPHY_RX_CBCR) &
           NABU_CBCR_ENABLE) ==
          (State->CphyCbcr & NABU_CBCR_ENABLE)) && ok;
    ok = ((NabuOwnRead32(Camcc, NABU_IFE0_CSID_CBCR) &
           NABU_CBCR_ENABLE) ==
          (State->CsidCbcr & NABU_CBCR_ENABLE)) && ok;
    ok = ((NabuOwnRead32(Camcc, NABU_IFE0_AXI_CBCR) &
           NABU_CBCR_ENABLE) ==
          (State->AxiCbcr & NABU_CBCR_ENABLE)) && ok;
    ok = ((NabuOwnRead32(Camcc, NABU_IFE0_CLK_CBCR) &
           NABU_CBCR_ENABLE) ==
          (State->MainCbcr & NABU_CBCR_ENABLE)) && ok;
    ok = ((NabuOwnRead32(Camcc, NABU_IFE0_GDSCR) & 1UL) ==
          (State->Gdsc & 1UL)) && ok;
    return ok;
}

static ULONG
NabuOwnEnableBranch(_In_ PUCHAR Camcc, _In_ ULONG Offset)
{
    ULONG value;
    ULONG elapsed;

    NabuOwnWrite32(Camcc, Offset, NabuOwnRead32(Camcc, Offset) | 1UL);
    for (elapsed = 0; elapsed < 5000; elapsed += 10) {
        value = NabuOwnRead32(Camcc, Offset);
        if ((value & 0x80000000UL) == 0) {
            return value;
        }
        KeStallExecutionProcessor(10);
    }
    return NabuOwnRead32(Camcc, Offset);
}

static BOOLEAN
NabuOwnEnableFrontPhyClocks(
    _Inout_ PNABU_OWN_DEVICE_CONTEXT Context,
    _In_ PUCHAR Camcc)
{
    ULONG value;
    ULONG wait;
    BOOLEAN updatesOk;

    Context->FrontTitanGdscBefore =
        (LONG)NabuOwnRead32(Camcc, NABU_TITAN_TOP_GDSCR);
    NabuOwnWrite32(
        Camcc, NABU_TITAN_TOP_GDSCR,
        NabuOwnReadLong(&Context->FrontTitanGdscBefore) & ~1UL);
    for (wait = 0; wait < 20000; wait += 10) {
        value = NabuOwnRead32(Camcc, NABU_TITAN_TOP_GDSCR + 4);
        if ((value & 0x00010000UL) != 0) {
            break;
        }
        KeStallExecutionProcessor(10);
    }
    Context->FrontTitanGdscAfter =
        (LONG)NabuOwnRead32(Camcc, NABU_TITAN_TOP_GDSCR);
    Context->FrontTitanGdscCfgAfter =
        (LONG)NabuOwnRead32(Camcc, NABU_TITAN_TOP_GDSCR + 4);
    if ((NabuOwnReadLong(&Context->FrontTitanGdscCfgAfter) &
         0x00010000UL) == 0) {
        return FALSE;
    }

    value = NabuOwnRead32(Camcc, 0x0000);
    if ((value & 1UL) == 0 ||
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

        value = (NabuOwnRead32(Camcc, 0x0000) |
                 (1UL << 23)) & ~1UL;
        NabuOwnWrite32(Camcc, 0x0000, value);
        NabuOwnWrite32(Camcc, 0x0038, 0);
        NabuOwnWrite32(Camcc, 0x0000, value | (1UL << 2));
        KeStallExecutionProcessor(10);
        NabuOwnWrite32(Camcc, 0x0038, 1);
        for (wait = 0; wait < 1000; wait += 10) {
            value = NabuOwnRead32(Camcc, 0x0000);
            if ((value & (1UL << 31)) != 0) {
                break;
            }
            KeStallExecutionProcessor(10);
        }
        if ((value & (1UL << 31)) != 0) {
            NabuOwnWrite32(
                Camcc, 0x000C,
                NabuOwnRead32(Camcc, 0x000C) | 7UL);
            NabuOwnWrite32(
                Camcc, 0x0000,
                NabuOwnRead32(Camcc, 0x0000) | 1UL);
            KeStallExecutionProcessor(10);
        }
    }
    Context->FrontPll0LockOk =
        (NabuOwnRead32(Camcc, 0x0000) &
         (1UL << 31)) != 0 ? 1 : 0;
    if (NabuOwnReadLong(&Context->FrontPll0LockOk) == 0) {
        return FALSE;
    }

    NabuOwnWrite32(
        Camcc, NABU_CSI1PHYTIMER_CBCR,
        NabuOwnRead32(Camcc, NABU_CSI1PHYTIMER_CBCR) & ~1UL);
    NabuOwnWrite32(
        Camcc, NABU_CSIPHY1_CBCR,
        NabuOwnRead32(Camcc, NABU_CSIPHY1_CBCR) & ~1UL);

    NabuOwnWrite32(Camcc, NABU_CPHY_RX_CFG_RCGR, 0x301);
    NabuOwnWrite32(
        Camcc, NABU_CPHY_RX_CMD_RCGR,
        NabuOwnRead32(Camcc, NABU_CPHY_RX_CMD_RCGR) | 3UL);
    updatesOk = NabuOwnWaitRcgr(Camcc, NABU_CPHY_RX_CMD_RCGR);

    NabuOwnWrite32(Camcc, NABU_CSI1PHYTIMER_CFG_RCGR, 0x107);
    NabuOwnWrite32(
        Camcc, NABU_CSI1PHYTIMER_CMD_RCGR,
        NabuOwnRead32(Camcc, NABU_CSI1PHYTIMER_CMD_RCGR) | 3UL);
    updatesOk =
        NabuOwnWaitRcgr(Camcc, NABU_CSI1PHYTIMER_CMD_RCGR) &&
        updatesOk;

    Context->FrontPhyTimerCbcr =
        (LONG)NabuOwnEnableBranch(Camcc, NABU_CSI1PHYTIMER_CBCR);
    Context->FrontPhyClockCbcr =
        (LONG)NabuOwnEnableBranch(Camcc, NABU_CSIPHY1_CBCR);
    Context->FrontCphyRxCmd =
        (LONG)NabuOwnRead32(Camcc, NABU_CPHY_RX_CMD_RCGR);
    Context->FrontCphyRxCfg =
        (LONG)NabuOwnRead32(Camcc, NABU_CPHY_RX_CFG_RCGR);
    Context->FrontPhyTimerCmd =
        (LONG)NabuOwnRead32(Camcc, NABU_CSI1PHYTIMER_CMD_RCGR);
    Context->FrontPhyTimerCfg =
        (LONG)NabuOwnRead32(Camcc, NABU_CSI1PHYTIMER_CFG_RCGR);
    Context->FrontClockTreeReady =
        (updatesOk &&
         (NabuOwnReadLong(&Context->FrontPhyTimerCbcr) &
          0x80000000UL) == 0 &&
         (NabuOwnReadLong(&Context->FrontPhyClockCbcr) &
          0x80000000UL) == 0) ? 1 : 0;
    return NabuOwnReadLong(&Context->FrontClockTreeReady) != 0;
}

static VOID
NabuOwnDisableFrontPhyClocks(_In_ PUCHAR Camcc)
{
    NabuOwnWrite32(
        Camcc, NABU_CSIPHY1_CBCR,
        NabuOwnRead32(Camcc, NABU_CSIPHY1_CBCR) & ~1UL);
    NabuOwnWrite32(
        Camcc, NABU_CSI1PHYTIMER_CBCR,
        NabuOwnRead32(Camcc, NABU_CSI1PHYTIMER_CBCR) & ~1UL);
}

typedef struct _NABU_PHY_REG {
    USHORT Offset;
    UCHAR Value;
    UCHAR IsSettle;
} NABU_PHY_REG;

static const NABU_PHY_REG NabuOwnPhyData0[] = {
    {0x004,0x0C,0},{0x02C,0x01,0},{0x034,0x0F,0},{0x01C,0x0A,0},
    {0x014,0x60,0},{0x03C,0xB8,0},{0x000,0x90,0},{0x008,0x0E,1},
    {0x010,0x52,0},{0x038,0xFE,0},{0x060,0x00,0},{0x064,0x7F,0}
};

static const NABU_PHY_REG NabuOwnPhyClock[] = {
    {0x704,0x0C,0},{0x72C,0x01,0},{0x734,0x0F,0},{0x71C,0x0A,0},
    {0x714,0x60,0},{0x728,0x04,0},{0x73C,0xB8,0},{0x700,0x80,0},
    {0x708,0x0E,1},{0x70C,0xA5,0},{0x710,0x52,0},{0x738,0x1F,0},
    {0x760,0x00,0},{0x764,0x7F,0}
};

static const NABU_PHY_REG NabuOwnPhyData1[] = {
    {0x204,0x0C,0},{0x22C,0x01,0},{0x234,0x0F,0},{0x21C,0x0A,0},
    {0x214,0x60,0},{0x23C,0xB8,0},{0x200,0x90,0},{0x208,0x0E,1},
    {0x210,0x52,0},{0x238,0xFE,0},{0x260,0x00,0},{0x264,0x7F,0}
};

static VOID
NabuOwnWritePhyTable(
    _In_ PUCHAR Base,
    _In_reads_(Count) const NABU_PHY_REG *Table,
    _In_ ULONG Count)
{
    ULONG index;

    for (index = 0; index < Count; ++index) {
        NabuOwnWrite32(
            Base, Table[index].Offset,
            Table[index].IsSettle != 0 ?
                NABU_FRONT_SETTLE_COUNT : Table[index].Value);
    }
}

static ULONG
NabuOwnReadFrontPhyVersion(_In_ PUCHAR Base)
{
    ULONG value;

    NabuOwnWrite32(Base, 0x818, 2);
    value = NabuOwnRead32(Base, 0x8E0);
    value |= NabuOwnRead32(Base, 0x8E4) << 8;
    value |= NabuOwnRead32(Base, 0x8E8) << 16;
    value |= NabuOwnRead32(Base, 0x8EC) << 24;
    return value;
}

static VOID
NabuOwnClearFrontPhyIrqs(_In_ PUCHAR Base)
{
    ULONG index;
    ULONG status;

    for (index = 0; index < 11; ++index) {
        status = NabuOwnRead32(Base, 0x8B0 + index * 4);
        NabuOwnWrite32(Base, 0x858 + index * 4, status);
        NabuOwnWrite32(Base, 0x858 + index * 4, 0);
    }
    NabuOwnWrite32(Base, 0x828, 1);
    NabuOwnWrite32(Base, 0x828, 0);
}

static VOID
NabuOwnConfigureFrontPhy(
    _Inout_ PNABU_OWN_DEVICE_CONTEXT Context,
    _In_ PUCHAR Base)
{
    static const UCHAR irqMasks[11] = {
        0xFF, 0xFF, 0xFB, 0xFF, 0x7F, 0xFF,
        0xFF, 0xEF, 0xFF, 0xFF, 0xFF
    };
    ULONG index;

    Context->FrontPhyLaneMask = NABU_FRONT_LANE_MASK;
    Context->FrontPhyLaneEnable = NABU_FRONT_LANE_ENABLE;
    Context->FrontPhySettleCount = NABU_FRONT_SETTLE_COUNT;

    NabuOwnWrite32(Base, 0x814, NABU_FRONT_LANE_ENABLE);
    NabuOwnWrite32(Base, 0x818, 1);
    NabuOwnWrite32(Base, 0x81C, 2);
    NabuOwnWrite32(Base, 0x800, 1);
    KeStallExecutionProcessor(1000);
    NabuOwnWrite32(Base, 0x800, 0);

    NabuOwnWritePhyTable(
        Base, NabuOwnPhyData0, RTL_NUMBER_OF(NabuOwnPhyData0));
    NabuOwnWritePhyTable(
        Base, NabuOwnPhyClock, RTL_NUMBER_OF(NabuOwnPhyClock));
    NabuOwnWritePhyTable(
        Base, NabuOwnPhyData1, RTL_NUMBER_OF(NabuOwnPhyData1));
    for (index = 0; index < RTL_NUMBER_OF(irqMasks); ++index) {
        NabuOwnWrite32(Base, 0x82C + index * 4, irqMasks[index]);
    }
}

static VOID
NabuOwnDisableFrontPhy(_In_ PUCHAR Base)
{
    NabuOwnWrite32(Base, 0x814, 0);
    KeStallExecutionProcessor(80000);
    NabuOwnWrite32(Base, 0x818, 0);
    NabuOwnWrite32(Base, 0x81C, 0);
}

static VOID
NabuOwnBuildCciRead(
    _Out_writes_bytes_(NABU_CCI_READ_BYTES) UCHAR *Request,
    _In_ PVOID Data)
{
    ULONG frequencyKHz = 400;
    UCHAR slaveAddress = 0x20;
    ULONG addressType = 2;
    USHORT registerAddress = 0x300b;
    USHORT length = 2;
    ULONG master = NABU_FRONT_CCI_MASTER;
    ULONG_PTR dataAddress = (ULONG_PTR)Data;

    RtlZeroMemory(Request, NABU_CCI_READ_BYTES);
    RtlCopyMemory(Request + 0x04, &frequencyKHz, sizeof(frequencyKHz));
    RtlCopyMemory(Request + 0x08, &slaveAddress, sizeof(slaveAddress));
    RtlCopyMemory(Request + 0x0c, &addressType, sizeof(addressType));
    RtlCopyMemory(Request + 0x18, &dataAddress, sizeof(dataAddress));
    RtlCopyMemory(Request + 0x28, &registerAddress,
                  sizeof(registerAddress));
    RtlCopyMemory(Request + 0x2a, &length, sizeof(length));
    RtlCopyMemory(Request + 0x3c, &master, sizeof(master));
}

static VOID
NabuOwnBuildSensorRead(
    _Out_writes_bytes_(NABU_CCI_READ_BYTES) UCHAR *Request,
    _In_ PVOID Data,
    _In_ USHORT Register,
    _In_ USHORT Length)
{
    ULONG frequencyKHz = 400;
    UCHAR slaveAddress = 0x20;
    ULONG addressType = 2;
    ULONG master = NABU_FRONT_CCI_MASTER;
    ULONG_PTR dataAddress = (ULONG_PTR)Data;

    RtlZeroMemory(Request, NABU_CCI_READ_BYTES);
    RtlCopyMemory(Request + 0x04, &frequencyKHz, sizeof(frequencyKHz));
    RtlCopyMemory(Request + 0x08, &slaveAddress, sizeof(slaveAddress));
    RtlCopyMemory(Request + 0x0c, &addressType, sizeof(addressType));
    RtlCopyMemory(Request + 0x18, &dataAddress, sizeof(dataAddress));
    RtlCopyMemory(Request + 0x28, &Register, sizeof(Register));
    RtlCopyMemory(Request + 0x2a, &Length, sizeof(Length));
    RtlCopyMemory(Request + 0x3c, &master, sizeof(master));
}

static VOID
NabuOwnBuildSensorWrite(
    _Out_writes_bytes_(NABU_CCI_READ_BYTES) UCHAR *Request,
    _In_ PVOID Data,
    _In_ USHORT Register,
    _In_ USHORT Length)
{
    ULONG frequencyKHz = 400;
    UCHAR slaveAddress = 0x20;
    ULONG addressType = 2;
    USHORT count = 1;
    ULONG master = NABU_FRONT_CCI_MASTER;
    ULONG_PTR dataAddress = (ULONG_PTR)Data;

    RtlZeroMemory(Request, NABU_CCI_READ_BYTES);
    RtlCopyMemory(Request + 0x04, &frequencyKHz, sizeof(frequencyKHz));
    RtlCopyMemory(Request + 0x08, &slaveAddress, sizeof(slaveAddress));
    RtlCopyMemory(Request + 0x0c, &addressType, sizeof(addressType));
    RtlCopyMemory(Request + 0x18, &dataAddress, sizeof(dataAddress));
    RtlCopyMemory(Request + 0x20, &dataAddress, sizeof(dataAddress));
    RtlCopyMemory(Request + 0x28, &Register, sizeof(Register));
    RtlCopyMemory(Request + 0x2a, &Length, sizeof(Length));
    RtlCopyMemory(Request + 0x2c, &count, sizeof(count));
    RtlCopyMemory(Request + 0x3c, &master, sizeof(master));
}

static NTSTATUS
NabuOwnSensorWriteRetry(
    _In_ PPLATFORM_CLIENT Client,
    _In_ USHORT Register,
    _In_ UCHAR Value)
{
    UCHAR request[NABU_CCI_READ_BYTES];
    ULONG attempt;
    NTSTATUS status = STATUS_UNSUCCESSFUL;

    for (attempt = 0; attempt < 5; ++attempt) {
        NabuOwnBuildSensorWrite(
            request, &Value, Register, sizeof(Value));
        status = PlatformDispatch(
            Client, PLAT_CMD_I2C_WRITE, request, NULL, NULL);
        if (NT_SUCCESS(status)) {
            break;
        }
        KeStallExecutionProcessor(1000);
    }
    return status;
}

static NTSTATUS
NabuOwnSensorReadRetry(
    _In_ PPLATFORM_CLIENT Client,
    _In_ USHORT Register,
    _Out_writes_bytes_(Length) PUCHAR Buffer,
    _In_ USHORT Length)
{
    UCHAR request[NABU_CCI_READ_BYTES];
    ULONG attempt;
    NTSTATUS status = STATUS_UNSUCCESSFUL;

    for (attempt = 0; attempt < 5; ++attempt) {
        RtlZeroMemory(Buffer, Length);
        NabuOwnBuildSensorRead(request, Buffer, Register, Length);
        status = PlatformDispatch(
            Client, PLAT_CMD_I2C_READ, NULL, NULL, request);
        if (NT_SUCCESS(status)) {
            break;
        }
        KeStallExecutionProcessor(1000);
    }
    return status;
}

static BOOLEAN
NabuOwnEnableFrontRails(_Inout_ PNABU_OWN_DEVICE_CONTEXT Context)
{
    SPMI_ARB_CONTEXT spmi;
    PHYSICAL_ADDRESS address;
    ULONG apid = 0;
    ULONG writeEe = 0;
    ULONG type = 0;
    ULONG subtype = 0;
    ULONG vsetLb = 0;
    ULONG vsetUb = 0;
    ULONG mode = 0;
    ULONG output = 0;
    ULONG enable = 0;
    ULONG registerValue = 0;
    BOOLEAN vioReady = FALSE;
    BOOLEAN vdigReady = FALSE;
    NTSTATUS status;

    RtlZeroMemory(&spmi, sizeof(spmi));

    address.QuadPart = SPMI_ARB_CORE_PHYS_ADDR;
    spmi.CoreBase = (PUCHAR)MmMapIoSpaceEx(
        address, SPMI_ARB_CORE_REGION_SIZE,
        PAGE_READWRITE | PAGE_NOCACHE);
    address.QuadPart = SPMI_ARB_CNFG_PHYS_ADDR;
    spmi.CnfgBase = (PUCHAR)MmMapIoSpaceEx(
        address, SPMI_ARB_CNFG_REGION_SIZE,
        PAGE_READWRITE | PAGE_NOCACHE);
    address.QuadPart = SPMI_ARB_OBSRVR_PHYS_ADDR;
    spmi.ObsrvrBase = (PUCHAR)MmMapIoSpaceEx(
        address, SPMI_ARB_OBSRVR_REGION_SIZE,
        PAGE_READWRITE | PAGE_NOCACHE);
    address.QuadPart = SPMI_ARB_CHNLS_PHYS_ADDR;
    spmi.ChnlsBase = (PUCHAR)MmMapIoSpaceEx(
        address, SPMI_ARB_CHNLS_REGION_SIZE,
        PAGE_READWRITE | PAGE_NOCACHE);
    if (spmi.CoreBase == NULL || spmi.CnfgBase == NULL ||
        spmi.ObsrvrBase == NULL || spmi.ChnlsBase == NULL) {
        Context->FrontSpmiInitStatus =
            (LONG)STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }

    status = SpmiArbInitialize(&spmi);
    Context->FrontSpmiInitStatus = (LONG)status;
    if (!NT_SUCCESS(status)) {
        goto Cleanup;
    }

    status = SpmiArbFindApid(
        &spmi,
        (UCHAR)SPMI_PM8150L_REG_SID,
        (UCHAR)SPMI_PM8150L_LDO1_PID,
        &apid,
        &writeEe);
    if (NT_SUCCESS(status)) {
        (VOID)SpmiArbReadPeripheral(
            &spmi, (UCHAR)SPMI_PM8150L_REG_SID,
            (UCHAR)SPMI_PM8150L_LDO1_PID,
            (UCHAR)SPMI_VREG_REG_TYPE, SPMI_OUR_EE,
            apid, &type, 1);
        (VOID)SpmiArbReadPeripheral(
            &spmi, (UCHAR)SPMI_PM8150L_REG_SID,
            (UCHAR)SPMI_PM8150L_LDO1_PID,
            (UCHAR)SPMI_VREG_REG_VSET_LB, SPMI_OUR_EE,
            apid, &vsetLb, 1);
        (VOID)SpmiArbReadPeripheral(
            &spmi, (UCHAR)SPMI_PM8150L_REG_SID,
            (UCHAR)SPMI_PM8150L_LDO1_PID,
            (UCHAR)SPMI_VREG_REG_VSET_UB, SPMI_OUR_EE,
            apid, &vsetUb, 1);
        (VOID)SpmiArbReadPeripheral(
            &spmi, (UCHAR)SPMI_PM8150L_REG_SID,
            (UCHAR)SPMI_PM8150L_LDO1_PID,
            (UCHAR)SPMI_VREG_REG_EN_CTL, SPMI_OUR_EE,
            apid, &enable, 1);
        if (type == SPMI_VREG_TYPE_LDO &&
            ((vsetUb << 8) | vsetLb) == SPMI_VREG_EXPECTED_VIO_MV &&
            writeEe == SPMI_OUR_EE &&
            (enable & SPMI_VREG_EN_BIT) == 0) {
            (VOID)SpmiArbWritePeripheral(
                &spmi, (UCHAR)SPMI_PM8150L_REG_SID,
                (UCHAR)SPMI_PM8150L_LDO1_PID,
                (UCHAR)SPMI_VREG_REG_EN_CTL,
                apid, SPMI_VREG_EN_BIT, 1);
            KeStallExecutionProcessor(2000);
        }
        registerValue = 0;
        (VOID)SpmiArbReadPeripheral(
            &spmi, (UCHAR)SPMI_PM8150L_REG_SID,
            (UCHAR)SPMI_PM8150L_LDO1_PID,
            (UCHAR)SPMI_VREG_REG_EN_CTL, SPMI_OUR_EE,
            apid, &registerValue, 1);
        Context->FrontVioEnAfter = (LONG)registerValue;
        registerValue = 0;
        (VOID)SpmiArbReadPeripheral(
            &spmi, (UCHAR)SPMI_PM8150L_REG_SID,
            (UCHAR)SPMI_PM8150L_LDO1_PID,
            (UCHAR)SPMI_VREG_REG_STATUS1, SPMI_OUR_EE,
            apid, &registerValue, 1);
        Context->FrontVioStatusAfter = (LONG)registerValue;
        vioReady =
            (NabuOwnReadLong(&Context->FrontVioEnAfter) &
             SPMI_VREG_EN_BIT) != 0 &&
            (NabuOwnReadLong(&Context->FrontVioStatusAfter) &
             0x80UL) != 0;
    }

    apid = 0;
    writeEe = 0;
    type = 0;
    status = SpmiArbFindApid(
        &spmi,
        (UCHAR)SPMI_PM8150L_SID,
        (UCHAR)SPMI_PM8150L_GPIO3_PID,
        &apid,
        &writeEe);
    if (NT_SUCCESS(status)) {
        (VOID)SpmiArbReadPeripheral(
            &spmi, (UCHAR)SPMI_PM8150L_SID,
            (UCHAR)SPMI_PM8150L_GPIO3_PID,
            (UCHAR)SPMI_PMIC_GPIO_REG_TYPE, SPMI_OUR_EE,
            apid, &type, 1);
        (VOID)SpmiArbReadPeripheral(
            &spmi, (UCHAR)SPMI_PM8150L_SID,
            (UCHAR)SPMI_PM8150L_GPIO3_PID,
            (UCHAR)SPMI_PMIC_GPIO_REG_SUBTYPE, SPMI_OUR_EE,
            apid, &subtype, 1);
        (VOID)SpmiArbReadPeripheral(
            &spmi, (UCHAR)SPMI_PM8150L_SID,
            (UCHAR)SPMI_PM8150L_GPIO3_PID,
            (UCHAR)SPMI_PMIC_GPIO_REG_MODE_CTL, SPMI_OUR_EE,
            apid, &mode, 1);
        (VOID)SpmiArbReadPeripheral(
            &spmi, (UCHAR)SPMI_PM8150L_SID,
            (UCHAR)SPMI_PM8150L_GPIO3_PID,
            (UCHAR)SPMI_PMIC_GPIO_REG_DIG_OUT_SRC, SPMI_OUR_EE,
            apid, &output, 1);
        (VOID)SpmiArbReadPeripheral(
            &spmi, (UCHAR)SPMI_PM8150L_SID,
            (UCHAR)SPMI_PM8150L_GPIO3_PID,
            (UCHAR)SPMI_PMIC_GPIO_REG_EN_CTL, SPMI_OUR_EE,
            apid, &enable, 1);
        if (type == SPMI_PMIC_GPIO_TYPE_GPIO &&
            (subtype == SPMI_PMIC_GPIO_SUBTYPE_LV ||
             subtype == SPMI_PMIC_GPIO_SUBTYPE_MV) &&
            writeEe == SPMI_OUR_EE) {
            if ((mode & 3UL) != 1UL) {
                (VOID)SpmiArbWritePeripheral(
                    &spmi, (UCHAR)SPMI_PM8150L_SID,
                    (UCHAR)SPMI_PM8150L_GPIO3_PID,
                    (UCHAR)SPMI_PMIC_GPIO_REG_MODE_CTL,
                    apid, 1, 1);
            }
            if (output != SPMI_PMIC_GPIO_LV_MV_OUT_HIGH) {
                (VOID)SpmiArbWritePeripheral(
                    &spmi, (UCHAR)SPMI_PM8150L_SID,
                    (UCHAR)SPMI_PM8150L_GPIO3_PID,
                    (UCHAR)SPMI_PMIC_GPIO_REG_DIG_OUT_SRC,
                    apid, SPMI_PMIC_GPIO_LV_MV_OUT_HIGH, 1);
            }
            if ((enable & SPMI_PMIC_GPIO_EN_MASTER_BIT) == 0) {
                (VOID)SpmiArbWritePeripheral(
                    &spmi, (UCHAR)SPMI_PM8150L_SID,
                    (UCHAR)SPMI_PM8150L_GPIO3_PID,
                    (UCHAR)SPMI_PMIC_GPIO_REG_EN_CTL,
                    apid, SPMI_PMIC_GPIO_EN_MASTER_BIT, 1);
            }
            KeStallExecutionProcessor(2000);
        }
        registerValue = 0;
        (VOID)SpmiArbReadPeripheral(
            &spmi, (UCHAR)SPMI_PM8150L_SID,
            (UCHAR)SPMI_PM8150L_GPIO3_PID,
            (UCHAR)SPMI_PMIC_GPIO_REG_MODE_CTL, SPMI_OUR_EE,
            apid, &registerValue, 1);
        Context->FrontVdigModeAfter = (LONG)registerValue;
        registerValue = 0;
        (VOID)SpmiArbReadPeripheral(
            &spmi, (UCHAR)SPMI_PM8150L_SID,
            (UCHAR)SPMI_PM8150L_GPIO3_PID,
            (UCHAR)SPMI_PMIC_GPIO_REG_DIG_OUT_SRC, SPMI_OUR_EE,
            apid, &registerValue, 1);
        Context->FrontVdigOutAfter = (LONG)registerValue;
        registerValue = 0;
        (VOID)SpmiArbReadPeripheral(
            &spmi, (UCHAR)SPMI_PM8150L_SID,
            (UCHAR)SPMI_PM8150L_GPIO3_PID,
            (UCHAR)SPMI_PMIC_GPIO_REG_EN_CTL, SPMI_OUR_EE,
            apid, &registerValue, 1);
        Context->FrontVdigEnAfter = (LONG)registerValue;
        vdigReady =
            (NabuOwnReadLong(&Context->FrontVdigModeAfter) & 3UL) == 1UL &&
            NabuOwnReadLong(&Context->FrontVdigOutAfter) ==
                SPMI_PMIC_GPIO_LV_MV_OUT_HIGH &&
            (NabuOwnReadLong(&Context->FrontVdigEnAfter) &
             SPMI_PMIC_GPIO_EN_MASTER_BIT) != 0;
    }

Cleanup:
    Context->FrontRailsReady = (vioReady && vdigReady) ? 1 : 0;
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
    return Context->FrontRailsReady != 0;
}

static BOOLEAN
NabuOwnPrepareFrontSensor(_Inout_ PNABU_OWN_DEVICE_CONTEXT Context)
{
    PHYSICAL_ADDRESS address;
    PUCHAR camcc = NULL;
    PUCHAR north = NULL;
    PUCHAR east = NULL;
    ULONG value;
    ULONG attempt;
    BOOLEAN railsReady;
    BOOLEAN clockReady = FALSE;

    railsReady = NabuOwnEnableFrontRails(Context);

    address.QuadPart = NABU_CAMCC_PHYS;
    camcc = (PUCHAR)MmMapIoSpaceEx(
        address, NABU_CAMCC_BYTES, PAGE_READWRITE | PAGE_NOCACHE);
    address.QuadPart = NABU_TLMM_NORTH_PHYS;
    north = (PUCHAR)MmMapIoSpaceEx(
        address, NABU_TLMM_NORTH_BYTES, PAGE_READWRITE | PAGE_NOCACHE);
    address.QuadPart = NABU_TLMM_EAST_PHYS;
    east = (PUCHAR)MmMapIoSpaceEx(
        address, NABU_TLMM_EAST_BYTES, PAGE_READWRITE | PAGE_NOCACHE);
    if (camcc == NULL || north == NULL || east == NULL) {
        goto Cleanup;
    }

    NabuOwnWrite32(camcc, NABU_MCLK1_CFG_RCGR, 1);
    NabuOwnWrite32(
        camcc,
        NABU_MCLK1_CMD_RCGR,
        NabuOwnRead32(camcc, NABU_MCLK1_CMD_RCGR) | 3UL);
    for (attempt = 0; attempt < 1000; ++attempt) {
        if ((NabuOwnRead32(camcc, NABU_MCLK1_CMD_RCGR) & 1UL) == 0) {
            break;
        }
        KeStallExecutionProcessor(10);
    }
    NabuOwnWrite32(
        camcc,
        NABU_MCLK1_CBCR,
        NabuOwnRead32(camcc, NABU_MCLK1_CBCR) | 1UL);
    for (attempt = 0; attempt < 1000; ++attempt) {
        if ((NabuOwnRead32(camcc, NABU_MCLK1_CBCR) &
             0x80000000UL) == 0) {
            break;
        }
        KeStallExecutionProcessor(10);
    }
    Context->FrontMclk1Cmd =
        (LONG)NabuOwnRead32(camcc, NABU_MCLK1_CMD_RCGR);
    Context->FrontMclk1Cfg =
        (LONG)NabuOwnRead32(camcc, NABU_MCLK1_CFG_RCGR);
    Context->FrontMclk1Cbcr =
        (LONG)NabuOwnRead32(camcc, NABU_MCLK1_CBCR);
    clockReady =
        (NabuOwnReadLong(&Context->FrontMclk1Cmd) & 1UL) == 0 &&
        (NabuOwnReadLong(&Context->FrontMclk1Cbcr) &
         0x80000000UL) == 0;

    value = NabuOwnRead32(
        north, NABU_TLMM_CTL(NABU_FRONT_VANA_GPIO));
    value = (value & ~(7UL << 2)) | (1UL << 9);
    NabuOwnWrite32(
        north, NABU_TLMM_CTL(NABU_FRONT_VANA_GPIO), value);
    NabuOwnWrite32(
        north,
        NABU_TLMM_IO(NABU_FRONT_VANA_GPIO),
        NabuOwnRead32(north, NABU_TLMM_IO(NABU_FRONT_VANA_GPIO)) |
            2UL);

    value = NabuOwnRead32(
        north, NABU_TLMM_CTL(NABU_FRONT_MCLK_GPIO));
    value &= ~(7UL << 2);
    value |= (1UL << 2);
    value &= ~3UL;
    NabuOwnWrite32(
        north, NABU_TLMM_CTL(NABU_FRONT_MCLK_GPIO), value);

    value = NabuOwnRead32(
        east, NABU_TLMM_CTL(NABU_FRONT_RESET_GPIO));
    value = (value & ~(7UL << 2)) | (1UL << 9);
    NabuOwnWrite32(
        east, NABU_TLMM_CTL(NABU_FRONT_RESET_GPIO), value);
    NabuOwnWrite32(
        east,
        NABU_TLMM_IO(NABU_FRONT_RESET_GPIO),
        NabuOwnRead32(east, NABU_TLMM_IO(NABU_FRONT_RESET_GPIO)) &
            ~2UL);
    KeStallExecutionProcessor(2000);
    NabuOwnWrite32(
        east,
        NABU_TLMM_IO(NABU_FRONT_RESET_GPIO),
        NabuOwnRead32(east, NABU_TLMM_IO(NABU_FRONT_RESET_GPIO)) |
            2UL);
    KeStallExecutionProcessor(50000);
    Context->FrontResetIo =
        (LONG)NabuOwnRead32(
            east, NABU_TLMM_IO(NABU_FRONT_RESET_GPIO));

Cleanup:
    Context->FrontPrepareReady =
        (railsReady && clockReady) ? 1 : 0;
    if (east != NULL) {
        MmUnmapIoSpace(east, NABU_TLMM_EAST_BYTES);
    }
    if (north != NULL) {
        MmUnmapIoSpace(north, NABU_TLMM_NORTH_BYTES);
    }
    if (camcc != NULL) {
        MmUnmapIoSpace(camcc, NABU_CAMCC_BYTES);
    }
    return Context->FrontPrepareReady != 0;
}

static VOID
NabuOwnSetFrontPhase(
    _Inout_ PNABU_OWN_DEVICE_CONTEXT Context,
    _In_ LONG Phase)
{
    InterlockedExchange(&Context->FrontIdPhase, Phase);
    NabuOwnWriteDiagnostics(Context);
}

static VOID
NabuOwnResetFrontStatus(_Inout_ PNABU_OWN_DEVICE_CONTEXT Context)
{
    Context->FrontIdCompleted = 0;
    Context->FrontPlatformOpenStatus = (LONG)STATUS_NOT_SUPPORTED;
    Context->FrontAcquire1Status = (LONG)STATUS_NOT_SUPPORTED;
    Context->FrontPowerStatus = (LONG)STATUS_NOT_SUPPORTED;
    Context->FrontPinStatus = (LONG)STATUS_NOT_SUPPORTED;
    Context->FrontInitMasterStatus = (LONG)STATUS_NOT_SUPPORTED;
    Context->FrontAcquire2Status = (LONG)STATUS_NOT_SUPPORTED;
    Context->FrontRailsReady = 0;
    Context->FrontSpmiInitStatus = (LONG)STATUS_NOT_SUPPORTED;
    Context->FrontVioEnAfter = 0;
    Context->FrontVioStatusAfter = 0;
    Context->FrontVdigModeAfter = 0;
    Context->FrontVdigOutAfter = 0;
    Context->FrontVdigEnAfter = 0;
    Context->FrontPrepareReady = 0;
    Context->FrontMclk1Cmd = 0;
    Context->FrontMclk1Cfg = 0;
    Context->FrontMclk1Cbcr = 0;
    Context->FrontResetIo = 0;
    Context->FrontCciReadStatus = (LONG)STATUS_NOT_SUPPORTED;
    Context->FrontCciReadAttemptCount = 0;
    Context->FrontCciRxByte0 = 0;
    Context->FrontCciRxByte1 = 0;
    Context->FrontCciChipId = 0;
    Context->FrontCciIdMatchesExpected = 0;
    Context->FrontDeinitStatus = (LONG)STATUS_NOT_SUPPORTED;
    Context->FrontRelease1Status = (LONG)STATUS_NOT_SUPPORTED;
    Context->FrontRelease2Status = (LONG)STATUS_NOT_SUPPORTED;
    Context->FrontProgramAttempted = 0;
    Context->FrontProgramPhase = 0;
    Context->FrontProgramCompleted = 0;
    Context->FrontInitStatus = (LONG)STATUS_NOT_SUPPORTED;
    Context->FrontInitFailIndex = (LONG)MAXULONG;
    Context->FrontInitFailReg = 0;
    Context->FrontModeStatus = (LONG)STATUS_NOT_SUPPORTED;
    Context->FrontModeFailIndex = (LONG)MAXULONG;
    Context->FrontModeFailReg = 0;
    Context->FrontGeometryStatus = (LONG)STATUS_NOT_SUPPORTED;
    Context->FrontWidth = 0;
    Context->FrontHeight = 0;
    Context->FrontStreamWriteStatus = (LONG)STATUS_NOT_SUPPORTED;
    Context->FrontStreamReadStatus = (LONG)STATUS_NOT_SUPPORTED;
    Context->FrontStreamReadback = 0;
    Context->FrontStreamStopStatus = (LONG)STATUS_NOT_SUPPORTED;
    Context->FrontProgramLastTable = 0;
    Context->FrontProgramLastIndex = 0;
    Context->FrontProgramLastReg = 0;
    Context->FrontProgramLastStatus = (LONG)STATUS_NOT_SUPPORTED;
    Context->FrontRtBandwidthStatus = (LONG)STATUS_NOT_SUPPORTED;
    Context->FrontRtBandwidthReleaseStatus =
        (LONG)STATUS_NOT_SUPPORTED;
    Context->FrontTitanGdscBefore = 0;
    Context->FrontTitanGdscAfter = 0;
    Context->FrontTitanGdscCfgAfter = 0;
    Context->FrontPll0LockOk = 0;
    Context->FrontCphyRxCmd = 0;
    Context->FrontCphyRxCfg = 0;
    Context->FrontPhyTimerCmd = 0;
    Context->FrontPhyTimerCfg = 0;
    Context->FrontPhyTimerCbcr = 0;
    Context->FrontPhyClockCbcr = 0;
    Context->FrontClockTreeReady = 0;
    Context->FrontPhyAttempted = 0;
    Context->FrontPhyPhase = 0;
    Context->FrontPhyCompleted = 0;
    Context->FrontPhyHwVersion = 0;
    Context->FrontPhyLaneMask = 0;
    Context->FrontPhyLaneEnable = 0;
    Context->FrontPhySettleCount = 0;
    Context->FrontPhyIrq5Before = 0;
    Context->FrontPhyIrq8Before = 0;
    Context->FrontPhyIrq5After = 0;
    Context->FrontPhyIrq8After = 0;
    Context->FrontPhyActivityDetected = 0;
    Context->FrontCsidAttempted = 0;
    Context->FrontCsidPhase = 0;
    Context->FrontCsidCompleted = 0;
    Context->FrontCsidHwVersion = 0;
    Context->FrontCsidResetTopIrq = 0;
    Context->FrontCsidResetPolls = 0;
    Context->FrontCsidRxCfg0 = 0;
    Context->FrontCsidRxCfg1 = 0;
    Context->FrontCsidRdiCfg0 = 0;
    Context->FrontCsidPacketsBefore = 0;
    Context->FrontCsidPacketsAfter = 0;
    Context->FrontCsidRxIrqAfter = 0;
    Context->FrontCsidRdiIrqAfter = 0;
    Context->FrontCsidTopIrqAfter = 0;
    Context->FrontCsidEccStats = 0;
    Context->FrontCsidCrcErrors = 0;
    Context->FrontCsidLongPkt0 = 0;
    Context->FrontCsidLongPkt1 = 0;
    Context->FrontCsidRdiBytesPing = 0;
    Context->FrontCsidRdiBytesPong = 0;
    Context->FrontCsidRdiStatus = 0;
    Context->FrontCsidPolls = 0;
    Context->FrontCsidRestoreCompleted = 0;
    Context->FrontFrameSofSeen = 0;
    Context->FrontFrameEofSeen = 0;
    Context->FrontFrameComplete = 0;
    Context->FrontFramePacketDelta = 0;
    Context->FrontFrameCaptureCtrl = 0;
    Context->FrontFrameVirtualChannel = 0;
    Context->FrontFrameDataType = 0;
    Context->FrontFrameWordCount = 0;
    Context->FrontFrameLongPacketMatches = 0;
    Context->FrontFrameRxErrorMask = 0;
    Context->FrontFrameRdiErrorMask = 0;
    Context->FrontFrameMeasure0 = 0;
    Context->FrontFrameMeasure1 = 0;
    Context->FrontFrameMeasure2 = 0;
    Context->FrontRawAttempted = 0;
    Context->FrontRawPhase = 0;
    Context->FrontRawCompleted = 0;
    Context->FrontRawBufferAllocStatus = STATUS_NOT_SUPPORTED;
    Context->FrontRawBufferBytes = 0;
    Context->FrontRawSmmuEnumerateStatus = STATUS_NOT_SUPPORTED;
    Context->FrontRawSmmuOpenStatus = STATUS_NOT_SUPPORTED;
    Context->FrontRawSmmuQueryStatus = STATUS_NOT_SUPPORTED;
    Context->FrontRawSmmuGetConfigStatus = STATUS_NOT_SUPPORTED;
    Context->FrontRawSmmuContextCount = 0;
    Context->FrontRawSmmuInterfaceSize = 0;
    Context->FrontRawSmmuInterfaceVersion = 0;
    Context->FrontRawSmmuContextLow = 0;
    Context->FrontRawSmmuContextHigh = 0;
    Context->FrontRawLegacyCallbackPresent = 0;
    Context->FrontRawLegacyCallbackRegistered = 0;
    Context->FrontRawLegacyCallbackRemoved = 0;
    Context->FrontRawIommuGetConfigPresent = 0;
    Context->FrontRawIommuCallbackPresent = 0;
    Context->FrontRawIommuIsPresent = 0;
    Context->FrontRawIommuIsResult = 0;
    Context->FrontRawConfigEntry0Index = MAXLONG;
    Context->FrontRawConfigEntry0HandleLow = 0;
    Context->FrontRawConfigEntry0HandleHigh = 0;
    Context->FrontRawConfigEntry1Index = MAXLONG;
    Context->FrontRawConfigEntry1HandleLow = 0;
    Context->FrontRawConfigEntry1HandleHigh = 0;
    Context->FrontRawConfigEntry2Index = MAXLONG;
    Context->FrontRawConfigEntry2HandleLow = 0;
    Context->FrontRawConfigEntry2HandleHigh = 0;
    Context->FrontRawVcmCreateStatus = STATUS_NOT_SUPPORTED;
    Context->FrontRawVcmAssociationCount = 0;
    Context->FrontRawVcmSetStateStatus = STATUS_NOT_SUPPORTED;
    Context->FrontRawVcmActivationCount = 0;
    Context->FrontRawVcmReserveStatus = STATUS_NOT_SUPPORTED;
    Context->FrontRawVcmBackStatus = STATUS_NOT_SUPPORTED;
    Context->FrontRawIovaLow = 0;
    Context->FrontRawIovaHigh = 0;
    Context->FrontRawPll3LockOk = 0;
    Context->FrontRawVfeHwVersion = 0;
    Context->FrontRawVfeResetStatus = 0;
    Context->FrontRawVfeResetPolls = 0;
    Context->FrontRawWmCfg = 0;
    Context->FrontRawWmImageAddress = 0;
    Context->FrontRawWmStatus0 = 0;
    Context->FrontRawWmStatus1 = 0;
    Context->FrontRawVfeTopIrq0 = 0;
    Context->FrontRawVfeTopIrq1 = 0;
    Context->FrontRawVfeBusIrq0 = 0;
    Context->FrontRawVfeBusIrq1 = 0;
    Context->FrontRawVfeBusIrq2 = 0;
    Context->FrontRawVfeBusDebug0 = 0;
    Context->FrontRawWaitPolls = 0;
    Context->FrontRawWaitExitReason = 0;
    Context->FrontRawBufferSample0 = NABU_FRONT_RAW_MARKER;
    Context->FrontRawBufferSample1 = NABU_FRONT_RAW_MARKER;
    Context->FrontRawBufferSample2 = NABU_FRONT_RAW_MARKER;
    Context->FrontRawBufferSample3 = NABU_FRONT_RAW_MARKER;
    Context->FrontRawBufferTail0 = NABU_FRONT_RAW_MARKER;
    Context->FrontRawBufferTail1 = NABU_FRONT_RAW_MARKER;
    Context->FrontRawBufferChanged = 0;
    Context->FrontRawBufferTailChanged = 0;
    Context->FrontRawFileStatus = STATUS_NOT_SUPPORTED;
    Context->FrontRawFileBytes = 0;
    Context->FrontRawVcmUnbackStatus = STATUS_NOT_SUPPORTED;
    Context->FrontRawVcmReleaseStatus = STATUS_NOT_SUPPORTED;
    Context->FrontRawVcmDeactivateStatus = STATUS_NOT_SUPPORTED;
    Context->FrontRawVcmSetStateOffStatus = STATUS_NOT_SUPPORTED;
    Context->FrontRawVcmDeassocStatus = STATUS_NOT_SUPPORTED;
    Context->FrontRawVcmFreeStatus = STATUS_NOT_SUPPORTED;
}

static VOID
NabuOwnSetProgramCheckpoint(
    _Inout_ PNABU_OWN_DEVICE_CONTEXT Context,
    _In_ LONG Phase,
    _In_ LONG Table,
    _In_ LONG Index,
    _In_ LONG Register,
    _In_ NTSTATUS Status)
{
    InterlockedExchange(&Context->FrontProgramPhase, Phase);
    InterlockedExchange(&Context->FrontProgramLastTable, Table);
    InterlockedExchange(&Context->FrontProgramLastIndex, Index);
    InterlockedExchange(&Context->FrontProgramLastReg, Register);
    InterlockedExchange(&Context->FrontProgramLastStatus, (LONG)Status);
    NabuOwnWriteDiagnostics(Context);
}

static NTSTATUS
NabuOwnWriteSensorTable(
    _Inout_ PNABU_OWN_DEVICE_CONTEXT Context,
    _In_ PPLATFORM_CLIENT Client,
    _In_ LONG Phase,
    _In_ LONG TableId,
    _In_reads_(Count) const NABU_SENSOR_REG8 *Table,
    _In_ ULONG Count,
    _Out_ volatile LONG *FailIndex,
    _Out_ volatile LONG *FailRegister)
{
    ULONG index;
    NTSTATUS status = STATUS_SUCCESS;

    *FailIndex = (LONG)MAXULONG;
    *FailRegister = 0;
    for (index = 0; index < Count; ++index) {
        if ((index & 7UL) == 0) {
            NabuOwnSetProgramCheckpoint(
                Context, Phase, TableId, (LONG)index,
                (LONG)Table[index].Register, STATUS_PENDING);
        }
        status = NabuOwnSensorWriteRetry(
            Client, Table[index].Register, Table[index].Value);
        if (!NT_SUCCESS(status)) {
            *FailIndex = (LONG)index;
            *FailRegister = (LONG)Table[index].Register;
            NabuOwnSetProgramCheckpoint(
                Context, Phase, TableId, (LONG)index,
                (LONG)Table[index].Register, status);
            break;
        }
    }
    if (NT_SUCCESS(status)) {
        NabuOwnSetProgramCheckpoint(
            Context, Phase, TableId, (LONG)Count, 0, STATUS_SUCCESS);
    }
    return status;
}

static NTSTATUS
NabuOwnProgramFrontSensor(
    _Inout_ PNABU_OWN_DEVICE_CONTEXT Context,
    _In_ PPLATFORM_CLIENT Client)
{
    UCHAR geometry[2];
    UCHAR streamValue = 0;
    BOOLEAN sensorStarted = FALSE;
    BOOLEAN sequenceComplete = FALSE;
    NTSTATUS status = STATUS_UNSUCCESSFUL;
    NTSTATUS stopStatus;

    InterlockedExchange(&Context->FrontProgramAttempted, 1);
    NabuOwnSetProgramCheckpoint(
        Context, 1, 0, 0, 0, STATUS_PENDING);

    NabuOwnSetProgramCheckpoint(
        Context, 2, 1, 0, 0x0103, STATUS_PENDING);
    status = NabuOwnSensorWriteRetry(Client, 0x0103, 0x01);
    if (!NT_SUCCESS(status)) {
        Context->FrontInitStatus = (LONG)status;
        Context->FrontInitFailIndex = 0;
        Context->FrontInitFailReg = 0x0103;
        goto Cleanup;
    }
    KeStallExecutionProcessor(10000);

    status = NabuOwnWriteSensorTable(
        Context, Client, 3, 2,
        NabuOwnOv8856Init + 1,
        RTL_NUMBER_OF(NabuOwnOv8856Init) - 1,
        &Context->FrontInitFailIndex,
        &Context->FrontInitFailReg);
    Context->FrontInitStatus = (LONG)status;
    if (!NT_SUCCESS(status)) {
        goto Cleanup;
    }

    status = NabuOwnWriteSensorTable(
        Context, Client, 4, 3,
        NabuOwnOv8856Mode3264x2448,
        RTL_NUMBER_OF(NabuOwnOv8856Mode3264x2448),
        &Context->FrontModeFailIndex,
        &Context->FrontModeFailReg);
    Context->FrontModeStatus = (LONG)status;
    if (!NT_SUCCESS(status)) {
        goto Cleanup;
    }

    NabuOwnSetProgramCheckpoint(
        Context, 5, 4, 0, 0x3808, STATUS_PENDING);
    status = NabuOwnSensorReadRetry(
        Client, 0x3808, geometry, sizeof(geometry));
    if (NT_SUCCESS(status)) {
        Context->FrontWidth =
            (LONG)(((ULONG)geometry[0] << 8) | geometry[1]);
        status = NabuOwnSensorReadRetry(
            Client, 0x380A, geometry, sizeof(geometry));
    }
    if (NT_SUCCESS(status)) {
        Context->FrontHeight =
            (LONG)(((ULONG)geometry[0] << 8) | geometry[1]);
        if (NabuOwnReadLong(&Context->FrontWidth) != 3264 ||
            NabuOwnReadLong(&Context->FrontHeight) != 2448) {
            status = STATUS_DATA_ERROR;
        }
    }
    Context->FrontGeometryStatus = (LONG)status;
    NabuOwnSetProgramCheckpoint(
        Context, 5, 4, 1, 0x380A, status);
    if (!NT_SUCCESS(status)) {
        goto Cleanup;
    }

    NabuOwnSetProgramCheckpoint(
        Context, 6, 5, 0, 0x0100, STATUS_PENDING);
    status = NabuOwnSensorWriteRetry(Client, 0x0100, 0x01);
    Context->FrontStreamWriteStatus = (LONG)status;
    if (!NT_SUCCESS(status)) {
        goto Cleanup;
    }
    sensorStarted = TRUE;

    status = NabuOwnSensorReadRetry(
        Client, 0x0100, &streamValue, sizeof(streamValue));
    Context->FrontStreamReadStatus = (LONG)status;
    Context->FrontStreamReadback = streamValue;
    if (NT_SUCCESS(status) && streamValue != 1) {
        status = STATUS_DATA_ERROR;
        Context->FrontStreamReadStatus = (LONG)status;
    }
    NabuOwnSetProgramCheckpoint(
        Context, 7, 5, 1, 0x0100, status);
    if (!NT_SUCCESS(status)) {
        goto Cleanup;
    }
    KeStallExecutionProcessor(10000);
    sequenceComplete = TRUE;

Cleanup:
    if (sensorStarted) {
        NabuOwnSetProgramCheckpoint(
            Context, 8, 5, 2, 0x0100, STATUS_PENDING);
        stopStatus = NabuOwnSensorWriteRetry(Client, 0x0100, 0x00);
        Context->FrontStreamStopStatus = (LONG)stopStatus;
        if (!NT_SUCCESS(stopStatus)) {
            status = stopStatus;
            sequenceComplete = FALSE;
        }
    }

    if (sequenceComplete) {
        status = STATUS_SUCCESS;
        Context->FrontProgramCompleted = 1;
        NabuOwnSetProgramCheckpoint(
            Context, 100, 0, 0, 0, STATUS_SUCCESS);
    } else {
        NabuOwnSetProgramCheckpoint(
            Context,
            (LONG)NabuOwnReadLong(&Context->FrontProgramPhase),
            (LONG)NabuOwnReadLong(&Context->FrontProgramLastTable),
            (LONG)NabuOwnReadLong(&Context->FrontProgramLastIndex),
            (LONG)NabuOwnReadLong(&Context->FrontProgramLastReg),
            status);
    }
    return status;
}

static VOID
NabuOwnSetFrontPhyPhase(
    _Inout_ PNABU_OWN_DEVICE_CONTEXT Context,
    _In_ LONG Phase)
{
    InterlockedExchange(&Context->FrontPhyPhase, Phase);
    NabuOwnWriteDiagnostics(Context);
}

static NTSTATUS
NabuOwnProbeFrontPhy(
    _Inout_ PNABU_OWN_DEVICE_CONTEXT Context,
    _In_ PPLATFORM_CLIENT Client)
{
    PHYSICAL_ADDRESS address;
    PUCHAR camcc = NULL;
    PUCHAR csiphy = NULL;
    UCHAR streamValue = 0;
    BOOLEAN bandwidthVoted = FALSE;
    BOOLEAN phyConfigured = FALSE;
    BOOLEAN sensorStarted = FALSE;
    BOOLEAN sequenceComplete = FALSE;
    NTSTATUS status = STATUS_UNSUCCESSFUL;
    NTSTATUS stopStatus;

    InterlockedExchange(&Context->FrontPhyAttempted, 1);
    NabuOwnSetFrontPhyPhase(Context, 1);

    status = PlatformRequestRtBandwidth(
        Client, 0, NABU_OWN_RT_BANDWIDTH, NABU_OWN_RT_BANDWIDTH);
    Context->FrontRtBandwidthStatus = (LONG)status;
    NabuOwnSetFrontPhyPhase(Context, 2);
    if (!NT_SUCCESS(status)) {
        goto Cleanup;
    }
    bandwidthVoted = TRUE;

    address.QuadPart = NABU_CAMCC_PHYS;
    camcc = (PUCHAR)MmMapIoSpaceEx(
        address, NABU_CAMCC_BYTES, PAGE_READWRITE | PAGE_NOCACHE);
    if (camcc == NULL) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }
    NabuOwnSetFrontPhyPhase(Context, 3);

    if (!NabuOwnEnableFrontPhyClocks(Context, camcc)) {
        status = STATUS_DEVICE_NOT_READY;
        goto Cleanup;
    }
    NabuOwnSetFrontPhyPhase(Context, 4);

    address.QuadPart = NABU_CSIPHY1_PHYS;
    csiphy = (PUCHAR)MmMapIoSpaceEx(
        address, NABU_CSIPHY1_BYTES, PAGE_READWRITE | PAGE_NOCACHE);
    if (csiphy == NULL) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }
    NabuOwnSetFrontPhyPhase(Context, 5);

    Context->FrontPhyHwVersion =
        (LONG)NabuOwnReadFrontPhyVersion(csiphy);
    if (NabuOwnReadLong(&Context->FrontPhyHwVersion) !=
        0x10000000UL) {
        status = STATUS_DEVICE_HARDWARE_ERROR;
        goto Cleanup;
    }

    NabuOwnConfigureFrontPhy(Context, csiphy);
    phyConfigured = TRUE;
    KeStallExecutionProcessor(5000);
    NabuOwnClearFrontPhyIrqs(csiphy);
    Context->FrontPhyIrq5Before =
        (LONG)NabuOwnRead32(csiphy, 0x8C4);
    Context->FrontPhyIrq8Before =
        (LONG)NabuOwnRead32(csiphy, 0x8D0);
    NabuOwnSetFrontPhyPhase(Context, 6);

    status = NabuOwnSensorWriteRetry(Client, 0x0100, 0x01);
    Context->FrontStreamWriteStatus = (LONG)status;
    if (!NT_SUCCESS(status)) {
        goto Cleanup;
    }
    sensorStarted = TRUE;

    status = NabuOwnSensorReadRetry(
        Client, 0x0100, &streamValue, sizeof(streamValue));
    Context->FrontStreamReadStatus = (LONG)status;
    Context->FrontStreamReadback = streamValue;
    if (NT_SUCCESS(status) && streamValue != 1) {
        status = STATUS_DATA_ERROR;
        Context->FrontStreamReadStatus = (LONG)status;
    }
    if (!NT_SUCCESS(status)) {
        goto Cleanup;
    }
    NabuOwnSetFrontPhyPhase(Context, 7);

    KeStallExecutionProcessor(50000);
    Context->FrontPhyIrq5After =
        (LONG)NabuOwnRead32(csiphy, 0x8C4);
    Context->FrontPhyIrq8After =
        (LONG)NabuOwnRead32(csiphy, 0x8D0);
    Context->FrontPhyActivityDetected =
        (NabuOwnReadLong(&Context->FrontPhyIrq5After) !=
             NabuOwnReadLong(&Context->FrontPhyIrq5Before) ||
         NabuOwnReadLong(&Context->FrontPhyIrq8After) !=
             NabuOwnReadLong(&Context->FrontPhyIrq8Before) ||
         NabuOwnReadLong(&Context->FrontPhyIrq5After) != 0 ||
         NabuOwnReadLong(&Context->FrontPhyIrq8After) != 0) ? 1 : 0;
    sequenceComplete = TRUE;
    status = STATUS_SUCCESS;
    NabuOwnSetFrontPhyPhase(Context, 8);

Cleanup:
    if (sensorStarted) {
        stopStatus = NabuOwnSensorWriteRetry(Client, 0x0100, 0x00);
        Context->FrontStreamStopStatus = (LONG)stopStatus;
        if (!NT_SUCCESS(stopStatus)) {
            sequenceComplete = FALSE;
            status = stopStatus;
        }
    }
    if (phyConfigured) {
        NabuOwnDisableFrontPhy(csiphy);
    }
    if (csiphy != NULL) {
        MmUnmapIoSpace(csiphy, NABU_CSIPHY1_BYTES);
    }
    if (camcc != NULL) {
        NabuOwnDisableFrontPhyClocks(camcc);
        MmUnmapIoSpace(camcc, NABU_CAMCC_BYTES);
    }
    if (bandwidthVoted) {
        Context->FrontRtBandwidthReleaseStatus =
            (LONG)PlatformRequestRtBandwidth(Client, 0, 0, 0);
    }

    Context->FrontPhyCompleted = sequenceComplete ? 1 : 0;
    NabuOwnSetFrontPhyPhase(
        Context, sequenceComplete ? 100 : Context->FrontPhyPhase);
    return status;
}

typedef struct _NABU_CSID_STATE {
    ULONG TopMask;
    ULONG RxMask;
    ULONG RdiMask;
    ULONG RxCfg0;
    ULONG RxCfg1;
    ULONG RxCapture;
    ULONG RdiCfg0;
    ULONG RdiCfg1;
    ULONG RdiCtrl;
    ULONG DropPattern;
    ULONG DropPeriod;
    ULONG IrqPattern;
    ULONG IrqPeriod;
    ULONG PixPattern;
    ULONG PixPeriod;
    ULONG LinePattern;
    ULONG LinePeriod;
    BOOLEAN Captured;
} NABU_CSID_STATE, *PNABU_CSID_STATE;

static VOID
NabuOwnCaptureCsidState(
    _In_ PUCHAR Csid,
    _Out_ PNABU_CSID_STATE State)
{
    RtlZeroMemory(State, sizeof(*State));
    State->TopMask = NabuOwnRead32(Csid, NABU_CSID_TOP_IRQ_MASK);
    State->RxMask = NabuOwnRead32(Csid, NABU_CSID_RX_IRQ_MASK);
    State->RdiMask = NabuOwnRead32(Csid, NABU_CSID_RDI0_IRQ_MASK);
    State->RxCfg0 = NabuOwnRead32(Csid, NABU_CSID_RX_CFG0);
    State->RxCfg1 = NabuOwnRead32(Csid, NABU_CSID_RX_CFG1);
    State->RxCapture = NabuOwnRead32(Csid, NABU_CSID_RX_CAPTURE);
    State->RdiCfg0 = NabuOwnRead32(Csid, NABU_CSID_RDI0_CFG0);
    State->RdiCfg1 = NabuOwnRead32(Csid, NABU_CSID_RDI0_CFG1);
    State->RdiCtrl = NabuOwnRead32(Csid, NABU_CSID_RDI0_CTRL);
    State->DropPattern =
        NabuOwnRead32(Csid, NABU_CSID_RDI0_DROP_PATTERN);
    State->DropPeriod =
        NabuOwnRead32(Csid, NABU_CSID_RDI0_DROP_PERIOD);
    State->IrqPattern =
        NabuOwnRead32(Csid, NABU_CSID_RDI0_IRQ_PATTERN);
    State->IrqPeriod =
        NabuOwnRead32(Csid, NABU_CSID_RDI0_IRQ_PERIOD);
    State->PixPattern =
        NabuOwnRead32(Csid, NABU_CSID_RDI0_PIX_PATTERN);
    State->PixPeriod =
        NabuOwnRead32(Csid, NABU_CSID_RDI0_PIX_PERIOD);
    State->LinePattern =
        NabuOwnRead32(Csid, NABU_CSID_RDI0_LINE_PATTERN);
    State->LinePeriod =
        NabuOwnRead32(Csid, NABU_CSID_RDI0_LINE_PERIOD);
    State->Captured = TRUE;
}

static BOOLEAN
NabuOwnRestoreCsidState(
    _In_ PUCHAR Csid,
    _In_ PNABU_CSID_STATE State)
{
    if (!State->Captured) {
        return TRUE;
    }

    NabuOwnWrite32(Csid, NABU_CSID_RDI0_CTRL, 0);
    NabuOwnWrite32(
        Csid, NABU_CSID_RDI0_LINE_PERIOD, State->LinePeriod);
    NabuOwnWrite32(
        Csid, NABU_CSID_RDI0_LINE_PATTERN, State->LinePattern);
    NabuOwnWrite32(
        Csid, NABU_CSID_RDI0_PIX_PERIOD, State->PixPeriod);
    NabuOwnWrite32(
        Csid, NABU_CSID_RDI0_PIX_PATTERN, State->PixPattern);
    NabuOwnWrite32(
        Csid, NABU_CSID_RDI0_IRQ_PERIOD, State->IrqPeriod);
    NabuOwnWrite32(
        Csid, NABU_CSID_RDI0_IRQ_PATTERN, State->IrqPattern);
    NabuOwnWrite32(
        Csid, NABU_CSID_RDI0_DROP_PERIOD, State->DropPeriod);
    NabuOwnWrite32(
        Csid, NABU_CSID_RDI0_DROP_PATTERN, State->DropPattern);
    NabuOwnWrite32(Csid, NABU_CSID_RDI0_CFG1, State->RdiCfg1);
    NabuOwnWrite32(Csid, NABU_CSID_RDI0_CFG0, State->RdiCfg0);
    NabuOwnWrite32(Csid, NABU_CSID_RX_CAPTURE, State->RxCapture);
    NabuOwnWrite32(Csid, NABU_CSID_RX_CFG1, State->RxCfg1);
    NabuOwnWrite32(Csid, NABU_CSID_RX_CFG0, State->RxCfg0);
    NabuOwnWrite32(Csid, NABU_CSID_RDI0_CTRL, State->RdiCtrl);
    NabuOwnWrite32(Csid, NABU_CSID_TOP_IRQ_CLEAR, 0x00000001UL);
    NabuOwnWrite32(Csid, NABU_CSID_RX_IRQ_CLEAR, 0x0FFFFFFFUL);
    NabuOwnWrite32(Csid, NABU_CSID_RDI0_IRQ_CLEAR, 0x00007FFFUL);
    NabuOwnWrite32(Csid, NABU_CSID_IRQ_CMD, 1);
    NabuOwnWrite32(Csid, NABU_CSID_RDI0_IRQ_MASK, State->RdiMask);
    NabuOwnWrite32(Csid, NABU_CSID_RX_IRQ_MASK, State->RxMask);
    NabuOwnWrite32(Csid, NABU_CSID_TOP_IRQ_MASK, State->TopMask);

    return
        NabuOwnRead32(Csid, NABU_CSID_RDI0_CTRL) ==
            State->RdiCtrl &&
        NabuOwnRead32(Csid, NABU_CSID_RDI0_CFG0) ==
            State->RdiCfg0 &&
        NabuOwnRead32(Csid, NABU_CSID_RX_CFG0) ==
            State->RxCfg0 &&
        NabuOwnRead32(Csid, NABU_CSID_RX_CFG1) ==
            State->RxCfg1 &&
        NabuOwnRead32(Csid, NABU_CSID_TOP_IRQ_MASK) ==
            State->TopMask &&
        NabuOwnRead32(Csid, NABU_CSID_RX_IRQ_MASK) ==
            State->RxMask &&
        NabuOwnRead32(Csid, NABU_CSID_RDI0_IRQ_MASK) ==
            State->RdiMask;
}

static NTSTATUS
NabuOwnProbeFrontRdi(
    _Inout_ PNABU_OWN_DEVICE_CONTEXT Context,
    _In_ PPLATFORM_CLIENT Client,
    _Inout_updates_bytes_opt_(NABU_FRONT_BUFFER_BYTES) PUCHAR RawBuffer,
    _In_ ULONGLONG RawDeviceAddress,
    _In_ BOOLEAN RawCapture)
{
    PHYSICAL_ADDRESS address;
    LARGE_INTEGER pollDelay;
    NABU_IFE_CLOCK_STATE ifeState;
    NABU_CSID_STATE csidState;
    PUCHAR camcc = NULL;
    PUCHAR csiphy = NULL;
    PUCHAR csid = NULL;
    PUCHAR vfe = NULL;
    UCHAR streamValue = 0;
    ULONG value = 0;
    ULONG rdiIrq = 0;
    ULONG rxIrq = 0;
    ULONG packetBefore = 0;
    ULONG attempt;
    ULONG rawAttempt;
    BOOLEAN bandwidthVoted = FALSE;
    BOOLEAN phyClocksTouched = FALSE;
    BOOLEAN phyConfigured = FALSE;
    BOOLEAN sensorStarted = FALSE;
    BOOLEAN wmStarted = FALSE;
    BOOLEAN packetsSeen = FALSE;
    BOOLEAN frameComplete = FALSE;
    BOOLEAN restoreOk = TRUE;
    NTSTATUS status = STATUS_UNSUCCESSFUL;
    NTSTATUS stopStatus;

    if (RawCapture &&
        (RawBuffer == NULL || RawDeviceAddress == 0 ||
         RawDeviceAddress > MAXULONG)) {
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(&ifeState, sizeof(ifeState));
    RtlZeroMemory(&csidState, sizeof(csidState));
    pollDelay.QuadPart = -10000LL; /* Relative 1 ms, PASSIVE_LEVEL queue. */
    InterlockedExchange(&Context->FrontPhyAttempted, 1);
    InterlockedExchange(&Context->FrontCsidAttempted, 1);
    NabuOwnSetFrontCsidPhase(Context, 1);

    status = PlatformRequestRtBandwidth(
        Client, 0, NABU_OWN_RT_BANDWIDTH, NABU_OWN_RT_BANDWIDTH);
    Context->FrontRtBandwidthStatus = (LONG)status;
    if (!NT_SUCCESS(status)) {
        goto Cleanup;
    }
    bandwidthVoted = TRUE;

    address.QuadPart = NABU_CAMCC_PHYS;
    camcc = (PUCHAR)MmMapIoSpaceEx(
        address, NABU_CAMCC_BYTES, PAGE_READWRITE | PAGE_NOCACHE);
    if (camcc == NULL) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }
    NabuOwnCaptureIfeClockState(camcc, &ifeState);
    NabuOwnSetFrontCsidPhase(Context, 10);

    if (!NabuOwnEnableIfeBase(camcc)) {
        status = STATUS_DEVICE_NOT_READY;
        goto Cleanup;
    }
    if (RawCapture &&
        !NabuOwnRaiseIfeClockForRaw(Context, camcc, &ifeState)) {
        status = STATUS_DEVICE_NOT_READY;
        goto Cleanup;
    }
    if (!NabuOwnEnableFrontPhyClocks(Context, camcc)) {
        phyClocksTouched = TRUE;
        status = STATUS_DEVICE_NOT_READY;
        goto Cleanup;
    }
    phyClocksTouched = TRUE;
    if (!NabuOwnEnableIfeReceive(camcc)) {
        status = STATUS_DEVICE_NOT_READY;
        goto Cleanup;
    }
    NabuOwnSetFrontCsidPhase(Context, 20);

    address.QuadPart = NABU_CSIPHY1_PHYS;
    csiphy = (PUCHAR)MmMapIoSpaceEx(
        address, NABU_CSIPHY1_BYTES, PAGE_READWRITE | PAGE_NOCACHE);
    if (csiphy == NULL) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }
    Context->FrontPhyHwVersion =
        (LONG)NabuOwnReadFrontPhyVersion(csiphy);
    if (NabuOwnReadLong(&Context->FrontPhyHwVersion) !=
        0x10000000UL) {
        status = STATUS_DEVICE_HARDWARE_ERROR;
        goto Cleanup;
    }
    NabuOwnConfigureFrontPhy(Context, csiphy);
    phyConfigured = TRUE;
    KeStallExecutionProcessor(5000);
    NabuOwnClearFrontPhyIrqs(csiphy);
    Context->FrontPhyIrq5Before =
        (LONG)NabuOwnRead32(csiphy, 0x8C4);
    Context->FrontPhyIrq8Before =
        (LONG)NabuOwnRead32(csiphy, 0x8D0);

    address.QuadPart = NABU_CSID0_PHYS;
    csid = (PUCHAR)MmMapIoSpaceEx(
        address, NABU_CSID0_BYTES, PAGE_READWRITE | PAGE_NOCACHE);
    if (csid == NULL) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }
    if (RawCapture) {
        address.QuadPart = NABU_VFE0_PHYS;
        vfe = (PUCHAR)MmMapIoSpaceEx(
            address, NABU_VFE0_BYTES, PAGE_READWRITE | PAGE_NOCACHE);
        if (vfe == NULL) {
            status = STATUS_INSUFFICIENT_RESOURCES;
            goto Cleanup;
        }
        Context->FrontRawVfeHwVersion =
            (LONG)NabuOwnRead32(vfe, NABU_VFE_HW_VERSION);
        if (NabuOwnReadLong(&Context->FrontRawVfeHwVersion) !=
            0x10020000UL) {
            status = STATUS_DEVICE_HARDWARE_ERROR;
            goto Cleanup;
        }
    }
    Context->FrontCsidHwVersion =
        (LONG)NabuOwnRead32(csid, NABU_CSID_HW_VERSION);
    if (NabuOwnReadLong(&Context->FrontCsidHwVersion) !=
        0x10000000UL) {
        status = STATUS_DEVICE_HARDWARE_ERROR;
        goto Cleanup;
    }
    NabuOwnCaptureCsidState(csid, &csidState);

    /*
     * No CSID interrupt is delivered to Windows.  Status remains readable
     * and is polled directly, avoiding an unserviced interrupt path.
     */
    NabuOwnWrite32(csid, NABU_CSID_TOP_IRQ_MASK, 0);
    NabuOwnWrite32(csid, NABU_CSID_RX_IRQ_MASK, 0);
    NabuOwnWrite32(csid, NABU_CSID_RDI0_IRQ_MASK, 0);
    NabuOwnWrite32(csid, NABU_CSID_TOP_IRQ_CLEAR, 0x00000001UL);
    NabuOwnWrite32(csid, NABU_CSID_RX_IRQ_CLEAR, 0x0FFFFFFFUL);
    NabuOwnWrite32(csid, NABU_CSID_RDI0_IRQ_CLEAR, 0x00007FFFUL);
    NabuOwnWrite32(csid, NABU_CSID_IRQ_CMD, 1);

    /* Stage 65-proven CSID175 retain-software reset. */
    NabuOwnSetFrontCsidPhase(Context, 30);
    NabuOwnWrite32(
        csid, NABU_CSID_RST_STROBES, NABU_CSID_RESET_RETAIN_SW);
    value = 0;
    for (attempt = 0; attempt < 10000; ++attempt) {
        value = NabuOwnRead32(csid, NABU_CSID_TOP_IRQ_STATUS);
        if ((value & NABU_CSID_RESET_DONE) != 0) {
            break;
        }
        KeStallExecutionProcessor(10);
    }
    Context->FrontCsidResetTopIrq = (LONG)value;
    Context->FrontCsidResetPolls =
        (LONG)(attempt < 10000 ? attempt + 1 : 10000);
    if ((value & NABU_CSID_RESET_DONE) == 0) {
        status = STATUS_IO_TIMEOUT;
        goto Cleanup;
    }
    NabuOwnWrite32(
        csid, NABU_CSID_TOP_IRQ_CLEAR, NABU_CSID_RESET_DONE);
    NabuOwnWrite32(csid, NABU_CSID_IRQ_CMD, 1);
    if (!NabuOwnWaitMask(
            csid, NABU_CSID_TOP_IRQ_STATUS,
            NABU_CSID_RESET_DONE, FALSE, 500, &value)) {
        status = STATUS_IO_TIMEOUT;
        goto Cleanup;
    }
    NabuOwnSetFrontCsidPhase(Context, 40);

    /*
     * Stage 66-proven front RX encoding plus RDI0 VC0/RAW10 decode.
     * Drop/subsample periods are one with zero patterns (no dropping).
     */
    NabuOwnWrite32(csid, NABU_CSID_RX_CFG0, NABU_FRONT_RX_CFG0);
    NabuOwnWrite32(csid, NABU_CSID_RX_CFG1, NABU_FRONT_RX_CFG1);
    /*
     * Official CSID175 long-packet capture: enable bit 0, RAW10 DT 0x2B
     * at shift 4, VC0 at shift 10.  This only latches the CSI header.
     */
    NabuOwnWrite32(
        csid, NABU_CSID_RX_CAPTURE, NABU_FRONT_RX_CAPTURE);
    NabuOwnWrite32(csid, NABU_CSID_RDI0_CFG0, NABU_FRONT_RDI_CFG0);
    NabuOwnWrite32(csid, NABU_CSID_RDI0_CFG1, 2);
    NabuOwnWrite32(csid, NABU_CSID_RDI0_DROP_PATTERN, 0);
    NabuOwnWrite32(csid, NABU_CSID_RDI0_DROP_PERIOD, 1);
    NabuOwnWrite32(csid, NABU_CSID_RDI0_IRQ_PATTERN, 0);
    NabuOwnWrite32(csid, NABU_CSID_RDI0_IRQ_PERIOD, 1);
    NabuOwnWrite32(csid, NABU_CSID_RDI0_PIX_PATTERN, 0);
    NabuOwnWrite32(csid, NABU_CSID_RDI0_PIX_PERIOD, 1);
    NabuOwnWrite32(csid, NABU_CSID_RDI0_LINE_PATTERN, 0);
    NabuOwnWrite32(csid, NABU_CSID_RDI0_LINE_PERIOD, 1);
    Context->FrontCsidRxCfg0 =
        (LONG)NabuOwnRead32(csid, NABU_CSID_RX_CFG0);
    Context->FrontCsidRxCfg1 =
        (LONG)NabuOwnRead32(csid, NABU_CSID_RX_CFG1);
    Context->FrontCsidRdiCfg0 =
        (LONG)NabuOwnRead32(csid, NABU_CSID_RDI0_CFG0);
    Context->FrontFrameCaptureCtrl =
        (LONG)NabuOwnRead32(csid, NABU_CSID_RX_CAPTURE);
    if (NabuOwnReadLong(&Context->FrontCsidRxCfg0) !=
            NABU_FRONT_RX_CFG0 ||
        NabuOwnReadLong(&Context->FrontCsidRxCfg1) !=
            NABU_FRONT_RX_CFG1 ||
        NabuOwnReadLong(&Context->FrontCsidRdiCfg0) !=
            NABU_FRONT_RDI_CFG0 ||
        NabuOwnReadLong(&Context->FrontFrameCaptureCtrl) !=
            NABU_FRONT_RX_CAPTURE) {
        status = STATUS_DEVICE_HARDWARE_ERROR;
        goto Cleanup;
    }

    if (RawCapture) {
        ULONG resetStatus = 0;
        ULONG wm = NABU_VFE_WM0;

        /*
         * Stage-31-proven VFE175 reset and RDI WM0 sequence.  All hardware
         * interrupt masks stay zero; completion is polled from MMIO/buffer.
         */
        Context->FrontRawPhase = 40;
        NabuOwnWriteDiagnostics(Context);
        NabuOwnWrite32(vfe, 0x5C, 0);
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
        Context->FrontRawVfeResetStatus = (LONG)resetStatus;
        Context->FrontRawVfeResetPolls = (LONG)(attempt + 1);
        if ((resetStatus & 0x80000000UL) == 0) {
            status = STATUS_IO_TIMEOUT;
            goto Cleanup;
        }
        NabuOwnWrite32(vfe, 0x64, resetStatus);
        NabuOwnWrite32(vfe, 0x68, NabuOwnRead32(vfe, 0x70));
        NabuOwnWrite32(vfe, 0x58, 1);

        NabuOwnWrite32(vfe, 0x226C, 0x82);
        NabuOwnWrite32(vfe, 0x207C, 0);
        NabuOwnWrite32(vfe, 0x2080, 0);
        NabuOwnWrite32(vfe, 0x200C, 0xFFFFF);
        NabuOwnWrite32(vfe, 0x211C, 0);
        NabuOwnWrite32(vfe, 0x2084, 0xFFFFFF);
        NabuOwnWrite32(vfe, 0x2044, 0);
        NabuOwnWrite32(vfe, 0x2048, 0);
        NabuOwnWrite32(vfe, 0x204C, 0);
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
        NabuOwnWrite32(vfe, wm + 0x58, NABU_FRONT_FRAME_BYTES);
        /*
         * VFE175 consumes/shadows image_addr; a read before the WM/RUP
         * update can legitimately return zero.  Program it last and commit
         * the WM immediately instead of rejecting a valid write.
         */
        NabuOwnWrite32(vfe, wm + 0x14, (ULONG)RawDeviceAddress);
        KeMemoryBarrier();
        NabuOwnWrite32(vfe, wm + 0x08, 3);
        wmStarted = TRUE;
        KeMemoryBarrier();
        NabuOwnWrite32(vfe, 0x4AC, 2);
        KeMemoryBarrier();
        Context->FrontRawWmCfg =
            (LONG)NabuOwnRead32(vfe, wm + 0x08);
        Context->FrontRawWmImageAddress = (LONG)(ULONG)RawDeviceAddress;
        Context->FrontRawPhase = 50;
        NabuOwnWriteDiagnostics(Context);
    }

    Context->FrontCsidPacketsBefore =
        (LONG)NabuOwnRead32(csid, NABU_CSID_TOTAL_PKTS);
    NabuOwnSetFrontCsidPhase(Context, 50);
    NabuOwnWrite32(csid, NABU_CSID_RDI0_CTRL, 1);
    if ((NabuOwnRead32(csid, NABU_CSID_RDI0_CTRL) & 1UL) == 0) {
        status = STATUS_DEVICE_HARDWARE_ERROR;
        goto Cleanup;
    }
    NabuOwnSetFrontCsidPhase(Context, 60);

    status = NabuOwnSensorWriteRetry(Client, 0x0100, 0x01);
    Context->FrontStreamWriteStatus = (LONG)status;
    if (!NT_SUCCESS(status)) {
        goto Cleanup;
    }
    sensorStarted = TRUE;
    status = NabuOwnSensorReadRetry(
        Client, 0x0100, &streamValue, sizeof(streamValue));
    Context->FrontStreamReadStatus = (LONG)status;
    Context->FrontStreamReadback = streamValue;
    if (!NT_SUCCESS(status) || streamValue != 1) {
        if (NT_SUCCESS(status)) {
            status = STATUS_DATA_ERROR;
        }
        goto Cleanup;
    }
    if (RawCapture) {
        InterlockedExchange(&Context->FrontCsidPhase, 70);
    } else {
        NabuOwnSetFrontCsidPhase(Context, 70);
    }

    /*
     * Stage 67 stopped on the first packet (SOF).  Stage 68 waits up to
     * 500 ms for the RDI INPUT_EOF bit, without enabling a hardware IRQ.
     */
    packetBefore =
        NabuOwnReadLong(&Context->FrontCsidPacketsBefore);
    value = packetBefore;
    for (attempt = 0; attempt < 500; ++attempt) {
        value = NabuOwnRead32(csid, NABU_CSID_TOTAL_PKTS);
        rdiIrq = NabuOwnRead32(csid, NABU_CSID_RDI0_IRQ_STATUS);
        if ((rdiIrq & NABU_CSID_PATH_INPUT_SOF) != 0) {
            Context->FrontFrameSofSeen = 1;
        }
        if (value != packetBefore) {
            packetsSeen = TRUE;
        }
        if ((rdiIrq & NABU_CSID_PATH_INPUT_EOF) != 0) {
            Context->FrontFrameEofSeen = 1;
            frameComplete = TRUE;
            break;
        }
        (VOID)KeDelayExecutionThread(KernelMode, FALSE, &pollDelay);
    }
    Context->FrontCsidPolls =
        (LONG)(attempt < 500 ? attempt + 1 : 500);
    Context->FrontCsidPacketsAfter = (LONG)value;
    Context->FrontFramePacketDelta =
        (LONG)(value - packetBefore);
    rxIrq = NabuOwnRead32(csid, NABU_CSID_RX_IRQ_STATUS);
    rdiIrq = NabuOwnRead32(csid, NABU_CSID_RDI0_IRQ_STATUS);
    Context->FrontCsidRxIrqAfter = (LONG)rxIrq;
    Context->FrontCsidRdiIrqAfter = (LONG)rdiIrq;
    Context->FrontCsidTopIrqAfter =
        (LONG)NabuOwnRead32(csid, NABU_CSID_TOP_IRQ_STATUS);
    Context->FrontCsidEccStats =
        (LONG)NabuOwnRead32(csid, NABU_CSID_ECC_STATS);
    Context->FrontCsidCrcErrors =
        (LONG)NabuOwnRead32(csid, NABU_CSID_CRC_ERRORS);
    Context->FrontCsidLongPkt0 =
        (LONG)NabuOwnRead32(csid, NABU_CSID_LONG_PKT0);
    Context->FrontCsidLongPkt1 =
        (LONG)NabuOwnRead32(csid, NABU_CSID_LONG_PKT1);
    value = NabuOwnReadLong(&Context->FrontCsidLongPkt0);
    Context->FrontFrameVirtualChannel =
        (LONG)((value >> 22) & 0x3UL);
    Context->FrontFrameDataType =
        (LONG)((value >> 16) & 0x3FUL);
    Context->FrontFrameWordCount =
        (LONG)(value & 0xFFFFUL);
    Context->FrontFrameLongPacketMatches =
        (NabuOwnReadLong(&Context->FrontFrameVirtualChannel) == 0 &&
         NabuOwnReadLong(&Context->FrontFrameDataType) == 0x2BUL &&
         NabuOwnReadLong(&Context->FrontFrameWordCount) == 4080UL)
            ? 1 : 0;
    Context->FrontCsidRdiBytesPing =
        (LONG)NabuOwnRead32(csid, NABU_CSID_RDI0_BYTES_PING);
    Context->FrontCsidRdiBytesPong =
        (LONG)NabuOwnRead32(csid, NABU_CSID_RDI0_BYTES_PONG);
    Context->FrontCsidRdiStatus =
        (LONG)NabuOwnRead32(csid, NABU_CSID_RDI0_STATUS);
    Context->FrontFrameRxErrorMask =
        (LONG)(rxIrq & NABU_CSID_RX_ERROR_MASK);
    Context->FrontFrameRdiErrorMask =
        (LONG)(rdiIrq & NABU_CSID_PATH_ERROR_MASK);
    Context->FrontFrameMeasure0 =
        (LONG)NabuOwnRead32(csid, NABU_CSID_RDI0_MEASURE0);
    Context->FrontFrameMeasure1 =
        (LONG)NabuOwnRead32(csid, NABU_CSID_RDI0_MEASURE1);
    Context->FrontFrameMeasure2 =
        (LONG)NabuOwnRead32(csid, NABU_CSID_RDI0_MEASURE2);
    if (RawCapture) {
        for (rawAttempt = 0; rawAttempt < 200; ++rawAttempt) {
            if (*(volatile ULONG *)(
                    RawBuffer + NABU_FRONT_FRAME_BYTES - 8) !=
                    NABU_FRONT_RAW_MARKER ||
                *(volatile ULONG *)(
                    RawBuffer + NABU_FRONT_FRAME_BYTES - 4) !=
                    NABU_FRONT_RAW_MARKER) {
                Context->FrontRawWaitExitReason = 1;
                break;
            }
            if ((NabuOwnRead32(vfe, 0x205C) & 0x7800UL) != 0) {
                Context->FrontRawWaitExitReason = 2;
                break;
            }
            (VOID)KeDelayExecutionThread(
                KernelMode, FALSE, &pollDelay);
        }
        Context->FrontRawWaitPolls =
            (LONG)(rawAttempt < 200 ? rawAttempt + 1 : 200);
        if (rawAttempt == 200) {
            Context->FrontRawWaitExitReason = 3;
        }
        Context->FrontRawWmCfg =
            (LONG)NabuOwnRead32(vfe, NABU_VFE_WM0 + 0x08);
        Context->FrontRawWmImageAddress =
            (LONG)NabuOwnRead32(vfe, NABU_VFE_WM0 + 0x14);
        Context->FrontRawWmStatus0 =
            (LONG)NabuOwnRead32(vfe, NABU_VFE_WM0 + 0x00);
        Context->FrontRawWmStatus1 =
            (LONG)NabuOwnRead32(vfe, NABU_VFE_WM0 + 0x04);
        Context->FrontRawVfeTopIrq0 =
            (LONG)NabuOwnRead32(vfe, 0x6C);
        Context->FrontRawVfeTopIrq1 =
            (LONG)NabuOwnRead32(vfe, 0x70);
        Context->FrontRawVfeBusIrq0 =
            (LONG)NabuOwnRead32(vfe, 0x205C);
        Context->FrontRawVfeBusIrq1 =
            (LONG)NabuOwnRead32(vfe, 0x2060);
        Context->FrontRawVfeBusIrq2 =
            (LONG)NabuOwnRead32(vfe, 0x2064);
        Context->FrontRawVfeBusDebug0 =
            (LONG)NabuOwnRead32(vfe, 0x2270);
        Context->FrontRawBufferSample0 =
            *(volatile LONG *)(RawBuffer + 0);
        Context->FrontRawBufferSample1 =
            *(volatile LONG *)(RawBuffer + 4);
        Context->FrontRawBufferSample2 =
            *(volatile LONG *)(RawBuffer + 8);
        Context->FrontRawBufferSample3 =
            *(volatile LONG *)(RawBuffer + 12);
        Context->FrontRawBufferTail0 =
            *(volatile LONG *)(
                RawBuffer + NABU_FRONT_FRAME_BYTES - 8);
        Context->FrontRawBufferTail1 =
            *(volatile LONG *)(
                RawBuffer + NABU_FRONT_FRAME_BYTES - 4);
        Context->FrontRawBufferChanged =
            (NabuOwnReadLong(&Context->FrontRawBufferSample0) !=
                 NABU_FRONT_RAW_MARKER ||
             NabuOwnReadLong(&Context->FrontRawBufferSample1) !=
                 NABU_FRONT_RAW_MARKER ||
             NabuOwnReadLong(&Context->FrontRawBufferSample2) !=
                 NABU_FRONT_RAW_MARKER ||
             NabuOwnReadLong(&Context->FrontRawBufferSample3) !=
                 NABU_FRONT_RAW_MARKER) ? 1 : 0;
        Context->FrontRawBufferTailChanged =
            (NabuOwnReadLong(&Context->FrontRawBufferTail0) !=
                 NABU_FRONT_RAW_MARKER ||
             NabuOwnReadLong(&Context->FrontRawBufferTail1) !=
                 NABU_FRONT_RAW_MARKER) ? 1 : 0;
        Context->FrontRawPhase = 60;
    }
    Context->FrontPhyIrq5After =
        (LONG)NabuOwnRead32(csiphy, 0x8C4);
    Context->FrontPhyIrq8After =
        (LONG)NabuOwnRead32(csiphy, 0x8D0);
    Context->FrontPhyActivityDetected =
        (NabuOwnReadLong(&Context->FrontPhyIrq5After) != 0 ||
         NabuOwnReadLong(&Context->FrontPhyIrq8After) != 0) ? 1 : 0;
    if (RawCapture) {
        InterlockedExchange(&Context->FrontCsidPhase, 80);
    } else {
        NabuOwnSetFrontCsidPhase(Context, 80);
    }
    if (!packetsSeen || !frameComplete ||
        (RawCapture &&
         NabuOwnReadLong(&Context->FrontRawBufferTailChanged) == 0)) {
        status = STATUS_DEVICE_NOT_READY;
        goto Cleanup;
    }
    if (NabuOwnReadLong(&Context->FrontFrameRxErrorMask) != 0 ||
        NabuOwnReadLong(&Context->FrontFrameRdiErrorMask) != 0 ||
        NabuOwnReadLong(&Context->FrontCsidEccStats) != 0 ||
        NabuOwnReadLong(&Context->FrontCsidCrcErrors) != 0 ||
        NabuOwnReadLong(&Context->FrontFrameLongPacketMatches) == 0) {
        status = STATUS_DATA_ERROR;
        goto Cleanup;
    }
    status = STATUS_SUCCESS;

Cleanup:
    if (RawCapture) {
        InterlockedExchange(&Context->FrontCsidPhase, 90);
    } else {
        NabuOwnSetFrontCsidPhase(Context, 90);
    }
    if (sensorStarted) {
        stopStatus = NabuOwnSensorWriteRetry(Client, 0x0100, 0x00);
        Context->FrontStreamStopStatus = (LONG)stopStatus;
        if (!NT_SUCCESS(stopStatus)) {
            restoreOk = FALSE;
            if (NT_SUCCESS(status)) {
                status = stopStatus;
            }
        }
    }
    if (wmStarted && vfe != NULL) {
        NabuOwnWrite32(vfe, NABU_VFE_WM0 + 0x08, 0);
        wmStarted = FALSE;
        KeMemoryBarrier();
    }
    if (RawCapture) {
        NabuOwnWriteDiagnostics(Context);
        if (Context->DiagnosticKey != NULL) {
            (VOID)ZwFlushKey(
                WdfRegistryWdmGetHandle(Context->DiagnosticKey));
        }
    }
    if (csid != NULL) {
        if (!NabuOwnRestoreCsidState(csid, &csidState)) {
            restoreOk = FALSE;
        }
        MmUnmapIoSpace(csid, NABU_CSID0_BYTES);
    }
    if (phyConfigured) {
        NabuOwnDisableFrontPhy(csiphy);
    }
    if (csiphy != NULL) {
        MmUnmapIoSpace(csiphy, NABU_CSIPHY1_BYTES);
    }
    if (vfe != NULL) {
        MmUnmapIoSpace(vfe, NABU_VFE0_BYTES);
    }
    if (camcc != NULL) {
        if (phyClocksTouched) {
            NabuOwnDisableFrontPhyClocks(camcc);
        }
        if (!NabuOwnRestoreIfeClockState(camcc, &ifeState)) {
            restoreOk = FALSE;
        }
        MmUnmapIoSpace(camcc, NABU_CAMCC_BYTES);
    }
    if (bandwidthVoted) {
        Context->FrontRtBandwidthReleaseStatus =
            (LONG)PlatformRequestRtBandwidth(Client, 0, 0, 0);
        if (!NT_SUCCESS(
                (NTSTATUS)Context->FrontRtBandwidthReleaseStatus)) {
            restoreOk = FALSE;
        }
    }

    Context->FrontCsidRestoreCompleted = restoreOk ? 1 : 0;
    Context->FrontFrameComplete =
        frameComplete && packetsSeen && restoreOk ? 1 : 0;
    Context->FrontPhyCompleted =
        frameComplete && packetsSeen && restoreOk ? 1 : 0;
    Context->FrontCsidCompleted =
        frameComplete && packetsSeen && restoreOk ? 1 : 0;
    if (!restoreOk && NT_SUCCESS(status)) {
        status = STATUS_DEVICE_HARDWARE_ERROR;
    }
    NabuOwnSetFrontCsidPhase(
        Context,
        NT_SUCCESS(status) && frameComplete && packetsSeen && restoreOk
            ? 100
            : Context->FrontCsidPhase);
    return status;
}

static PVOID
NabuOwnGetSmmuFunction(
    _In_reads_bytes_(NABU_SMMU_INTERFACE_BYTES) const UCHAR *Table,
    _In_ ULONG Offset)
{
    PVOID function = NULL;

    if (Offset <= NABU_SMMU_INTERFACE_BYTES - sizeof(function)) {
        RtlCopyMemory(&function, Table + Offset, sizeof(function));
    }
    return function;
}

#ifdef AVSTREAM_BUILD
static NTSTATUS
NabuFrontLegacyStateCallback(
    _In_opt_ PVOID ContextBank,
    _In_opt_ PVOID GetFaultInformation)
{
    /*
     * The callback is required by the legacy nabu SMMU contract.  The stock
     * Qualcomm callback only gathers fault diagnostics here; it does not
     * participate in normal mapping.  Keep our callback side-effect-free so
     * an unexpected fault cannot re-enter VCM teardown.
     */
    UNREFERENCED_PARAMETER(ContextBank);
    UNREFERENCED_PARAMETER(GetFaultInformation);
    return STATUS_SUCCESS;
}

static VOID
NabuFrontReleasePersistentVcm(
    _Inout_ PNABU_OWN_DEVICE_CONTEXT Context)
{
    NTSTATUS status;

    if (Context->AvVcmStockGuard != NULL &&
        Context->AvVcmRelease != NULL) {
        status = Context->AvVcmRelease(Context->AvVcmStockGuard);
        Context->FrontRawVcmReleaseStatus = (LONG)status;
        Context->AvVcmStockGuard = NULL;
    }
    while (Context->AvVcmActivated != 0 &&
           Context->AvVcmDeactivate != NULL) {
        Context->AvVcmActivated -= 1;
        status = Context->AvVcmDeactivate(
            Context->AvVcmAssociations[Context->AvVcmActivated]);
        Context->FrontRawVcmDeactivateStatus = (LONG)status;
    }
    if (Context->AvSmmuStateEnabled &&
        Context->AvSetState != NULL) {
        status = Context->AvSetState(
            Context->AvSmmuInterface.Header.Context, 0);
        Context->FrontRawVcmSetStateOffStatus = (LONG)status;
        Context->AvSmmuStateEnabled = FALSE;
    }
    if (Context->AvLegacyCallbackRegistered &&
        Context->AvLegacyCallbackRemove != NULL) {
        Context->AvLegacyCallbackRemove(
            Context->AvSmmuInterface.Header.Context,
            NABU_SMMU_CONFIG_SELECTOR);
        Context->FrontRawLegacyCallbackRemoved = 1;
        Context->AvLegacyCallbackRegistered = FALSE;
    }
    while (Context->AvVcmAssociated != 0 &&
           Context->AvVcmDeassoc != NULL) {
        Context->AvVcmAssociated -= 1;
        status = Context->AvVcmDeassoc(
            Context->AvVcmAssociations[Context->AvVcmAssociated]);
        Context->FrontRawVcmDeassocStatus = (LONG)status;
        Context->AvVcmAssociations[Context->AvVcmAssociated] = NULL;
    }
    if (Context->AvVcmRegion != NULL &&
        Context->AvVcmFree != NULL) {
        status = Context->AvVcmFree(Context->AvVcmRegion);
        Context->FrontRawVcmFreeStatus = (LONG)status;
        Context->AvVcmRegion = NULL;
    }
    if (Context->AvSmmuInterface.Header.InterfaceDereference != NULL) {
        Context->AvSmmuInterface.Header.InterfaceDereference(
            Context->AvSmmuInterface.Header.Context);
    }
    RtlZeroMemory(
        &Context->AvSmmuInterface,
        sizeof(Context->AvSmmuInterface));
    if (Context->AvSmmuFileObject != NULL) {
        ObDereferenceObject(Context->AvSmmuFileObject);
        Context->AvSmmuFileObject = NULL;
    }
    if (Context->AvSmmuInterfaceList != NULL) {
        ExFreePool(Context->AvSmmuInterfaceList);
        Context->AvSmmuInterfaceList = NULL;
    }
    Context->AvVcmPersistent = 0;
}

static NTSTATUS
NabuFrontInitializePersistentVcm(
    _Inout_ PNABU_OWN_DEVICE_CONTEXT Context)
{
    UNICODE_STRING interfaceName;
    PDEVICE_OBJECT deviceObject = NULL;
    KEVENT event;
    IO_STATUS_BLOCK ioStatus;
    PIRP irp = NULL;
    PIO_STACK_LOCATION stack;
    UCHAR configuration[NABU_SMMU_CONFIG_BYTES];
    PUCHAR table;
    ULONGLONG rawCount = 0;
    ULONGLONG contextHandle;
    ULONG i;
    NTSTATUS status;
    PVOID iommuGetConfig;
    PVOID iommuCallback;
    NABU_SMMU_IS_IOMMU isIommu;
    NABU_SMMU_GET_CONFIG getConfig;
    NABU_SMMU_LEGACY_CALLBACK_REGISTER legacyCallbackRegister;
    NABU_VCM_CREATE vcmCreate;
    NABU_VCM_ASSOC vcmAssoc;
    NABU_VCM_HANDLE_STATUS vcmActivate;
    BOOLEAN iommuMode = FALSE;

    if (InterlockedCompareExchange(
            &Context->AvVcmSessionState, 1, 0) != 0) {
        if (NabuOwnReadLong(&Context->AvVcmSessionState) == 2) {
            return STATUS_SUCCESS;
        }
        return (NTSTATUS)NabuOwnReadLong(
            &Context->AvVcmSessionStatus);
    }

    RtlZeroMemory(configuration, sizeof(configuration));
    RtlZeroMemory(
        &Context->AvSmmuInterface,
        sizeof(Context->AvSmmuInterface));
    RtlZeroMemory(
        Context->AvVcmAssociations,
        sizeof(Context->AvVcmAssociations));
    Context->AvVcmSessionStatus = STATUS_PENDING;
    Context->AvVcmPersistent = 0;
    Context->AvVcmStockGuardStatus = STATUS_NOT_SUPPORTED;

    status = IoGetDeviceInterfaces(
        &NabuOwnSmmuDeviceInterfaceGuid,
        NULL,
        FALSE,
        &Context->AvSmmuInterfaceList);
    Context->FrontRawSmmuEnumerateStatus = (LONG)status;
    if (!NT_SUCCESS(status) ||
        Context->AvSmmuInterfaceList == NULL ||
        Context->AvSmmuInterfaceList[0] == L'\0') {
        goto Failure;
    }

    RtlInitUnicodeString(
        &interfaceName, Context->AvSmmuInterfaceList);
    status = IoGetDeviceObjectPointer(
        &interfaceName,
        NABU_SMMU_DESIRED_ACCESS,
        &Context->AvSmmuFileObject,
        &deviceObject);
    Context->FrontRawSmmuOpenStatus = (LONG)status;
    if (!NT_SUCCESS(status)) {
        goto Failure;
    }

    KeInitializeEvent(&event, NotificationEvent, FALSE);
    RtlZeroMemory(&ioStatus, sizeof(ioStatus));
    irp = IoBuildSynchronousFsdRequest(
        IRP_MJ_PNP,
        deviceObject,
        NULL,
        0,
        NULL,
        &event,
        &ioStatus);
    if (irp == NULL) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        Context->FrontRawSmmuQueryStatus = (LONG)status;
        goto Failure;
    }
    stack = IoGetNextIrpStackLocation(irp);
    stack->MinorFunction = IRP_MN_QUERY_INTERFACE;
    stack->Parameters.QueryInterface.InterfaceType =
        (LPGUID)&NabuOwnSmmuQueryInterfaceGuid;
    stack->Parameters.QueryInterface.Size =
        (USHORT)sizeof(Context->AvSmmuInterface);
    stack->Parameters.QueryInterface.Version = 0;
    stack->Parameters.QueryInterface.Interface =
        (PINTERFACE)&Context->AvSmmuInterface;
    stack->Parameters.QueryInterface.InterfaceSpecificData = NULL;
    status = IoCallDriver(deviceObject, irp);
    if (status == STATUS_PENDING) {
        (VOID)KeWaitForSingleObject(
            &event, Executive, KernelMode, FALSE, NULL);
        status = ioStatus.Status;
    }
    Context->FrontRawSmmuQueryStatus = (LONG)status;
    if (!NT_SUCCESS(status) ||
        Context->AvSmmuInterface.Header.Size <
            NABU_SMMU_INTERFACE_BYTES) {
        if (NT_SUCCESS(status)) {
            status = STATUS_INFO_LENGTH_MISMATCH;
            Context->FrontRawSmmuQueryStatus = (LONG)status;
        }
        goto Failure;
    }

    table = (PUCHAR)&Context->AvSmmuInterface;
    Context->FrontRawSmmuInterfaceSize =
        (LONG)Context->AvSmmuInterface.Header.Size;
    Context->FrontRawSmmuInterfaceVersion =
        (LONG)Context->AvSmmuInterface.Header.Version;
    Context->FrontRawSmmuContextLow =
        (LONG)(ULONG_PTR)Context->AvSmmuInterface.Header.Context;
    Context->FrontRawSmmuContextHigh =
        (LONG)((ULONG_PTR)Context->AvSmmuInterface.Header.Context >> 32);

    legacyCallbackRegister =
        (NABU_SMMU_LEGACY_CALLBACK_REGISTER)NabuOwnGetSmmuFunction(
            table, NABU_SMMU_LEGACY_CB_OFFSET);
    Context->AvLegacyCallbackRemove =
        (NABU_SMMU_LEGACY_CALLBACK_REMOVE)NabuOwnGetSmmuFunction(
            table, NABU_SMMU_LEGACY_CB_REMOVE_OFFSET);
    iommuGetConfig = NabuOwnGetSmmuFunction(
        table, NABU_IOMMU_GET_CONFIG_OFFSET);
    iommuCallback = NabuOwnGetSmmuFunction(
        table, NABU_IOMMU_CB_OFFSET);
    isIommu = (NABU_SMMU_IS_IOMMU)NabuOwnGetSmmuFunction(
        table, NABU_IOMMU_IS_IOMMU_OFFSET);
    Context->FrontRawLegacyCallbackPresent =
        legacyCallbackRegister != NULL ? 1 : 0;
    Context->FrontRawIommuGetConfigPresent =
        iommuGetConfig != NULL ? 1 : 0;
    Context->FrontRawIommuCallbackPresent =
        iommuCallback != NULL ? 1 : 0;
    Context->FrontRawIommuIsPresent = isIommu != NULL ? 1 : 0;
    if (isIommu != NULL) {
        iommuMode = isIommu() ? TRUE : FALSE;
        Context->FrontRawIommuIsResult = iommuMode ? 1 : 0;
    }

    Context->AvSetState =
        (NABU_SMMU_SET_STATE)NabuOwnGetSmmuFunction(
            table, NABU_SMMU_SET_STATE_OFFSET);
    getConfig = (NABU_SMMU_GET_CONFIG)NabuOwnGetSmmuFunction(
        table, NABU_SMMU_GET_CONFIG_OFFSET);
    vcmCreate = (NABU_VCM_CREATE)NabuOwnGetSmmuFunction(
        table, NABU_VCM_CREATE_OFFSET);
    Context->AvVcmFree =
        (NABU_VCM_FREE)NabuOwnGetSmmuFunction(
            table, NABU_VCM_FREE_OFFSET);
    Context->AvVcmReserve =
        (NABU_VCM_RESERVE)NabuOwnGetSmmuFunction(
            table, NABU_VCM_RESERVE_OFFSET);
    Context->AvVcmRelease =
        (NABU_VCM_HANDLE_STATUS)NabuOwnGetSmmuFunction(
            table, NABU_VCM_RELEASE_OFFSET);
    Context->AvVcmBack =
        (NABU_VCM_BACK)NabuOwnGetSmmuFunction(
            table, NABU_VCM_BACK_OFFSET);
    Context->AvVcmUnback =
        (NABU_VCM_HANDLE_STATUS)NabuOwnGetSmmuFunction(
            table, NABU_VCM_UNBACK_OFFSET);
    vcmAssoc = (NABU_VCM_ASSOC)NabuOwnGetSmmuFunction(
        table, NABU_VCM_ASSOC_OFFSET);
    Context->AvVcmDeassoc =
        (NABU_VCM_HANDLE_STATUS)NabuOwnGetSmmuFunction(
            table, NABU_VCM_DEASSOC_OFFSET);
    vcmActivate =
        (NABU_VCM_HANDLE_STATUS)NabuOwnGetSmmuFunction(
            table, NABU_VCM_ACTIVATE_OFFSET);
    Context->AvVcmDeactivate =
        (NABU_VCM_HANDLE_STATUS)NabuOwnGetSmmuFunction(
            table, NABU_VCM_DEACTIVATE_OFFSET);
    Context->AvVcmGetDevAddr =
        (NABU_VCM_GET_DEV_ADDR)NabuOwnGetSmmuFunction(
            table, NABU_VCM_GET_DEV_ADDR_OFFSET);
    if (Context->AvSetState == NULL || getConfig == NULL ||
        vcmCreate == NULL || Context->AvVcmFree == NULL ||
        Context->AvVcmReserve == NULL ||
        Context->AvVcmRelease == NULL ||
        Context->AvVcmBack == NULL ||
        Context->AvVcmUnback == NULL ||
        vcmAssoc == NULL || Context->AvVcmDeassoc == NULL ||
        vcmActivate == NULL ||
        Context->AvVcmDeactivate == NULL ||
        Context->AvVcmGetDevAddr == NULL) {
        status = STATUS_PROCEDURE_NOT_FOUND;
        Context->FrontRawSmmuGetConfigStatus = (LONG)status;
        goto Failure;
    }

    status = getConfig(
        Context->AvSmmuInterface.Header.Context,
        NABU_SMMU_CONFIG_SELECTOR,
        configuration);
    Context->FrontRawSmmuGetConfigStatus = (LONG)status;
    if (!NT_SUCCESS(status)) {
        goto Failure;
    }
    RtlCopyMemory(&rawCount, configuration, sizeof(rawCount));
    Context->FrontRawSmmuContextCount = (LONG)rawCount;
    if (rawCount == 0 ||
        rawCount > NABU_SMMU_MAX_CONTEXT_BANKS) {
        status = STATUS_BUFFER_OVERFLOW;
        goto Failure;
    }

    Context->AvVcmRegion =
        vcmCreate(0, NABU_VCM_REGION_BYTES);
    Context->FrontRawVcmCreateStatus =
        Context->AvVcmRegion != NULL
            ? STATUS_SUCCESS
            : STATUS_UNSUCCESSFUL;
    if (Context->AvVcmRegion == NULL) {
        status = STATUS_UNSUCCESSFUL;
        goto Failure;
    }

    for (i = 0; i < (ULONG)rawCount; ++i) {
        PUCHAR entry =
            configuration + sizeof(rawCount) +
            (i * NABU_SMMU_CONFIG_ENTRY_BYTES);

        RtlCopyMemory(
            &contextHandle,
            entry + sizeof(ULONGLONG),
            sizeof(contextHandle));
        Context->AvVcmAssociations[i] = vcmAssoc(
            Context->AvVcmRegion,
            (PVOID)(ULONG_PTR)contextHandle,
            0);
        if (Context->AvVcmAssociations[i] == NULL) {
            status = STATUS_UNSUCCESSFUL;
            goto Failure;
        }
        Context->AvVcmAssociated += 1;
        Context->FrontRawVcmAssociationCount =
            (LONG)Context->AvVcmAssociated;
    }

    /*
     * qccamavs8150 registers the legacy state callback and deliberately does
     * not call SmmuSetState on this nabu path.  Calling both was the remaining
     * semantic mismatch in AV13/AV14 and made VcmBack reject a valid MDL.
     */
    if (!iommuMode) {
        if (legacyCallbackRegister == NULL ||
            Context->AvLegacyCallbackRemove == NULL) {
            status = STATUS_PROCEDURE_NOT_FOUND;
            goto Failure;
        }
        legacyCallbackRegister(
            Context->AvSmmuInterface.Header.Context,
            NABU_SMMU_CONFIG_SELECTOR,
            NabuFrontLegacyStateCallback,
            Context);
        Context->AvLegacyCallbackRegistered = TRUE;
        Context->FrontRawLegacyCallbackRegistered = 1;
        Context->FrontRawVcmSetStateStatus =
            STATUS_NOT_SUPPORTED;
    } else {
        status = Context->AvSetState(
            Context->AvSmmuInterface.Header.Context,
            NABU_SMMU_CONFIG_SELECTOR);
        Context->FrontRawVcmSetStateStatus = (LONG)status;
        if (!NT_SUCCESS(status)) {
            goto Failure;
        }
        Context->AvSmmuStateEnabled = TRUE;
    }

    Context->AvVcmStockGuard = Context->AvVcmReserve(
        Context->AvVcmRegion,
        NABU_VCM_STOCK_GUARD_BYTES,
        NABU_VCM_STOCK_GUARD_FLAGS);
    Context->AvVcmStockGuardStatus =
        Context->AvVcmStockGuard != NULL
            ? STATUS_SUCCESS
            : STATUS_UNSUCCESSFUL;
    if (Context->AvVcmStockGuard == NULL) {
        status = STATUS_UNSUCCESSFUL;
        goto Failure;
    }

    for (i = 0; i < Context->AvVcmAssociated; ++i) {
        status = vcmActivate(Context->AvVcmAssociations[i]);
        if (!NT_SUCCESS(status)) {
            goto Failure;
        }
        Context->AvVcmActivated += 1;
        Context->FrontRawVcmActivationCount =
            (LONG)Context->AvVcmActivated;
    }

    Context->AvVcmPersistent = 1;
    Context->AvVcmSessionStatus = STATUS_SUCCESS;
    InterlockedExchange(&Context->AvVcmSessionState, 2);
    return STATUS_SUCCESS;

Failure:
    Context->AvVcmSessionStatus = (LONG)status;
    NabuFrontReleasePersistentVcm(Context);
    InterlockedExchange(&Context->AvVcmSessionState, 3);
    return status;
}
#endif

static NTSTATUS
NabuOwnCaptureFrontRaw(
    _Inout_ PNABU_OWN_DEVICE_CONTEXT Context,
    _In_ PPLATFORM_CLIENT Client)
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
    PVOID rawBuffer = NULL;
    PMDL mdl = NULL;
    ULONG associated = 0;
    ULONG activated = 0;
    BOOLEAN stateEnabled = FALSE;
    BOOLEAN backed = FALSE;
    BOOLEAN cleanupOk = TRUE;
    BOOLEAN rawMapped = FALSE;
    ULONGLONG rawCount = 0;
    ULONGLONG contextHandle;
    ULONGLONG deviceAddress = 0;
    ULONGLONG reserveBytes;
    ULONG i;
    NTSTATUS status = STATUS_UNSUCCESSFUL;
    NTSTATUS captureStatus = STATUS_UNSUCCESSFUL;
    NTSTATUS finalStatus;
    PUCHAR table;
    PVOID legacyCallback = NULL;
    PVOID iommuGetConfig = NULL;
    PVOID iommuCallback = NULL;
    NABU_SMMU_IS_IOMMU isIommu = NULL;
    NABU_SMMU_SET_STATE setState = NULL;
    NABU_SMMU_GET_CONFIG getConfig = NULL;
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
    Context->FrontRawAttempted = 1;
    Context->FrontRawPhase = 1;
    NabuOwnWriteDiagnostics(Context);

#if defined(AVSTREAM_BUILD) && !defined(AVSTREAM_STAGE70_ONESHOT)
    /*
     * Match qccamavs8150::Mdl32 exactly: pages below 4 GB, non-cached
     * allocation, FULLY_REQUIRED | NO_WAIT | REQUIRE_CONTIGUOUS_CHUNKS.
     * AV8/AV10 tested allocation variants with a transient VCM; AV16 combines
     * the stock MDL with the now-proven persistent legacy VCM lifecycle.
     */
    {
        PHYSICAL_ADDRESS lowAddress;
        PHYSICAL_ADDRESS highAddress;
        PHYSICAL_ADDRESS skipBytes;
        PPFN_NUMBER pfnArray;
        ULONG pageCount;

        lowAddress.QuadPart = 0;
        highAddress.QuadPart = MAXULONG;
        skipBytes.QuadPart = 0;
        mdl = MmAllocatePagesForMdlEx(
            lowAddress,
            highAddress,
            skipBytes,
            NABU_FRONT_BUFFER_BYTES,
            MmNonCached,
            0x85);
        Context->FrontRawBufferAllocStatus =
            mdl != NULL ? STATUS_SUCCESS : STATUS_INSUFFICIENT_RESOURCES;
        if (mdl == NULL) {
            status = STATUS_INSUFFICIENT_RESOURCES;
            goto Cleanup;
        }
        Context->FrontRawBufferBytes = NABU_FRONT_BUFFER_BYTES;
        Context->AvRawMdlFlags = mdl->MdlFlags;
        Context->AvRawMdlByteCount = MmGetMdlByteCount(mdl);
        Context->AvRawMdlByteOffset = MmGetMdlByteOffset(mdl);
        pageCount = BYTES_TO_PAGES(MmGetMdlByteCount(mdl));
        pfnArray = (PPFN_NUMBER)(mdl + 1);
        if (pageCount != 0) {
            PFN_NUMBER firstPfn = pfnArray[0];
            PFN_NUMBER lastPfn = pfnArray[pageCount - 1];

            Context->AvRawFirstPfnLow = (LONG)firstPfn;
            Context->AvRawFirstPfnHigh =
                (LONG)((ULONGLONG)firstPfn >> 32);
            Context->AvRawLastPfnLow = (LONG)lastPfn;
            Context->AvRawLastPfnHigh =
                (LONG)((ULONGLONG)lastPfn >> 32);
        }
    }
#else
    /*
     * Manual Stage70 and AV18 use the already-proven non-paged pool + MDL
     * path.  Keeping allocation, MDL construction, VCM backing and teardown
     * in one call removes the last lifecycle difference from Stage70.
     */
    rawBuffer = ExAllocatePool2(
        POOL_FLAG_NON_PAGED, NABU_FRONT_BUFFER_BYTES,
        NABU_FRONT_RAW_POOL_TAG);
    Context->FrontRawBufferAllocStatus =
        rawBuffer != NULL ? STATUS_SUCCESS : STATUS_INSUFFICIENT_RESOURCES;
    if (rawBuffer == NULL) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }
    Context->FrontRawBufferBytes = NABU_FRONT_BUFFER_BYTES;
    RtlFillMemory(rawBuffer, NABU_FRONT_BUFFER_BYTES, 0xAB);
    KeMemoryBarrier();
#endif
    Context->FrontRawPhase = 2;
    NabuOwnWriteDiagnostics(Context);

#if defined(AVSTREAM_BUILD) && !defined(AVSTREAM_STAGE70_ONESHOT)
    /*
     * AVStream acquired and activated a device-lifetime VCM session when the
     * pin entered KSSTATE_ACQUIRE.  Map only this frame here; do not recreate
     * or tear down the SMMU graph while the streaming pin is running.
     */
    if (NabuOwnReadLong(&Context->AvVcmSessionState) != 2 ||
        Context->AvVcmRegion == NULL) {
        status = (NTSTATUS)NabuOwnReadLong(
            &Context->AvVcmSessionStatus);
        if (NT_SUCCESS(status)) {
            status = STATUS_DEVICE_NOT_READY;
        }
        goto Cleanup;
    }
    vcmRegion = Context->AvVcmRegion;
    vcmReserve = Context->AvVcmReserve;
    vcmRelease = Context->AvVcmRelease;
    vcmBack = Context->AvVcmBack;
    vcmUnback = Context->AvVcmUnback;
    vcmGetDevAddr = Context->AvVcmGetDevAddr;
    Context->FrontRawSmmuEnumerateStatus = STATUS_SUCCESS;
    Context->FrontRawSmmuOpenStatus = STATUS_SUCCESS;
    Context->FrontRawSmmuQueryStatus = STATUS_SUCCESS;
    Context->FrontRawSmmuGetConfigStatus = STATUS_SUCCESS;
    Context->FrontRawSmmuContextCount =
        (LONG)Context->AvVcmAssociated;
    Context->FrontRawSmmuInterfaceSize =
        (LONG)Context->AvSmmuInterface.Header.Size;
    Context->FrontRawSmmuInterfaceVersion =
        (LONG)Context->AvSmmuInterface.Header.Version;
    Context->FrontRawLegacyCallbackPresent = 1;
    Context->FrontRawLegacyCallbackRegistered =
        Context->AvLegacyCallbackRegistered ? 1 : 0;
    Context->FrontRawVcmCreateStatus = STATUS_SUCCESS;
    Context->FrontRawVcmAssociationCount =
        (LONG)Context->AvVcmAssociated;
    Context->FrontRawVcmActivationCount =
        (LONG)Context->AvVcmActivated;
    goto PersistentSessionReady;
#endif

    status = IoGetDeviceInterfaces(
        &NabuOwnSmmuDeviceInterfaceGuid, NULL, FALSE, &interfaceList);
    Context->FrontRawSmmuEnumerateStatus = (LONG)status;
    if (!NT_SUCCESS(status) || interfaceList == NULL ||
        interfaceList[0] == L'\0') {
        goto Cleanup;
    }
    RtlInitUnicodeString(&interfaceName, interfaceList);
    status = IoGetDeviceObjectPointer(
        &interfaceName, NABU_SMMU_DESIRED_ACCESS,
        &fileObject, &deviceObject);
    Context->FrontRawSmmuOpenStatus = (LONG)status;
    if (!NT_SUCCESS(status)) {
        goto Cleanup;
    }

    KeInitializeEvent(&event, NotificationEvent, FALSE);
    RtlZeroMemory(&ioStatus, sizeof(ioStatus));
    irp = IoBuildSynchronousFsdRequest(
        IRP_MJ_PNP, deviceObject, NULL, 0, NULL, &event, &ioStatus);
    if (irp == NULL) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        Context->FrontRawSmmuQueryStatus = (LONG)status;
        goto Cleanup;
    }
    stack = IoGetNextIrpStackLocation(irp);
    stack->MinorFunction = IRP_MN_QUERY_INTERFACE;
    stack->Parameters.QueryInterface.InterfaceType =
        (LPGUID)&NabuOwnSmmuQueryInterfaceGuid;
    stack->Parameters.QueryInterface.Size =
        (USHORT)sizeof(smmuInterface);
    stack->Parameters.QueryInterface.Version = 0;
    stack->Parameters.QueryInterface.Interface =
        (PINTERFACE)&smmuInterface;
    stack->Parameters.QueryInterface.InterfaceSpecificData = NULL;
    status = IoCallDriver(deviceObject, irp);
    if (status == STATUS_PENDING) {
        (VOID)KeWaitForSingleObject(
            &event, Executive, KernelMode, FALSE, NULL);
        status = ioStatus.Status;
    }
    Context->FrontRawSmmuQueryStatus = (LONG)status;
    if (!NT_SUCCESS(status) ||
        smmuInterface.Header.Size < NABU_SMMU_INTERFACE_BYTES) {
        if (NT_SUCCESS(status)) {
            status = STATUS_INFO_LENGTH_MISMATCH;
            Context->FrontRawSmmuQueryStatus = (LONG)status;
        }
        goto Cleanup;
    }

    table = (PUCHAR)&smmuInterface;
    Context->FrontRawSmmuInterfaceSize =
        (LONG)smmuInterface.Header.Size;
    Context->FrontRawSmmuInterfaceVersion =
        (LONG)smmuInterface.Header.Version;
    Context->FrontRawSmmuContextLow =
        (LONG)(ULONG_PTR)smmuInterface.Header.Context;
    Context->FrontRawSmmuContextHigh =
        (LONG)((ULONG_PTR)smmuInterface.Header.Context >> 32);
    legacyCallback = NabuOwnGetSmmuFunction(
        table, NABU_SMMU_LEGACY_CB_OFFSET);
    iommuGetConfig = NabuOwnGetSmmuFunction(
        table, NABU_IOMMU_GET_CONFIG_OFFSET);
    iommuCallback = NabuOwnGetSmmuFunction(
        table, NABU_IOMMU_CB_OFFSET);
    isIommu = (NABU_SMMU_IS_IOMMU)NabuOwnGetSmmuFunction(
        table, NABU_IOMMU_IS_IOMMU_OFFSET);
    Context->FrontRawLegacyCallbackPresent =
        legacyCallback != NULL ? 1 : 0;
    Context->FrontRawIommuGetConfigPresent =
        iommuGetConfig != NULL ? 1 : 0;
    Context->FrontRawIommuCallbackPresent =
        iommuCallback != NULL ? 1 : 0;
    Context->FrontRawIommuIsPresent = isIommu != NULL ? 1 : 0;
    if (isIommu != NULL) {
        Context->FrontRawIommuIsResult = isIommu() ? 1 : 0;
    }
    setState = (NABU_SMMU_SET_STATE)NabuOwnGetSmmuFunction(
        table, NABU_SMMU_SET_STATE_OFFSET);
    getConfig = (NABU_SMMU_GET_CONFIG)NabuOwnGetSmmuFunction(
        table, NABU_SMMU_GET_CONFIG_OFFSET);
    vcmCreate = (NABU_VCM_CREATE)NabuOwnGetSmmuFunction(
        table, NABU_VCM_CREATE_OFFSET);
    vcmFree = (NABU_VCM_FREE)NabuOwnGetSmmuFunction(
        table, NABU_VCM_FREE_OFFSET);
    vcmReserve = (NABU_VCM_RESERVE)NabuOwnGetSmmuFunction(
        table, NABU_VCM_RESERVE_OFFSET);
    vcmRelease = (NABU_VCM_HANDLE_STATUS)NabuOwnGetSmmuFunction(
        table, NABU_VCM_RELEASE_OFFSET);
    vcmBack = (NABU_VCM_BACK)NabuOwnGetSmmuFunction(
        table, NABU_VCM_BACK_OFFSET);
    vcmUnback = (NABU_VCM_HANDLE_STATUS)NabuOwnGetSmmuFunction(
        table, NABU_VCM_UNBACK_OFFSET);
    vcmAssoc = (NABU_VCM_ASSOC)NabuOwnGetSmmuFunction(
        table, NABU_VCM_ASSOC_OFFSET);
    vcmDeassoc = (NABU_VCM_HANDLE_STATUS)NabuOwnGetSmmuFunction(
        table, NABU_VCM_DEASSOC_OFFSET);
    vcmActivate = (NABU_VCM_HANDLE_STATUS)NabuOwnGetSmmuFunction(
        table, NABU_VCM_ACTIVATE_OFFSET);
    vcmDeactivate = (NABU_VCM_HANDLE_STATUS)NabuOwnGetSmmuFunction(
        table, NABU_VCM_DEACTIVATE_OFFSET);
    vcmGetDevAddr = (NABU_VCM_GET_DEV_ADDR)NabuOwnGetSmmuFunction(
        table, NABU_VCM_GET_DEV_ADDR_OFFSET);
    if (setState == NULL || getConfig == NULL ||
        vcmCreate == NULL || vcmFree == NULL ||
        vcmReserve == NULL || vcmRelease == NULL ||
        vcmBack == NULL || vcmUnback == NULL ||
        vcmAssoc == NULL || vcmDeassoc == NULL ||
        vcmActivate == NULL || vcmDeactivate == NULL ||
        vcmGetDevAddr == NULL) {
        status = STATUS_PROCEDURE_NOT_FOUND;
        Context->FrontRawSmmuGetConfigStatus = (LONG)status;
        goto Cleanup;
    }

    status = getConfig(
        smmuInterface.Header.Context,
        NABU_SMMU_CONFIG_SELECTOR, configuration);
    Context->FrontRawSmmuGetConfigStatus = (LONG)status;
    if (!NT_SUCCESS(status)) {
        goto Cleanup;
    }
    RtlCopyMemory(&rawCount, configuration, sizeof(rawCount));
    Context->FrontRawSmmuContextCount = (LONG)rawCount;
    if (rawCount == 0 || rawCount > NABU_SMMU_MAX_CONTEXT_BANKS) {
        status = STATUS_BUFFER_OVERFLOW;
        goto Cleanup;
    }
    if (rawCount > 0) {
        PUCHAR entry = configuration + sizeof(rawCount);
        RtlCopyMemory(&contextHandle, entry + sizeof(ULONGLONG),
                      sizeof(contextHandle));
        Context->FrontRawConfigEntry0Index = entry[0];
        Context->FrontRawConfigEntry0HandleLow = (LONG)contextHandle;
        Context->FrontRawConfigEntry0HandleHigh =
            (LONG)(contextHandle >> 32);
    }
    if (rawCount > 1) {
        PUCHAR entry = configuration + sizeof(rawCount) +
            NABU_SMMU_CONFIG_ENTRY_BYTES;
        RtlCopyMemory(&contextHandle, entry + sizeof(ULONGLONG),
                      sizeof(contextHandle));
        Context->FrontRawConfigEntry1Index = entry[0];
        Context->FrontRawConfigEntry1HandleLow = (LONG)contextHandle;
        Context->FrontRawConfigEntry1HandleHigh =
            (LONG)(contextHandle >> 32);
    }
    if (rawCount > 2) {
        PUCHAR entry = configuration + sizeof(rawCount) +
            (2UL * NABU_SMMU_CONFIG_ENTRY_BYTES);
        RtlCopyMemory(&contextHandle, entry + sizeof(ULONGLONG),
                      sizeof(contextHandle));
        Context->FrontRawConfigEntry2Index = entry[0];
        Context->FrontRawConfigEntry2HandleLow = (LONG)contextHandle;
        Context->FrontRawConfigEntry2HandleHigh =
            (LONG)(contextHandle >> 32);
    }

    Context->FrontRawPhase = 10;
    NabuOwnWriteDiagnostics(Context);
    vcmRegion = vcmCreate(0, NABU_VCM_REGION_BYTES);
    Context->FrontRawVcmCreateStatus =
        vcmRegion != NULL ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
    if (vcmRegion == NULL) {
        status = STATUS_UNSUCCESSFUL;
        goto Cleanup;
    }
    for (i = 0; i < (ULONG)rawCount; ++i) {
        PUCHAR entry = configuration + sizeof(rawCount) +
            (i * NABU_SMMU_CONFIG_ENTRY_BYTES);
        RtlCopyMemory(
            &contextHandle, entry + sizeof(ULONGLONG),
            sizeof(contextHandle));
        associations[i] = vcmAssoc(
            vcmRegion, (PVOID)(ULONG_PTR)contextHandle, 0);
        if (associations[i] == NULL) {
            status = STATUS_UNSUCCESSFUL;
            goto Cleanup;
        }
        associated += 1;
        Context->FrontRawVcmAssociationCount = (LONG)associated;
    }

    status = setState(
        smmuInterface.Header.Context, NABU_SMMU_CONFIG_SELECTOR);
    Context->FrontRawVcmSetStateStatus = (LONG)status;
    if (!NT_SUCCESS(status)) {
        goto Cleanup;
    }
    stateEnabled = TRUE;
    for (i = 0; i < associated; ++i) {
        status = vcmActivate(associations[i]);
        if (!NT_SUCCESS(status)) {
            goto Cleanup;
        }
        activated += 1;
        Context->FrontRawVcmActivationCount = (LONG)activated;
    }

#if defined(AVSTREAM_BUILD) && !defined(AVSTREAM_STAGE70_ONESHOT)
PersistentSessionReady:
#endif
#if !defined(AVSTREAM_BUILD) || defined(AVSTREAM_STAGE70_ONESHOT)
    mdl = IoAllocateMdl(
        rawBuffer, NABU_FRONT_BUFFER_BYTES, FALSE, FALSE, NULL);
    if (mdl == NULL) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        Context->FrontRawVcmBackStatus = (LONG)status;
        goto Cleanup;
    }
    MmBuildMdlForNonPagedPool(mdl);
#ifdef AVSTREAM_BUILD
    Context->AvRawMdlFlags = mdl->MdlFlags;
    Context->AvRawMdlByteCount = MmGetMdlByteCount(mdl);
    Context->AvRawMdlByteOffset = MmGetMdlByteOffset(mdl);
    {
        ULONG pageCount = BYTES_TO_PAGES(MmGetMdlByteCount(mdl));
        PPFN_NUMBER pfnArray = (PPFN_NUMBER)(mdl + 1);

        if (pageCount != 0) {
            PFN_NUMBER firstPfn = pfnArray[0];
            PFN_NUMBER lastPfn = pfnArray[pageCount - 1];

            Context->AvRawFirstPfnLow = (LONG)firstPfn;
            Context->AvRawFirstPfnHigh =
                (LONG)((ULONGLONG)firstPfn >> 32);
            Context->AvRawLastPfnLow = (LONG)lastPfn;
            Context->AvRawLastPfnHigh =
                (LONG)((ULONGLONG)lastPfn >> 32);
        }
    }
#endif
#endif
#if !defined(AVSTREAM_BUILD) || defined(AVSTREAM_STAGE70_ONESHOT)
    guardReservation = vcmReserve(
        vcmRegion, NABU_VCM_GUARD_BYTES, NABU_VCM_RESERVE_FLAGS);
    if (guardReservation == NULL) {
        status = STATUS_UNSUCCESSFUL;
        Context->FrontRawVcmReserveStatus = (LONG)status;
        goto Cleanup;
    }
#endif
    reserveBytes =
        ((ULONGLONG)NABU_FRONT_BUFFER_BYTES + 0x3FFFULL) &
        ~0x3FFFULL;
    reservation = vcmReserve(
        vcmRegion, reserveBytes, NABU_VCM_RESERVE_FLAGS);
    Context->FrontRawVcmReserveStatus =
        reservation != NULL ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
    if (reservation == NULL) {
        status = STATUS_UNSUCCESSFUL;
        goto Cleanup;
    }
    KeFlushIoBuffers(mdl, TRUE, TRUE);
    status = vcmBack(reservation, mdl);
    Context->FrontRawVcmBackStatus = (LONG)status;
    if (!NT_SUCCESS(status)) {
        goto Cleanup;
    }
    backed = TRUE;
    deviceAddress = vcmGetDevAddr(reservation);
    Context->FrontRawIovaLow = (LONG)deviceAddress;
    Context->FrontRawIovaHigh = (LONG)(deviceAddress >> 32);
    if (deviceAddress == 0 || deviceAddress == 1 ||
        deviceAddress > MAXULONG) {
        status = STATUS_CONFLICTING_ADDRESSES;
        goto Cleanup;
    }
#if defined(AVSTREAM_BUILD) && !defined(AVSTREAM_STAGE70_ONESHOT)
    rawBuffer = MmMapLockedPagesSpecifyCache(
        mdl,
        KernelMode,
        MmWriteCombined,
        NULL,
        FALSE,
        NormalPagePriority | MdlMappingNoExecute);
    if (rawBuffer == NULL) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }
    rawMapped = TRUE;
    RtlFillMemory(rawBuffer, NABU_FRONT_BUFFER_BYTES, 0xAB);
    KeMemoryBarrier();
    KeFlushIoBuffers(mdl, TRUE, TRUE);
#endif
    Context->FrontRawPhase = 20;
    NabuOwnWriteDiagnostics(Context);
    captureStatus = NabuOwnProbeFrontRdi(
        Context, Client, (PUCHAR)rawBuffer, deviceAddress, TRUE);
    status = captureStatus;
    KeFlushIoBuffers(mdl, TRUE, TRUE);

#ifdef AVSTREAM_BUILD
    if (NT_SUCCESS(captureStatus) &&
        NabuOwnReadLong(&Context->FrontRawBufferTailChanged) != 0 &&
        g_NabuFrontRawConsumer != NULL) {
        g_NabuFrontRawConsumer(
            g_NabuFrontRawConsumerContext,
            (const USHORT *)rawBuffer,
            NABU_FRONT_WIDTH,
            NABU_FRONT_HEIGHT);
    }
#else
    if (NabuOwnReadLong(&Context->FrontRawBufferTailChanged) != 0) {
        UNICODE_STRING fileName;
        OBJECT_ATTRIBUTES attributes;
        IO_STATUS_BLOCK fileIoStatus;
        HANDLE file = NULL;
        NTSTATUS fileStatus;

        RtlInitUnicodeString(
            &fileName,
            L"\\??\\C:\\CamTest\\nabu_stage70_front_3264x2448.raw16");
        InitializeObjectAttributes(
            &attributes, &fileName,
            OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
            NULL, NULL);
        fileStatus = ZwCreateFile(
            &file, GENERIC_WRITE | SYNCHRONIZE,
            &attributes, &fileIoStatus, NULL,
            FILE_ATTRIBUTE_NORMAL, 0, FILE_OVERWRITE_IF,
            FILE_SYNCHRONOUS_IO_NONALERT |
                FILE_NON_DIRECTORY_FILE,
            NULL, 0);
        Context->FrontRawFileStatus = (LONG)fileStatus;
        if (NT_SUCCESS(fileStatus) && file != NULL) {
            fileStatus = ZwWriteFile(
                file, NULL, NULL, NULL, &fileIoStatus,
                rawBuffer, NABU_FRONT_FRAME_BYTES, NULL, NULL);
            Context->FrontRawFileStatus = (LONG)fileStatus;
            if (NT_SUCCESS(fileStatus)) {
                Context->FrontRawFileBytes =
                    (LONG)fileIoStatus.Information;
            }
            ZwClose(file);
        }
        if (NT_SUCCESS(captureStatus) && !NT_SUCCESS(fileStatus)) {
            status = fileStatus;
        }
    }
#endif
    Context->FrontRawCompleted =
#ifdef AVSTREAM_BUILD
        (NT_SUCCESS(status) &&
         NabuOwnReadLong(&Context->FrontRawBufferTailChanged) != 0 &&
         g_NabuFrontRawConsumer != NULL) ? 1 : 0;
#else
        (NT_SUCCESS(status) &&
         NabuOwnReadLong(&Context->FrontRawBufferTailChanged) != 0 &&
         NabuOwnReadLong(&Context->FrontRawFileBytes) ==
             NABU_FRONT_FRAME_BYTES) ? 1 : 0;
#endif

Cleanup:
    /*
     * The capture routine has already stopped sensor -> WM -> CSID before
     * this reverse-order SMMU teardown.  Never unback a live write master.
     */
    finalStatus = status;
    Context->FrontRawPhase = 80;
    NabuOwnWriteDiagnostics(Context);
    if (backed && vcmUnback != NULL) {
        status = vcmUnback(reservation);
        Context->FrontRawVcmUnbackStatus = (LONG)status;
        cleanupOk = NT_SUCCESS(status) && cleanupOk;
    }
    if (reservation != NULL && vcmRelease != NULL) {
        status = vcmRelease(reservation);
        Context->FrontRawVcmReleaseStatus = (LONG)status;
        cleanupOk = NT_SUCCESS(status) && cleanupOk;
    }
    if (guardReservation != NULL && vcmRelease != NULL) {
        status = vcmRelease(guardReservation);
        cleanupOk = NT_SUCCESS(status) && cleanupOk;
    }
#if defined(AVSTREAM_BUILD) && !defined(AVSTREAM_STAGE70_ONESHOT)
    if (rawMapped && rawBuffer != NULL && mdl != NULL) {
        MmUnmapLockedPages(rawBuffer, mdl);
        rawBuffer = NULL;
        rawMapped = FALSE;
    }
    if (mdl != NULL) {
        MmFreePagesFromMdl(mdl);
        ExFreePool(mdl);
        mdl = NULL;
    }
#else
    if (mdl != NULL) {
        IoFreeMdl(mdl);
        mdl = NULL;
    }
#endif
#if !defined(AVSTREAM_BUILD) || defined(AVSTREAM_STAGE70_ONESHOT)
    while (activated != 0 && vcmDeactivate != NULL) {
        activated -= 1;
        status = vcmDeactivate(associations[activated]);
        Context->FrontRawVcmDeactivateStatus = (LONG)status;
        cleanupOk = NT_SUCCESS(status) && cleanupOk;
    }
    if (stateEnabled && setState != NULL) {
        status = setState(smmuInterface.Header.Context, 0);
        Context->FrontRawVcmSetStateOffStatus = (LONG)status;
        cleanupOk = NT_SUCCESS(status) && cleanupOk;
    }
    while (associated != 0 && vcmDeassoc != NULL) {
        associated -= 1;
        status = vcmDeassoc(associations[associated]);
        Context->FrontRawVcmDeassocStatus = (LONG)status;
        cleanupOk = NT_SUCCESS(status) && cleanupOk;
    }
    if (vcmRegion != NULL && vcmFree != NULL) {
        status = vcmFree(vcmRegion);
        Context->FrontRawVcmFreeStatus = (LONG)status;
        cleanupOk = NT_SUCCESS(status) && cleanupOk;
    }
    if (smmuInterface.Header.InterfaceDereference != NULL) {
        smmuInterface.Header.InterfaceDereference(
            smmuInterface.Header.Context);
    }
    if (fileObject != NULL) {
        ObDereferenceObject(fileObject);
    }
    if (interfaceList != NULL) {
        ExFreePool(interfaceList);
    }
#endif
#if !defined(AVSTREAM_BUILD) || defined(AVSTREAM_STAGE70_ONESHOT)
    if (rawBuffer != NULL) {
        ExFreePoolWithTag(rawBuffer, NABU_FRONT_RAW_POOL_TAG);
    }
#endif
    if (!cleanupOk && NT_SUCCESS(finalStatus)) {
        finalStatus = STATUS_DEVICE_HARDWARE_ERROR;
        Context->FrontRawCompleted = 0;
    }
    Context->FrontRawPhase =
        NabuOwnReadLong(&Context->FrontRawCompleted) != 0 ? 100 : 90;
    NabuOwnWriteDiagnostics(Context);
    return finalStatus;
}

static NTSTATUS
NabuOwnExecuteFrontId(
    _In_ WDFDEVICE Device,
    _Inout_ PNABU_OWN_DEVICE_CONTEXT Context,
    _In_ BOOLEAN ProgramSensor,
    _In_ BOOLEAN ProbePhy,
    _In_ BOOLEAN ProbeRdi,
    _In_ BOOLEAN CaptureRaw)
{
    PLATFORM_CLIENT client;
    UCHAR request[NABU_CCI_READ_BYTES];
    UCHAR receive[2];
    ULONG master = NABU_FRONT_CCI_MASTER;
    ULONG attempt;
    BOOLEAN opened = FALSE;
    BOOLEAN acquired1 = FALSE;
    BOOLEAN initialized = FALSE;
    BOOLEAN acquired2 = FALSE;
    NTSTATUS status;
    NTSTATUS cleanupStatus;

    RtlZeroMemory(&client, sizeof(client));
    NabuOwnSetFrontPhase(Context, 1);

    status = PlatformOpen(Device, &client);
    Context->FrontPlatformOpenStatus = (LONG)status;
    NabuOwnSetFrontPhase(Context, 2);
    if (!NT_SUCCESS(status)) {
        goto Cleanup;
    }
    opened = TRUE;

    status = PlatformDispatch(
        &client, NABU_PLAT_CMD_ACQUIRE, NULL, NULL, NULL);
    Context->FrontAcquire1Status = (LONG)status;
    NabuOwnSetFrontPhase(Context, 3);
    if (!NT_SUCCESS(status)) {
        goto Cleanup;
    }
    acquired1 = TRUE;

    status = PlatformPower(&client);
    Context->FrontPowerStatus = (LONG)status;
    NabuOwnSetFrontPhase(Context, 4);
    if (!NT_SUCCESS(status)) {
        goto Cleanup;
    }

    status = PlatformPinConfig(&client);
    Context->FrontPinStatus = (LONG)status;
    NabuOwnSetFrontPhase(Context, 5);
    if (!NT_SUCCESS(status)) {
        goto Cleanup;
    }

    status = PlatformInitMaster(&client, master);
    Context->FrontInitMasterStatus = (LONG)status;
    NabuOwnSetFrontPhase(Context, 6);
    if (!NT_SUCCESS(status)) {
        goto Cleanup;
    }
    initialized = TRUE;

    status = PlatformDispatch(
        &client, NABU_PLAT_CMD_ACQUIRE, NULL, NULL, NULL);
    Context->FrontAcquire2Status = (LONG)status;
    NabuOwnSetFrontPhase(Context, 7);
    if (!NT_SUCCESS(status)) {
        goto Cleanup;
    }
    acquired2 = TRUE;

    if (!NabuOwnPrepareFrontSensor(Context)) {
        status = STATUS_DEVICE_NOT_READY;
        NabuOwnSetFrontPhase(Context, 8);
        goto Cleanup;
    }
    NabuOwnSetFrontPhase(Context, 8);

    status = STATUS_UNSUCCESSFUL;
    for (attempt = 1; attempt <= 5; ++attempt) {
        RtlZeroMemory(receive, sizeof(receive));
        NabuOwnBuildCciRead(request, receive);
        status = PlatformDispatch(
            &client, PLAT_CMD_I2C_READ, NULL, NULL, request);
        Context->FrontCciReadStatus = (LONG)status;
        Context->FrontCciReadAttemptCount = (LONG)attempt;
        Context->FrontCciRxByte0 = receive[0];
        Context->FrontCciRxByte1 = receive[1];
        Context->FrontCciChipId =
            (LONG)(((ULONG)receive[0] << 8) | receive[1]);
        Context->FrontCciIdMatchesExpected =
            NabuOwnReadLong(&Context->FrontCciChipId) == 0x885AUL ?
                1 : 0;
        NabuOwnSetFrontPhase(Context, 9);
        if (Context->FrontCciIdMatchesExpected != 0) {
            status = STATUS_SUCCESS;
            Context->FrontCciReadStatus = (LONG)STATUS_SUCCESS;
            Context->FrontIdCompleted = 1;
            break;
        }
        KeStallExecutionProcessor(2000);
    }
    if (Context->FrontIdCompleted == 0 && NT_SUCCESS(status)) {
        status = STATUS_UNSUCCESSFUL;
    }
    if (ProgramSensor &&
        NabuOwnReadLong(&Context->FrontIdCompleted) != 0) {
        status = NabuOwnProgramFrontSensor(Context, &client);
    }
    if (ProbePhy && NT_SUCCESS(status)) {
        status = NabuOwnProbeFrontPhy(Context, &client);
    }
    if (CaptureRaw && NT_SUCCESS(status)) {
        status = NabuOwnCaptureFrontRaw(Context, &client);
    } else if (ProbeRdi && NT_SUCCESS(status)) {
        status = NabuOwnProbeFrontRdi(
            Context, &client, NULL, 0, FALSE);
    }

Cleanup:
    if (initialized) {
        cleanupStatus = PlatformDispatch(
            &client, PLAT_CMD_I2C_MASTER_DEINIT,
            &master, NULL, NULL);
        Context->FrontDeinitStatus = (LONG)cleanupStatus;
        NabuOwnSetFrontPhase(Context, 10);
    }
    if (acquired1) {
        cleanupStatus = PlatformDispatch(
            &client, NABU_PLAT_CMD_RELEASE, NULL, NULL, NULL);
        Context->FrontRelease1Status = (LONG)cleanupStatus;
        NabuOwnSetFrontPhase(Context, 11);
    }
    if (acquired2) {
        cleanupStatus = PlatformDispatch(
            &client, NABU_PLAT_CMD_RELEASE, NULL, NULL, NULL);
        Context->FrontRelease2Status = (LONG)cleanupStatus;
        NabuOwnSetFrontPhase(Context, 12);
    }
    if (opened) {
        PlatformClose(&client);
    }
    if (NabuOwnReadLong(&Context->FrontIdCompleted) != 0) {
        NabuOwnSetFrontPhase(Context, 100);
    }
    return status;
}

static NTSTATUS
NabuOwnRunFrontProbe(
    _In_ WDFDEVICE Device,
    _Inout_ PNABU_OWN_DEVICE_CONTEXT Context,
    _In_ BOOLEAN ProgramSensor,
    _In_ BOOLEAN ProbePhy,
    _In_ BOOLEAN ProbeRdi,
    _In_ BOOLEAN CaptureRaw)
{
    BOOLEAN d0Held = FALSE;
    NTSTATUS status;

    if (InterlockedCompareExchange(
            &Context->CommandInProgress, 1, 0) != 0) {
        return STATUS_DEVICE_BUSY;
    }

    InterlockedIncrement(&Context->CommandRunCount);
    InterlockedExchange(&Context->FrontIdAttempted, 1);
    InterlockedExchange(&Context->FrontIdPhase, 0);
    NabuOwnResetFrontStatus(Context);
    InterlockedExchange(&Context->D0StopIdleStatus,
                        (LONG)STATUS_NOT_SUPPORTED);
    InterlockedExchange(&Context->D0ResumeIdleCalled, 0);

    if (NabuOwnReadLong(&Context->DevicePrepared) == 0 ||
        NabuOwnReadLong(&Context->D0EntryCount) == 0 ||
        NabuOwnReadLong(&Context->DeviceInD0) == 0) {
        status = STATUS_DEVICE_NOT_READY;
        goto Finished;
    }

    status = WdfDeviceStopIdle(Device, TRUE);
    InterlockedExchange(&Context->D0StopIdleStatus, (LONG)status);
    if (!NT_SUCCESS(status)) {
        goto Finished;
    }
    d0Held = TRUE;

    InterlockedExchange(&Context->HardwareTouched, 1);
    status = NabuOwnExecuteFrontId(
        Device, Context, ProgramSensor, ProbePhy, ProbeRdi, CaptureRaw);

Finished:
    if (d0Held) {
        WdfDeviceResumeIdle(Device);
        InterlockedExchange(&Context->D0ResumeIdleCalled, 1);
    }
    InterlockedExchange(&Context->LastCommandStatus, (LONG)status);
    InterlockedExchange(&Context->CommandInProgress, 0);
    NabuOwnWriteDiagnostics(Context);
    return status;
}

static NTSTATUS
NabuOwnRunD0Probe(
    _In_ WDFDEVICE Device,
    _In_ PNABU_OWN_DEVICE_CONTEXT Context)
{
    NTSTATUS status;

    if (InterlockedCompareExchange(
            &Context->CommandInProgress, 1, 0) != 0) {
        return STATUS_DEVICE_BUSY;
    }

    InterlockedIncrement(&Context->CommandRunCount);
    InterlockedExchange(&Context->D0ProbeAttempted, 1);
    InterlockedExchange(&Context->D0StopIdleStatus,
                        (LONG)STATUS_NOT_SUPPORTED);
    InterlockedExchange(&Context->D0ResumeIdleCalled, 0);
    InterlockedExchange(&Context->D0ProbeCompleted, 0);

    if (InterlockedCompareExchange(
            &Context->DevicePrepared, 0, 0) == 0 ||
        InterlockedCompareExchange(
            &Context->D0EntryCount, 0, 0) == 0 ||
        InterlockedCompareExchange(
            &Context->DeviceInD0, 0, 0) == 0) {
        status = STATUS_DEVICE_NOT_READY;
    } else {
        status = WdfDeviceStopIdle(Device, TRUE);
        InterlockedExchange(&Context->D0StopIdleStatus, (LONG)status);
        if (NT_SUCCESS(status)) {
            WdfDeviceResumeIdle(Device);
            InterlockedExchange(&Context->D0ResumeIdleCalled, 1);
            InterlockedExchange(&Context->D0ProbeCompleted, 1);
        }
    }

    InterlockedExchange(&Context->LastCommandStatus, (LONG)status);
    InterlockedExchange(&Context->CommandInProgress, 0);
    NabuOwnWriteDiagnostics(Context);
    return status;
}

#ifndef AVSTREAM_BUILD
VOID
NabuOwnEvtIoDeviceControl(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode)
{
    PNABU_OWN_CONTROL_CONTEXT controlContext;
    PNABU_OWN_DEVICE_CONTEXT deviceContext;
    PNABU_CAM_MANUAL_STATUS output;
    WDFDEVICE targetDevice;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(InputBufferLength);

    if (IoControlCode != IOCTL_NABU_CAM_MANUAL_GET_STATUS &&
        IoControlCode != IOCTL_NABU_CAM_MANUAL_PROBE_D0 &&
        IoControlCode != IOCTL_NABU_CAM_MANUAL_PROBE_FRONT_ID &&
        IoControlCode != IOCTL_NABU_CAM_MANUAL_PROBE_FRONT_PROGRAM &&
        IoControlCode != IOCTL_NABU_CAM_MANUAL_PROBE_FRONT_PHY &&
        IoControlCode != IOCTL_NABU_CAM_MANUAL_PROBE_FRONT_RDI &&
        IoControlCode != IOCTL_NABU_CAM_MANUAL_PROBE_FRONT_FRAME &&
        IoControlCode != IOCTL_NABU_CAM_MANUAL_CAPTURE_FRONT_RAW) {
        WdfRequestComplete(Request, STATUS_NOT_SUPPORTED);
        return;
    }

    controlContext = NabuOwnGetControlContext(WdfIoQueueGetDevice(Queue));
    WdfWaitLockAcquire(controlContext->TargetLock, NULL);
    targetDevice = controlContext->TargetDevice;
    if (targetDevice != NULL) {
        WdfObjectReference(targetDevice);
    }
    WdfWaitLockRelease(controlContext->TargetLock);

    if (targetDevice == NULL) {
        WdfRequestComplete(Request, STATUS_DEVICE_NOT_READY);
        return;
    }

    deviceContext = NabuOwnGetDeviceContext(targetDevice);
    if (IoControlCode == IOCTL_NABU_CAM_MANUAL_GET_STATUS) {
        status = WdfRequestRetrieveOutputBuffer(Request,
                                                sizeof(*output),
                                                (PVOID *)&output,
                                                NULL);
        if (NT_SUCCESS(status)) {
            NabuOwnFillStatus(deviceContext, output);
            WdfRequestCompleteWithInformation(Request,
                                              STATUS_SUCCESS,
                                              sizeof(*output));
        } else {
            WdfRequestComplete(Request, status);
        }
    } else if (IoControlCode == IOCTL_NABU_CAM_MANUAL_PROBE_D0) {
        status = NabuOwnRunD0Probe(targetDevice, deviceContext);
        WdfRequestComplete(Request, status);
    } else if (
        IoControlCode == IOCTL_NABU_CAM_MANUAL_PROBE_FRONT_ID) {
        status = NabuOwnRunFrontProbe(
            targetDevice, deviceContext, FALSE, FALSE, FALSE, FALSE);
        WdfRequestComplete(Request, status);
    } else if (
        IoControlCode ==
            IOCTL_NABU_CAM_MANUAL_PROBE_FRONT_PROGRAM) {
        status = NabuOwnRunFrontProbe(
            targetDevice, deviceContext, TRUE, FALSE, FALSE, FALSE);
        WdfRequestComplete(Request, status);
    } else if (
        IoControlCode == IOCTL_NABU_CAM_MANUAL_PROBE_FRONT_PHY) {
        status = NabuOwnRunFrontProbe(
            targetDevice, deviceContext, TRUE, TRUE, FALSE, FALSE);
        WdfRequestComplete(Request, status);
    } else if (
        IoControlCode == IOCTL_NABU_CAM_MANUAL_CAPTURE_FRONT_RAW) {
        status = NabuOwnRunFrontProbe(
            targetDevice, deviceContext, TRUE, FALSE, FALSE, TRUE);
        WdfRequestComplete(Request, status);
    } else {
        status = NabuOwnRunFrontProbe(
            targetDevice, deviceContext, TRUE, FALSE, TRUE, FALSE);
        WdfRequestComplete(Request, status);
    }

    WdfObjectDereference(targetDevice);
}

NTSTATUS
NabuOwnEvtDeviceD0Entry(
    _In_ WDFDEVICE Device,
    _In_ WDF_POWER_DEVICE_STATE PreviousState)
{
    PNABU_OWN_DEVICE_CONTEXT context = NabuOwnGetDeviceContext(Device);

    UNREFERENCED_PARAMETER(PreviousState);

    InterlockedIncrement(&context->D0EntryCount);
    InterlockedExchange(&context->DeviceInD0, 1);
    NabuOwnWriteDiagnostics(context);
    return STATUS_SUCCESS;
}

NTSTATUS
NabuOwnEvtDeviceD0Exit(
    _In_ WDFDEVICE Device,
    _In_ WDF_POWER_DEVICE_STATE TargetState)
{
    PNABU_OWN_DEVICE_CONTEXT context = NabuOwnGetDeviceContext(Device);

    UNREFERENCED_PARAMETER(TargetState);

    InterlockedExchange(&context->DeviceInD0, 0);
    InterlockedIncrement(&context->D0ExitCount);
    NabuOwnWriteDiagnostics(context);
    return STATUS_SUCCESS;
}

NTSTATUS
NabuOwnEvtPrepareHardware(
    _In_ WDFDEVICE Device,
    _In_ WDFCMRESLIST ResourcesRaw,
    _In_ WDFCMRESLIST ResourcesTranslated)
{
    PNABU_OWN_DEVICE_CONTEXT context = NabuOwnGetDeviceContext(Device);
    NTSTATUS status;

    UNREFERENCED_PARAMETER(ResourcesRaw);
    UNREFERENCED_PARAMETER(ResourcesTranslated);

    if (context->DiagnosticKey == NULL) {
        status = WdfDeviceOpenRegistryKey(Device,
                                           PLUGPLAY_REGKEY_DEVICE,
                                           KEY_SET_VALUE,
                                           WDF_NO_OBJECT_ATTRIBUTES,
                                           &context->DiagnosticKey);
        context->DiagnosticKeyOpenStatus = (ULONG)status;
        if (!NT_SUCCESS(status)) {
            return status;
        }
    }

    context->PrepareCount += 1;
    InterlockedExchange(&context->DevicePrepared, 1);
    NabuOwnWriteDiagnostics(context);
    return STATUS_SUCCESS;
}

NTSTATUS
NabuOwnEvtReleaseHardware(
    _In_ WDFDEVICE Device,
    _In_ WDFCMRESLIST ResourcesTranslated)
{
    PNABU_OWN_DEVICE_CONTEXT context = NabuOwnGetDeviceContext(Device);

    UNREFERENCED_PARAMETER(ResourcesTranslated);

    InterlockedExchange(&context->DevicePrepared, 0);
    if (context->DiagnosticKey != NULL) {
        WdfRegistryClose(context->DiagnosticKey);
        context->DiagnosticKey = NULL;
    }
    return STATUS_SUCCESS;
}

VOID
NabuOwnEvtFdoCleanup(_In_ WDFOBJECT Object)
{
    WDFDEVICE controlDevice = g_NabuOwnControlDevice;
    PNABU_OWN_CONTROL_CONTEXT controlContext;

    UNREFERENCED_PARAMETER(Object);

    g_NabuOwnControlDevice = NULL;
    if (controlDevice == NULL) {
        return;
    }

    controlContext = NabuOwnGetControlContext(controlDevice);
    WdfWaitLockAcquire(controlContext->TargetLock, NULL);
    controlContext->TargetDevice = NULL;
    WdfWaitLockRelease(controlContext->TargetLock);
    WdfObjectDelete(controlDevice);
}

static NTSTATUS
NabuOwnCreateControlDevice(
    _In_ WDFDRIVER Driver,
    _In_ WDFDEVICE TargetDevice)
{
    DECLARE_CONST_UNICODE_STRING(sddl,
        L"D:P(A;;GA;;;SY)(A;;GA;;;BA)");
    DECLARE_CONST_UNICODE_STRING(deviceName,
        L"\\Device\\NabuCamOwnControl");
    DECLARE_CONST_UNICODE_STRING(symbolicLink,
        L"\\DosDevices\\Global\\NabuCamOwnControl");
    PWDFDEVICE_INIT controlInit;
    WDF_OBJECT_ATTRIBUTES controlAttributes;
    WDF_OBJECT_ATTRIBUTES lockAttributes;
    WDF_OBJECT_ATTRIBUTES queueAttributes;
    WDF_IO_QUEUE_CONFIG queueConfig;
    WDFDEVICE controlDevice;
    PNABU_OWN_CONTROL_CONTEXT controlContext;
    NTSTATUS status;

    controlInit = WdfControlDeviceInitAllocate(Driver, &sddl);
    if (controlInit == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    status = WdfDeviceInitAssignName(controlInit, &deviceName);
    if (!NT_SUCCESS(status)) {
        WdfDeviceInitFree(controlInit);
        return status;
    }

    WdfDeviceInitSetExclusive(controlInit, FALSE);
    WdfDeviceInitSetIoType(controlInit, WdfDeviceIoBuffered);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&controlAttributes,
                                            NABU_OWN_CONTROL_CONTEXT);
    controlAttributes.ExecutionLevel = WdfExecutionLevelPassive;
    status = WdfDeviceCreate(&controlInit,
                             &controlAttributes,
                             &controlDevice);
    if (!NT_SUCCESS(status)) {
        if (controlInit != NULL) {
            WdfDeviceInitFree(controlInit);
        }
        return status;
    }

    controlContext = NabuOwnGetControlContext(controlDevice);
    controlContext->TargetDevice = TargetDevice;

    WDF_OBJECT_ATTRIBUTES_INIT(&lockAttributes);
    lockAttributes.ParentObject = controlDevice;
    status = WdfWaitLockCreate(&lockAttributes,
                               &controlContext->TargetLock);
    if (!NT_SUCCESS(status)) {
        WdfObjectDelete(controlDevice);
        return status;
    }

    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig,
                                            WdfIoQueueDispatchParallel);
    queueConfig.PowerManaged = WdfFalse;
    queueConfig.EvtIoDeviceControl = NabuOwnEvtIoDeviceControl;
    WDF_OBJECT_ATTRIBUTES_INIT(&queueAttributes);
    queueAttributes.ExecutionLevel = WdfExecutionLevelPassive;
    queueAttributes.SynchronizationScope = WdfSynchronizationScopeNone;
    status = WdfIoQueueCreate(controlDevice,
                              &queueConfig,
                              &queueAttributes,
                              NULL);
    if (!NT_SUCCESS(status)) {
        WdfObjectDelete(controlDevice);
        return status;
    }

    status = WdfDeviceCreateSymbolicLink(controlDevice, &symbolicLink);
    if (!NT_SUCCESS(status)) {
        WdfObjectDelete(controlDevice);
        return status;
    }

    g_NabuOwnControlDevice = controlDevice;
    WdfControlFinishInitializing(controlDevice);
    return STATUS_SUCCESS;
}

NTSTATUS
NabuOwnEvtDeviceAdd(
    _In_ WDFDRIVER Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit)
{
    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_PNPPOWER_EVENT_CALLBACKS pnpCallbacks;
    WDFDEVICE device;
    PNABU_OWN_DEVICE_CONTEXT context;
    NTSTATUS status;

    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpCallbacks);
    pnpCallbacks.EvtDevicePrepareHardware = NabuOwnEvtPrepareHardware;
    pnpCallbacks.EvtDeviceReleaseHardware = NabuOwnEvtReleaseHardware;
    pnpCallbacks.EvtDeviceD0Entry = NabuOwnEvtDeviceD0Entry;
    pnpCallbacks.EvtDeviceD0Exit = NabuOwnEvtDeviceD0Exit;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpCallbacks);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes,
                                            NABU_OWN_DEVICE_CONTEXT);
    attributes.ExecutionLevel = WdfExecutionLevelPassive;
    attributes.EvtCleanupCallback = NabuOwnEvtFdoCleanup;
    status = WdfDeviceCreate(&DeviceInit, &attributes, &device);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    context = NabuOwnGetDeviceContext(device);
    RtlZeroMemory(context, sizeof(*context));
    InterlockedExchange(&context->LastCommandStatus,
                        (LONG)STATUS_NOT_SUPPORTED);
    InterlockedExchange(&context->D0StopIdleStatus,
                        (LONG)STATUS_NOT_SUPPORTED);
    NabuOwnResetFrontStatus(context);

    return NabuOwnCreateControlDevice(Driver, device);
}

NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    WDF_DRIVER_CONFIG config;

    g_NabuOwnControlDevice = NULL;
    WDF_DRIVER_CONFIG_INIT(&config, NabuOwnEvtDeviceAdd);
    return WdfDriverCreate(DriverObject,
                           RegistryPath,
                           WDF_NO_OBJECT_ATTRIBUTES,
                           &config,
                           WDF_NO_HANDLE);
}
#else

NTSTATUS
NabuFrontBackendInitialize(_In_ WDFDEVICE Device)
{
    WDF_OBJECT_ATTRIBUTES attributes;
    PNABU_OWN_DEVICE_CONTEXT context = NULL;
    NTSTATUS status;

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(
        &attributes, NABU_OWN_DEVICE_CONTEXT);
    status = WdfObjectAllocateContext(
        Device, &attributes, (PVOID *)&context);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    RtlZeroMemory(context, sizeof(*context));
    InterlockedExchange(
        &context->LastCommandStatus, (LONG)STATUS_NOT_SUPPORTED);
    InterlockedExchange(
        &context->D0StopIdleStatus, (LONG)STATUS_NOT_SUPPORTED);
    InterlockedExchange(&context->DevicePrepared, 1);
    InterlockedExchange(&context->D0EntryCount, 1);
    InterlockedExchange(&context->DeviceInD0, 1);
#ifdef AVSTREAM_BUILD
    InterlockedExchange(
        &context->AvVcmSessionStatus,
        (LONG)STATUS_NOT_SUPPORTED);
#endif
    NabuOwnResetFrontStatus(context);
    return STATUS_SUCCESS;
}

NTSTATUS
NabuFrontBackendAcquire(_In_ WDFDEVICE Device)
{
#ifdef AVSTREAM_BUILD
    PNABU_OWN_DEVICE_CONTEXT context =
        NabuOwnGetDeviceContext(Device);

    if (KeGetCurrentIrql() != PASSIVE_LEVEL) {
        return STATUS_INVALID_DEVICE_STATE;
    }
#ifdef AVSTREAM_STAGE70_ONESHOT
    /*
     * AV18 intentionally does not create a long-lived VCM at ACQUIRE.
     * NabuOwnCaptureFrontRaw performs the complete successful Stage70
     * lifecycle synchronously at RUN and tears it down after the frame.
     */
    InterlockedExchange(&context->AvVcmSessionState, 0);
    InterlockedExchange(
        &context->AvVcmSessionStatus, (LONG)STATUS_NOT_SUPPORTED);
    InterlockedExchange(&context->AvVcmPersistent, 0);
    return STATUS_SUCCESS;
#else
    return NabuFrontInitializePersistentVcm(context);
#endif
#else
    UNREFERENCED_PARAMETER(Device);
    return STATUS_NOT_SUPPORTED;
#endif
}

VOID
NabuFrontBackendRelease(_In_ WDFDEVICE Device)
{
#ifdef AVSTREAM_BUILD
    PNABU_OWN_DEVICE_CONTEXT context =
        NabuOwnGetDeviceContext(Device);

    if (KeGetCurrentIrql() != PASSIVE_LEVEL) {
        return;
    }
#ifdef AVSTREAM_STAGE70_ONESHOT
    /*
     * The one-shot capture has already released its frame reservation,
     * associations, VCM region, queried interface and pool MDL.
     */
    InterlockedExchange(&context->AvVcmSessionState, 0);
    InterlockedExchange(
        &context->AvVcmSessionStatus, (LONG)STATUS_NOT_SUPPORTED);
    InterlockedExchange(&context->AvVcmPersistent, 0);
    return;
#else
    if (InterlockedCompareExchange(
            &context->CommandInProgress, 0, 0) != 0) {
        return;
    }
    InterlockedExchange(&context->AvVcmSessionState, 4);
    NabuFrontReleasePersistentVcm(context);
    InterlockedExchange(
        &context->AvVcmSessionStatus,
        (LONG)STATUS_NOT_SUPPORTED);
    InterlockedExchange(&context->AvVcmSessionState, 0);
#endif
#else
    UNREFERENCED_PARAMETER(Device);
#endif
}

NTSTATUS
NabuFrontBackendCapture(
    _In_ WDFDEVICE Device,
    _In_ NABU_FRONT_RAW_CONSUMER Consumer,
    _In_opt_ PVOID ConsumerContext)
{
    PNABU_OWN_DEVICE_CONTEXT context =
        NabuOwnGetDeviceContext(Device);
    NTSTATUS status;

    if (Consumer == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (InterlockedCompareExchange(
            &context->CommandInProgress, 1, 0) != 0) {
        return STATUS_DEVICE_BUSY;
    }

    InterlockedIncrement(&context->CommandRunCount);
    InterlockedExchange(&context->FrontIdAttempted, 1);
    InterlockedExchange(&context->FrontIdPhase, 0);
    NabuOwnResetFrontStatus(context);
    InterlockedExchange(&context->HardwareTouched, 1);

    g_NabuFrontRawConsumer = Consumer;
    g_NabuFrontRawConsumerContext = ConsumerContext;
    status = NabuOwnExecuteFrontId(
        Device, context, TRUE, FALSE, FALSE, TRUE);
    g_NabuFrontRawConsumer = NULL;
    g_NabuFrontRawConsumerContext = NULL;

    if (NT_SUCCESS(status) &&
        NabuOwnReadLong(&context->FrontRawCompleted) == 0) {
        status = STATUS_IO_DEVICE_ERROR;
    }
    InterlockedExchange(&context->LastCommandStatus, (LONG)status);
    InterlockedExchange(&context->CommandInProgress, 0);
    return status;
}

VOID
NabuFrontBackendWriteDiagnostics(
    _In_ WDFDEVICE Device,
    _In_ NABU_FRONT_DIAGNOSTIC_WRITER Writer,
    _In_opt_ PVOID WriterContext)
{
    PNABU_OWN_DEVICE_CONTEXT context;

    if (Device == NULL || Writer == NULL) {
        return;
    }
    context = NabuOwnGetDeviceContext(Device);

#define NABU_WRITE_BACKEND(Name, Field) \
    Writer(WriterContext, L"NabuAv_Backend" Name, \
           NabuOwnReadLong(&context->Field))

    NABU_WRITE_BACKEND(L"LastStatus", LastCommandStatus);
    NABU_WRITE_BACKEND(L"IdPhase", FrontIdPhase);
    NABU_WRITE_BACKEND(L"IdCompleted", FrontIdCompleted);
    NABU_WRITE_BACKEND(L"PlatformOpenStatus", FrontPlatformOpenStatus);
    NABU_WRITE_BACKEND(L"Acquire1Status", FrontAcquire1Status);
    NABU_WRITE_BACKEND(L"PowerStatus", FrontPowerStatus);
    NABU_WRITE_BACKEND(L"PinStatus", FrontPinStatus);
    NABU_WRITE_BACKEND(L"InitMasterStatus", FrontInitMasterStatus);
    NABU_WRITE_BACKEND(L"Acquire2Status", FrontAcquire2Status);
    NABU_WRITE_BACKEND(L"PrepareReady", FrontPrepareReady);
    NABU_WRITE_BACKEND(L"CciReadStatus", FrontCciReadStatus);
    NABU_WRITE_BACKEND(L"CciChipId", FrontCciChipId);
    NABU_WRITE_BACKEND(L"CciIdMatches", FrontCciIdMatchesExpected);
    NABU_WRITE_BACKEND(L"ProgramAttempted", FrontProgramAttempted);
    NABU_WRITE_BACKEND(L"ProgramPhase", FrontProgramPhase);
    NABU_WRITE_BACKEND(L"ProgramCompleted", FrontProgramCompleted);
    NABU_WRITE_BACKEND(L"InitStatus", FrontInitStatus);
    NABU_WRITE_BACKEND(L"ModeStatus", FrontModeStatus);
    NABU_WRITE_BACKEND(L"GeometryStatus", FrontGeometryStatus);
    NABU_WRITE_BACKEND(L"StreamWriteStatus", FrontStreamWriteStatus);
    NABU_WRITE_BACKEND(L"StreamReadStatus", FrontStreamReadStatus);
    NABU_WRITE_BACKEND(L"StreamReadback", FrontStreamReadback);
    NABU_WRITE_BACKEND(L"StreamStopStatus", FrontStreamStopStatus);
    NABU_WRITE_BACKEND(L"RtBandwidthStatus", FrontRtBandwidthStatus);
    NABU_WRITE_BACKEND(L"RawAttempted", FrontRawAttempted);
    NABU_WRITE_BACKEND(L"RawPhase", FrontRawPhase);
    NABU_WRITE_BACKEND(L"RawCompleted", FrontRawCompleted);
    NABU_WRITE_BACKEND(L"RawBufferAllocStatus", FrontRawBufferAllocStatus);
    NABU_WRITE_BACKEND(L"SmmuEnumerateStatus", FrontRawSmmuEnumerateStatus);
    NABU_WRITE_BACKEND(L"SmmuOpenStatus", FrontRawSmmuOpenStatus);
    NABU_WRITE_BACKEND(L"SmmuQueryStatus", FrontRawSmmuQueryStatus);
    NABU_WRITE_BACKEND(L"SmmuGetConfigStatus", FrontRawSmmuGetConfigStatus);
    NABU_WRITE_BACKEND(L"SmmuContextCount", FrontRawSmmuContextCount);
    NABU_WRITE_BACKEND(L"SmmuInterfaceSize", FrontRawSmmuInterfaceSize);
    NABU_WRITE_BACKEND(L"SmmuInterfaceVersion", FrontRawSmmuInterfaceVersion);
    NABU_WRITE_BACKEND(L"SmmuContextLow", FrontRawSmmuContextLow);
    NABU_WRITE_BACKEND(L"SmmuContextHigh", FrontRawSmmuContextHigh);
    NABU_WRITE_BACKEND(L"LegacyCallbackPresent", FrontRawLegacyCallbackPresent);
    NABU_WRITE_BACKEND(L"LegacyCallbackRegistered", FrontRawLegacyCallbackRegistered);
    NABU_WRITE_BACKEND(L"LegacyCallbackRemoved", FrontRawLegacyCallbackRemoved);
    NABU_WRITE_BACKEND(L"IommuGetConfigPresent", FrontRawIommuGetConfigPresent);
    NABU_WRITE_BACKEND(L"IommuCallbackPresent", FrontRawIommuCallbackPresent);
    NABU_WRITE_BACKEND(L"IommuIsPresent", FrontRawIommuIsPresent);
    NABU_WRITE_BACKEND(L"IommuIsResult", FrontRawIommuIsResult);
    NABU_WRITE_BACKEND(L"ConfigEntry0Index", FrontRawConfigEntry0Index);
    NABU_WRITE_BACKEND(L"ConfigEntry0HandleLow", FrontRawConfigEntry0HandleLow);
    NABU_WRITE_BACKEND(L"ConfigEntry0HandleHigh", FrontRawConfigEntry0HandleHigh);
    NABU_WRITE_BACKEND(L"ConfigEntry1Index", FrontRawConfigEntry1Index);
    NABU_WRITE_BACKEND(L"ConfigEntry1HandleLow", FrontRawConfigEntry1HandleLow);
    NABU_WRITE_BACKEND(L"ConfigEntry1HandleHigh", FrontRawConfigEntry1HandleHigh);
    NABU_WRITE_BACKEND(L"ConfigEntry2Index", FrontRawConfigEntry2Index);
    NABU_WRITE_BACKEND(L"ConfigEntry2HandleLow", FrontRawConfigEntry2HandleLow);
    NABU_WRITE_BACKEND(L"ConfigEntry2HandleHigh", FrontRawConfigEntry2HandleHigh);
    NABU_WRITE_BACKEND(L"VcmCreateStatus", FrontRawVcmCreateStatus);
    NABU_WRITE_BACKEND(L"VcmAssociationCount", FrontRawVcmAssociationCount);
    NABU_WRITE_BACKEND(L"VcmSetStateStatus", FrontRawVcmSetStateStatus);
    NABU_WRITE_BACKEND(L"VcmActivationCount", FrontRawVcmActivationCount);
    NABU_WRITE_BACKEND(L"VcmReserveStatus", FrontRawVcmReserveStatus);
    NABU_WRITE_BACKEND(L"VcmBackStatus", FrontRawVcmBackStatus);
    NABU_WRITE_BACKEND(L"IovaLow", FrontRawIovaLow);
    NABU_WRITE_BACKEND(L"IovaHigh", FrontRawIovaHigh);
    NABU_WRITE_BACKEND(L"BufferChanged", FrontRawBufferChanged);
    NABU_WRITE_BACKEND(L"BufferTailChanged", FrontRawBufferTailChanged);
#ifdef AVSTREAM_BUILD
    NABU_WRITE_BACKEND(L"VcmSessionState", AvVcmSessionState);
    NABU_WRITE_BACKEND(L"VcmSessionStatus", AvVcmSessionStatus);
    NABU_WRITE_BACKEND(L"VcmPersistent", AvVcmPersistent);
    NABU_WRITE_BACKEND(L"VcmStockGuardStatus", AvVcmStockGuardStatus);
    NABU_WRITE_BACKEND(L"RawMdlFlags", AvRawMdlFlags);
    NABU_WRITE_BACKEND(L"RawMdlByteCount", AvRawMdlByteCount);
    NABU_WRITE_BACKEND(L"RawMdlByteOffset", AvRawMdlByteOffset);
    NABU_WRITE_BACKEND(L"RawFirstPfnLow", AvRawFirstPfnLow);
    NABU_WRITE_BACKEND(L"RawFirstPfnHigh", AvRawFirstPfnHigh);
    NABU_WRITE_BACKEND(L"RawLastPfnLow", AvRawLastPfnLow);
    NABU_WRITE_BACKEND(L"RawLastPfnHigh", AvRawLastPfnHigh);
#endif

#undef NABU_WRITE_BACKEND
}

VOID
NabuFrontBackendCleanup(_In_ WDFDEVICE Device)
{
    PNABU_OWN_DEVICE_CONTEXT context =
        NabuOwnGetDeviceContext(Device);

    InterlockedExchange(&context->DeviceInD0, 0);
    InterlockedExchange(&context->DevicePrepared, 0);
#ifdef AVSTREAM_BUILD
#ifndef AVSTREAM_STAGE70_ONESHOT
    InterlockedExchange(&context->AvVcmSessionState, 4);
    NabuFrontReleasePersistentVcm(context);
#endif
#endif
    g_NabuFrontRawConsumer = NULL;
    g_NabuFrontRawConsumerContext = NULL;
}

#endif
