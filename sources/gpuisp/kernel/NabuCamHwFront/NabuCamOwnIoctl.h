#pragma once

static const GUID GUID_DEVINTERFACE_NABU_CAM_MANUAL =
    { 0xdf322a69, 0x46e7, 0x4a0b,
      { 0x8a, 0x01, 0x21, 0x62, 0xd9, 0x7d, 0xf9, 0x85 } };

#define NABU_CAM_MANUAL_PROTOCOL_VERSION 18UL

#define IOCTL_NABU_CAM_MANUAL_GET_STATUS \
    CTL_CODE(0x8000, 0x800, METHOD_BUFFERED, FILE_READ_ACCESS)
#define IOCTL_NABU_CAM_MANUAL_PROBE_D0 \
    CTL_CODE(0x8000, 0x802, METHOD_BUFFERED, \
             FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_NABU_CAM_MANUAL_PROBE_FRONT_ID \
    CTL_CODE(0x8000, 0x803, METHOD_BUFFERED, \
             FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_NABU_CAM_MANUAL_PROBE_FRONT_PROGRAM \
    CTL_CODE(0x8000, 0x804, METHOD_BUFFERED, \
             FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_NABU_CAM_MANUAL_PROBE_FRONT_PHY \
    CTL_CODE(0x8000, 0x805, METHOD_BUFFERED, \
             FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_NABU_CAM_MANUAL_PROBE_FRONT_RDI \
    CTL_CODE(0x8000, 0x80E, METHOD_BUFFERED, \
             FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_NABU_CAM_MANUAL_PROBE_FRONT_FRAME \
    CTL_CODE(0x8000, 0x80F, METHOD_BUFFERED, \
             FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_NABU_CAM_MANUAL_CAPTURE_FRONT_RAW \
    CTL_CODE(0x8000, 0x810, METHOD_BUFFERED, \
             FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_NABU_CAM_HW_CAPTURE_FRONT_RAW_BUFFER \
    CTL_CODE(0x8000, 0x811, METHOD_OUT_DIRECT, \
             FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_NABU_CAM_HW_FRONT_SESSION_START \
    CTL_CODE(0x8000, 0x812, METHOD_BUFFERED, \
             FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_NABU_CAM_HW_FRONT_SESSION_GET_FRAME \
    CTL_CODE(0x8000, 0x813, METHOD_OUT_DIRECT, \
             FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_NABU_CAM_HW_FRONT_SESSION_STOP \
    CTL_CODE(0x8000, 0x814, METHOD_BUFFERED, \
             FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_NABU_CAM_HW_GET_DEVICE_IDENTITY \
    CTL_CODE(0x8000, 0x815, METHOD_BUFFERED, FILE_READ_ACCESS)

// Build008 native RAW transport: unchanged 1632x1224 sensor/DMA mode; no CPU resize.
#define NABU_FRONT_PREVIEW_RAW_WIDTH  1632UL
#define NABU_FRONT_PREVIEW_RAW_HEIGHT 1224UL
#define NABU_FRONT_PREVIEW_RAW_STRIDE \
    (NABU_FRONT_PREVIEW_RAW_WIDTH * sizeof(USHORT))
#define NABU_FRONT_PREVIEW_RAW_BYTES \
    (NABU_FRONT_PREVIEW_RAW_STRIDE * NABU_FRONT_PREVIEW_RAW_HEIGHT)

#define NABU_CAM_PHYSICAL_LOCATION_MAX 256UL

typedef struct _NABU_CAM_DEVICE_IDENTITY {
    ULONG Size;
    ULONG PhysicalLocationStatus;
    ULONG PhysicalLocationType;
    ULONG PhysicalLocationBytes;
    ULONG ContainerIdStatus;
    ULONG ContainerIdType;
    ULONG ContainerIdBytes;
    GUID ContainerId;
    UCHAR PhysicalLocation[NABU_CAM_PHYSICAL_LOCATION_MAX];
} NABU_CAM_DEVICE_IDENTITY, *PNABU_CAM_DEVICE_IDENTITY;

typedef struct _NABU_CAM_MANUAL_STATUS {
    ULONG Size;
    ULONG ProtocolVersion;
    ULONG BuildTag;
    ULONG Stage;
    ULONG DevicePrepared;
    ULONG HardwareTouched;
    ULONG CommandInProgress;
    ULONG CommandRunCount;
    ULONG LastCommandStatus;
    ULONG CciChipId;
    ULONG FrontProgramCompleted;
    ULONG FrontStreamReadback;
    ULONG FrontPhyCompleted;
    ULONG FrontCsidCompleted;
    ULONG FrontCsidPacketsBefore;
    ULONG FrontCsidPacketsAfter;
    ULONG FrontCsidRdiIrqAfter;
    ULONG FrontCsidEccStats;
    ULONG FrontCsidCrcErrors;
    ULONG D0EntryCount;
    ULONG D0ExitCount;
    ULONG DeviceInD0;
    ULONG D0ProbeAttempted;
    ULONG D0StopIdleStatus;
    ULONG D0ResumeIdleCalled;
    ULONG D0ProbeCompleted;
    ULONG FrontIdAttempted;
    ULONG FrontIdPhase;
    ULONG FrontIdCompleted;
    ULONG FrontPlatformOpenStatus;
    ULONG FrontAcquire1Status;
    ULONG FrontPowerStatus;
    ULONG FrontPinStatus;
    ULONG FrontInitMasterStatus;
    ULONG FrontAcquire2Status;
    ULONG FrontRailsReady;
    ULONG FrontSpmiInitStatus;
    ULONG FrontVioEnAfter;
    ULONG FrontVioStatusAfter;
    ULONG FrontVdigModeAfter;
    ULONG FrontVdigOutAfter;
    ULONG FrontVdigEnAfter;
    ULONG FrontPrepareReady;
    ULONG FrontMclk1Cmd;
    ULONG FrontMclk1Cfg;
    ULONG FrontMclk1Cbcr;
    ULONG FrontResetIo;
    ULONG FrontCciReadStatus;
    ULONG FrontCciReadAttemptCount;
    ULONG FrontCciRxByte0;
    ULONG FrontCciRxByte1;
    ULONG FrontCciChipId;
    ULONG FrontCciIdMatchesExpected;
    ULONG FrontDeinitStatus;
    ULONG FrontRelease1Status;
    ULONG FrontRelease2Status;
    ULONG FrontProgramAttempted;
    ULONG FrontProgramPhase;
    ULONG FrontInitStatus;
    ULONG FrontInitFailIndex;
    ULONG FrontInitFailReg;
    ULONG FrontModeStatus;
    ULONG FrontModeFailIndex;
    ULONG FrontModeFailReg;
    ULONG FrontGeometryStatus;
    ULONG FrontWidth;
    ULONG FrontHeight;
    ULONG FrontStreamWriteStatus;
    ULONG FrontStreamReadStatus;
    ULONG FrontStreamStopStatus;
    ULONG FrontProgramLastTable;
    ULONG FrontProgramLastIndex;
    ULONG FrontProgramLastReg;
    ULONG FrontProgramLastStatus;
    ULONG FrontRtBandwidthStatus;
    ULONG FrontRtBandwidthReleaseStatus;
    ULONG FrontTitanGdscBefore;
    ULONG FrontTitanGdscAfter;
    ULONG FrontTitanGdscCfgAfter;
    ULONG FrontPll0LockOk;
    ULONG FrontCphyRxCmd;
    ULONG FrontCphyRxCfg;
    ULONG FrontPhyTimerCmd;
    ULONG FrontPhyTimerCfg;
    ULONG FrontPhyTimerCbcr;
    ULONG FrontPhyClockCbcr;
    ULONG FrontClockTreeReady;
    ULONG FrontPhyAttempted;
    ULONG FrontPhyPhase;
    ULONG FrontPhyHwVersion;
    ULONG FrontPhyLaneMask;
    ULONG FrontPhyLaneEnable;
    ULONG FrontPhySettleCount;
    ULONG FrontPhyIrq5Before;
    ULONG FrontPhyIrq8Before;
    ULONG FrontPhyIrq5After;
    ULONG FrontPhyIrq8After;
    ULONG FrontPhyActivityDetected;
    ULONG FrontCsidAttempted;
    ULONG FrontCsidPhase;
    ULONG FrontCsidHwVersion;
    ULONG FrontCsidResetTopIrq;
    ULONG FrontCsidResetPolls;
    ULONG FrontCsidRxCfg0;
    ULONG FrontCsidRxCfg1;
    ULONG FrontCsidRdiCfg0;
    ULONG FrontCsidRxIrqAfter;
    ULONG FrontCsidTopIrqAfter;
    ULONG FrontCsidLongPkt0;
    ULONG FrontCsidLongPkt1;
    ULONG FrontCsidRdiBytesPing;
    ULONG FrontCsidRdiBytesPong;
    ULONG FrontCsidRdiStatus;
    ULONG FrontCsidPolls;
    ULONG FrontCsidRestoreCompleted;
    ULONG FrontFrameSofSeen;
    ULONG FrontFrameEofSeen;
    ULONG FrontFrameComplete;
    ULONG FrontFramePacketDelta;
    ULONG FrontFrameCaptureCtrl;
    ULONG FrontFrameVirtualChannel;
    ULONG FrontFrameDataType;
    ULONG FrontFrameWordCount;
    ULONG FrontFrameLongPacketMatches;
    ULONG FrontFrameRxErrorMask;
    ULONG FrontFrameRdiErrorMask;
    ULONG FrontFrameMeasure0;
    ULONG FrontFrameMeasure1;
    ULONG FrontFrameMeasure2;
    ULONG FrontRawAttempted;
    ULONG FrontRawPhase;
    ULONG FrontRawCompleted;
    ULONG FrontRawBufferAllocStatus;
    ULONG FrontRawBufferBytes;
    ULONG FrontRawSmmuEnumerateStatus;
    ULONG FrontRawSmmuOpenStatus;
    ULONG FrontRawSmmuQueryStatus;
    ULONG FrontRawSmmuGetConfigStatus;
    ULONG FrontRawSmmuContextCount;
    ULONG FrontRawVcmCreateStatus;
    ULONG FrontRawVcmAssociationCount;
    ULONG FrontRawVcmSetStateStatus;
    ULONG FrontRawVcmActivationCount;
    ULONG FrontRawVcmReserveStatus;
    ULONG FrontRawVcmBackStatus;
    ULONG FrontRawIovaLow;
    ULONG FrontRawIovaHigh;
    ULONG FrontRawPll3LockOk;
    ULONG FrontRawVfeHwVersion;
    ULONG FrontRawVfeResetStatus;
    ULONG FrontRawVfeResetPolls;
    ULONG FrontRawWmCfg;
    ULONG FrontRawWmImageAddress;
    ULONG FrontRawWmStatus0;
    ULONG FrontRawWmStatus1;
    ULONG FrontRawVfeTopIrq0;
    ULONG FrontRawVfeTopIrq1;
    ULONG FrontRawVfeBusIrq0;
    ULONG FrontRawVfeBusIrq1;
    ULONG FrontRawVfeBusIrq2;
    ULONG FrontRawVfeBusDebug0;
    ULONG FrontRawWaitPolls;
    ULONG FrontRawWaitExitReason;
    ULONG FrontRawBufferSample0;
    ULONG FrontRawBufferSample1;
    ULONG FrontRawBufferSample2;
    ULONG FrontRawBufferSample3;
    ULONG FrontRawBufferTail0;
    ULONG FrontRawBufferTail1;
    ULONG FrontRawBufferChanged;
    ULONG FrontRawBufferTailChanged;
    ULONG FrontRawFileStatus;
    ULONG FrontRawFileBytes;
    ULONG FrontRawVcmUnbackStatus;
    ULONG FrontRawVcmReleaseStatus;
    ULONG FrontRawVcmDeactivateStatus;
    ULONG FrontRawVcmSetStateOffStatus;
    ULONG FrontRawVcmDeassocStatus;
    ULONG FrontRawVcmFreeStatus;
} NABU_CAM_MANUAL_STATUS, *PNABU_CAM_MANUAL_STATUS;

