#include <windows.h>
#include <stdio.h>
#include <wchar.h>
#include "NabuCamOwnIoctl.h"

static HANDLE
OpenNabuManualControl(void)
{
    HANDLE device;

    device = CreateFileW(L"\\\\.\\NabuCamOwnControl",
                         GENERIC_READ | GENERIC_WRITE,
                         FILE_SHARE_READ | FILE_SHARE_WRITE,
                         NULL,
                         OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL,
                         NULL);
    if (device == INVALID_HANDLE_VALUE) {
        wprintf(L"Cannot open manual camera control (Win32=%lu).\n",
                GetLastError());
    }
    return device;
}

static void
PrintStatus(const NABU_CAM_MANUAL_STATUS *status)
{
    wprintf(L"BuildTag                 : %lu\n", status->BuildTag);
    wprintf(L"Stage                    : %lu\n", status->Stage);
    wprintf(L"DevicePrepared           : %lu\n", status->DevicePrepared);
    wprintf(L"HardwareTouched          : %lu\n", status->HardwareTouched);
    wprintf(L"CommandInProgress        : %lu\n", status->CommandInProgress);
    wprintf(L"CommandRunCount          : %lu\n", status->CommandRunCount);
    wprintf(L"LastCommandStatus        : 0x%08lX\n", status->LastCommandStatus);
    wprintf(L"D0EntryCount             : %lu\n", status->D0EntryCount);
    wprintf(L"D0ExitCount              : %lu\n", status->D0ExitCount);
    wprintf(L"DeviceInD0               : %lu\n", status->DeviceInD0);
    wprintf(L"D0ProbeAttempted         : %lu\n", status->D0ProbeAttempted);
    wprintf(L"D0StopIdleStatus         : 0x%08lX\n", status->D0StopIdleStatus);
    wprintf(L"D0ResumeIdleCalled       : %lu\n", status->D0ResumeIdleCalled);
    wprintf(L"D0ProbeCompleted         : %lu\n", status->D0ProbeCompleted);
    wprintf(L"FrontIdAttempted         : %lu\n", status->FrontIdAttempted);
    wprintf(L"FrontIdPhase             : %lu\n", status->FrontIdPhase);
    wprintf(L"FrontIdCompleted         : %lu\n", status->FrontIdCompleted);
    wprintf(L"FrontPlatformOpenStatus  : 0x%08lX\n", status->FrontPlatformOpenStatus);
    wprintf(L"FrontAcquire1Status      : 0x%08lX\n", status->FrontAcquire1Status);
    wprintf(L"FrontPowerStatus         : 0x%08lX\n", status->FrontPowerStatus);
    wprintf(L"FrontPinStatus           : 0x%08lX\n", status->FrontPinStatus);
    wprintf(L"FrontInitMasterStatus    : 0x%08lX\n", status->FrontInitMasterStatus);
    wprintf(L"FrontAcquire2Status      : 0x%08lX\n", status->FrontAcquire2Status);
    wprintf(L"FrontRailsReady          : %lu\n", status->FrontRailsReady);
    wprintf(L"FrontSpmiInitStatus      : 0x%08lX\n", status->FrontSpmiInitStatus);
    wprintf(L"FrontVioEnAfter          : 0x%08lX\n", status->FrontVioEnAfter);
    wprintf(L"FrontVioStatusAfter      : 0x%08lX\n", status->FrontVioStatusAfter);
    wprintf(L"FrontVdigModeAfter       : 0x%08lX\n", status->FrontVdigModeAfter);
    wprintf(L"FrontVdigOutAfter        : 0x%08lX\n", status->FrontVdigOutAfter);
    wprintf(L"FrontVdigEnAfter         : 0x%08lX\n", status->FrontVdigEnAfter);
    wprintf(L"FrontPrepareReady        : %lu\n", status->FrontPrepareReady);
    wprintf(L"FrontMclk1Cmd            : 0x%08lX\n", status->FrontMclk1Cmd);
    wprintf(L"FrontMclk1Cfg            : 0x%08lX\n", status->FrontMclk1Cfg);
    wprintf(L"FrontMclk1Cbcr           : 0x%08lX\n", status->FrontMclk1Cbcr);
    wprintf(L"FrontResetIo             : 0x%08lX\n", status->FrontResetIo);
    wprintf(L"FrontCciReadStatus       : 0x%08lX\n", status->FrontCciReadStatus);
    wprintf(L"FrontCciReadAttemptCount : %lu\n", status->FrontCciReadAttemptCount);
    wprintf(L"FrontCciRxByte0          : 0x%02lX\n", status->FrontCciRxByte0);
    wprintf(L"FrontCciRxByte1          : 0x%02lX\n", status->FrontCciRxByte1);
    wprintf(L"FrontCciChipId           : 0x%04lX\n", status->FrontCciChipId);
    wprintf(L"FrontCciIdMatchesExpected: %lu\n", status->FrontCciIdMatchesExpected);
    wprintf(L"FrontDeinitStatus        : 0x%08lX\n", status->FrontDeinitStatus);
    wprintf(L"FrontRelease1Status      : 0x%08lX\n", status->FrontRelease1Status);
    wprintf(L"FrontRelease2Status      : 0x%08lX\n", status->FrontRelease2Status);
    wprintf(L"FrontProgramAttempted    : %lu\n", status->FrontProgramAttempted);
    wprintf(L"FrontProgramPhase        : %lu\n", status->FrontProgramPhase);
    wprintf(L"FrontProgramCompleted    : %lu\n", status->FrontProgramCompleted);
    wprintf(L"FrontInitStatus          : 0x%08lX\n", status->FrontInitStatus);
    wprintf(L"FrontInitFailIndex       : %lu\n", status->FrontInitFailIndex);
    wprintf(L"FrontInitFailReg         : 0x%04lX\n", status->FrontInitFailReg);
    wprintf(L"FrontModeStatus          : 0x%08lX\n", status->FrontModeStatus);
    wprintf(L"FrontModeFailIndex       : %lu\n", status->FrontModeFailIndex);
    wprintf(L"FrontModeFailReg         : 0x%04lX\n", status->FrontModeFailReg);
    wprintf(L"FrontGeometryStatus      : 0x%08lX\n", status->FrontGeometryStatus);
    wprintf(L"FrontWidth               : %lu\n", status->FrontWidth);
    wprintf(L"FrontHeight              : %lu\n", status->FrontHeight);
    wprintf(L"FrontStreamWriteStatus   : 0x%08lX\n", status->FrontStreamWriteStatus);
    wprintf(L"FrontStreamReadStatus    : 0x%08lX\n", status->FrontStreamReadStatus);
    wprintf(L"FrontStreamReadback      : %lu\n", status->FrontStreamReadback);
    wprintf(L"FrontStreamStopStatus    : 0x%08lX\n", status->FrontStreamStopStatus);
    wprintf(L"FrontProgramLastTable    : %lu\n", status->FrontProgramLastTable);
    wprintf(L"FrontProgramLastIndex    : %lu\n", status->FrontProgramLastIndex);
    wprintf(L"FrontProgramLastReg      : 0x%04lX\n", status->FrontProgramLastReg);
    wprintf(L"FrontProgramLastStatus   : 0x%08lX\n", status->FrontProgramLastStatus);
    wprintf(L"FrontRtBandwidthStatus   : 0x%08lX\n", status->FrontRtBandwidthStatus);
    wprintf(L"FrontRtBandwidthRelease  : 0x%08lX\n", status->FrontRtBandwidthReleaseStatus);
    wprintf(L"FrontTitanGdscBefore     : 0x%08lX\n", status->FrontTitanGdscBefore);
    wprintf(L"FrontTitanGdscAfter      : 0x%08lX\n", status->FrontTitanGdscAfter);
    wprintf(L"FrontTitanGdscCfgAfter   : 0x%08lX\n", status->FrontTitanGdscCfgAfter);
    wprintf(L"FrontPll0LockOk          : %lu\n", status->FrontPll0LockOk);
    wprintf(L"FrontCphyRxCmd           : 0x%08lX\n", status->FrontCphyRxCmd);
    wprintf(L"FrontCphyRxCfg           : 0x%08lX\n", status->FrontCphyRxCfg);
    wprintf(L"FrontPhyTimerCmd         : 0x%08lX\n", status->FrontPhyTimerCmd);
    wprintf(L"FrontPhyTimerCfg         : 0x%08lX\n", status->FrontPhyTimerCfg);
    wprintf(L"FrontPhyTimerCbcr        : 0x%08lX\n", status->FrontPhyTimerCbcr);
    wprintf(L"FrontPhyClockCbcr        : 0x%08lX\n", status->FrontPhyClockCbcr);
    wprintf(L"FrontClockTreeReady      : %lu\n", status->FrontClockTreeReady);
    wprintf(L"FrontPhyAttempted        : %lu\n", status->FrontPhyAttempted);
    wprintf(L"FrontPhyPhase            : %lu\n", status->FrontPhyPhase);
    wprintf(L"FrontPhyCompleted        : %lu\n", status->FrontPhyCompleted);
    wprintf(L"FrontPhyHwVersion        : 0x%08lX\n", status->FrontPhyHwVersion);
    wprintf(L"FrontPhyLaneMask         : 0x%08lX\n", status->FrontPhyLaneMask);
    wprintf(L"FrontPhyLaneEnable       : 0x%08lX\n", status->FrontPhyLaneEnable);
    wprintf(L"FrontPhySettleCount      : %lu\n", status->FrontPhySettleCount);
    wprintf(L"FrontPhyIrq5Before       : 0x%08lX\n", status->FrontPhyIrq5Before);
    wprintf(L"FrontPhyIrq8Before       : 0x%08lX\n", status->FrontPhyIrq8Before);
    wprintf(L"FrontPhyIrq5After        : 0x%08lX\n", status->FrontPhyIrq5After);
    wprintf(L"FrontPhyIrq8After        : 0x%08lX\n", status->FrontPhyIrq8After);
    wprintf(L"FrontPhyActivityDetected : %lu\n", status->FrontPhyActivityDetected);
    wprintf(L"FrontCsidAttempted       : %lu\n", status->FrontCsidAttempted);
    wprintf(L"FrontCsidPhase           : %lu\n", status->FrontCsidPhase);
    wprintf(L"FrontCsidCompleted       : %lu\n", status->FrontCsidCompleted);
    wprintf(L"FrontCsidHwVersion       : 0x%08lX\n", status->FrontCsidHwVersion);
    wprintf(L"FrontCsidResetTopIrq     : 0x%08lX\n", status->FrontCsidResetTopIrq);
    wprintf(L"FrontCsidResetPolls      : %lu\n", status->FrontCsidResetPolls);
    wprintf(L"FrontCsidRxCfg0          : 0x%08lX\n", status->FrontCsidRxCfg0);
    wprintf(L"FrontCsidRxCfg1          : 0x%08lX\n", status->FrontCsidRxCfg1);
    wprintf(L"FrontCsidRdiCfg0         : 0x%08lX\n", status->FrontCsidRdiCfg0);
    wprintf(L"FrontCsidPacketsBefore   : %lu\n", status->FrontCsidPacketsBefore);
    wprintf(L"FrontCsidPacketsAfter    : %lu\n", status->FrontCsidPacketsAfter);
    wprintf(L"FrontCsidRxIrqAfter      : 0x%08lX\n", status->FrontCsidRxIrqAfter);
    wprintf(L"FrontCsidRdiIrqAfter     : 0x%08lX\n", status->FrontCsidRdiIrqAfter);
    wprintf(L"FrontCsidTopIrqAfter     : 0x%08lX\n", status->FrontCsidTopIrqAfter);
    wprintf(L"FrontCsidEccStats        : 0x%08lX\n", status->FrontCsidEccStats);
    wprintf(L"FrontCsidCrcErrors       : 0x%08lX\n", status->FrontCsidCrcErrors);
    wprintf(L"FrontCsidLongPkt0        : 0x%08lX\n", status->FrontCsidLongPkt0);
    wprintf(L"FrontCsidLongPkt1        : 0x%08lX\n", status->FrontCsidLongPkt1);
    wprintf(L"FrontCsidRdiBytesPing    : %lu\n", status->FrontCsidRdiBytesPing);
    wprintf(L"FrontCsidRdiBytesPong    : %lu\n", status->FrontCsidRdiBytesPong);
    wprintf(L"FrontCsidRdiStatus       : 0x%08lX\n", status->FrontCsidRdiStatus);
    wprintf(L"FrontCsidPolls           : %lu\n", status->FrontCsidPolls);
    wprintf(L"FrontCsidRestoreCompleted: %lu\n", status->FrontCsidRestoreCompleted);
    wprintf(L"FrontFrameSofSeen         : %lu\n", status->FrontFrameSofSeen);
    wprintf(L"FrontFrameEofSeen         : %lu\n", status->FrontFrameEofSeen);
    wprintf(L"FrontFrameComplete        : %lu\n", status->FrontFrameComplete);
    wprintf(L"FrontFramePacketDelta     : %lu\n", status->FrontFramePacketDelta);
    wprintf(L"FrontFrameCaptureCtrl     : 0x%08lX\n", status->FrontFrameCaptureCtrl);
    wprintf(L"FrontFrameVirtualChannel  : %lu\n", status->FrontFrameVirtualChannel);
    wprintf(L"FrontFrameDataType        : 0x%02lX\n", status->FrontFrameDataType);
    wprintf(L"FrontFrameWordCount       : %lu\n", status->FrontFrameWordCount);
    wprintf(L"FrontFrameLongPktMatches  : %lu\n", status->FrontFrameLongPacketMatches);
    wprintf(L"FrontFrameRxErrorMask     : 0x%08lX\n", status->FrontFrameRxErrorMask);
    wprintf(L"FrontFrameRdiErrorMask    : 0x%08lX\n", status->FrontFrameRdiErrorMask);
    wprintf(L"FrontFrameMeasure0        : 0x%08lX\n", status->FrontFrameMeasure0);
    wprintf(L"FrontFrameMeasure1        : 0x%08lX\n", status->FrontFrameMeasure1);
    wprintf(L"FrontFrameMeasure2        : 0x%08lX\n", status->FrontFrameMeasure2);
    wprintf(L"FrontRawAttempted          : %lu\n", status->FrontRawAttempted);
    wprintf(L"FrontRawPhase              : %lu\n", status->FrontRawPhase);
    wprintf(L"FrontRawCompleted          : %lu\n", status->FrontRawCompleted);
    wprintf(L"FrontRawBufferAllocStatus  : 0x%08lX\n", status->FrontRawBufferAllocStatus);
    wprintf(L"FrontRawBufferBytes        : %lu\n", status->FrontRawBufferBytes);
    wprintf(L"FrontRawSmmuEnumerateStatus: 0x%08lX\n", status->FrontRawSmmuEnumerateStatus);
    wprintf(L"FrontRawSmmuOpenStatus     : 0x%08lX\n", status->FrontRawSmmuOpenStatus);
    wprintf(L"FrontRawSmmuQueryStatus    : 0x%08lX\n", status->FrontRawSmmuQueryStatus);
    wprintf(L"FrontRawSmmuGetConfigStatus: 0x%08lX\n", status->FrontRawSmmuGetConfigStatus);
    wprintf(L"FrontRawSmmuContextCount   : %lu\n", status->FrontRawSmmuContextCount);
    wprintf(L"FrontRawVcmCreateStatus    : 0x%08lX\n", status->FrontRawVcmCreateStatus);
    wprintf(L"FrontRawVcmAssociationCount: %lu\n", status->FrontRawVcmAssociationCount);
    wprintf(L"FrontRawVcmSetStateStatus  : 0x%08lX\n", status->FrontRawVcmSetStateStatus);
    wprintf(L"FrontRawVcmActivationCount : %lu\n", status->FrontRawVcmActivationCount);
    wprintf(L"FrontRawVcmReserveStatus   : 0x%08lX\n", status->FrontRawVcmReserveStatus);
    wprintf(L"FrontRawVcmBackStatus      : 0x%08lX\n", status->FrontRawVcmBackStatus);
    wprintf(L"FrontRawIovaLow            : 0x%08lX\n", status->FrontRawIovaLow);
    wprintf(L"FrontRawIovaHigh           : 0x%08lX\n", status->FrontRawIovaHigh);
    wprintf(L"FrontRawPll3LockOk         : %lu\n", status->FrontRawPll3LockOk);
    wprintf(L"FrontRawVfeHwVersion       : 0x%08lX\n", status->FrontRawVfeHwVersion);
    wprintf(L"FrontRawVfeResetStatus     : 0x%08lX\n", status->FrontRawVfeResetStatus);
    wprintf(L"FrontRawVfeResetPolls      : %lu\n", status->FrontRawVfeResetPolls);
    wprintf(L"FrontRawWmCfg              : 0x%08lX\n", status->FrontRawWmCfg);
    wprintf(L"FrontRawWmImageAddress     : 0x%08lX\n", status->FrontRawWmImageAddress);
    wprintf(L"FrontRawWmStatus0          : 0x%08lX\n", status->FrontRawWmStatus0);
    wprintf(L"FrontRawWmStatus1          : 0x%08lX\n", status->FrontRawWmStatus1);
    wprintf(L"FrontRawVfeTopIrq0         : 0x%08lX\n", status->FrontRawVfeTopIrq0);
    wprintf(L"FrontRawVfeTopIrq1         : 0x%08lX\n", status->FrontRawVfeTopIrq1);
    wprintf(L"FrontRawVfeBusIrq0         : 0x%08lX\n", status->FrontRawVfeBusIrq0);
    wprintf(L"FrontRawVfeBusIrq1         : 0x%08lX\n", status->FrontRawVfeBusIrq1);
    wprintf(L"FrontRawVfeBusIrq2         : 0x%08lX\n", status->FrontRawVfeBusIrq2);
    wprintf(L"FrontRawVfeBusDebug0       : 0x%08lX\n", status->FrontRawVfeBusDebug0);
    wprintf(L"FrontRawWaitPolls          : %lu\n", status->FrontRawWaitPolls);
    wprintf(L"FrontRawWaitExitReason     : %lu\n", status->FrontRawWaitExitReason);
    wprintf(L"FrontRawBufferSample0      : 0x%08lX\n", status->FrontRawBufferSample0);
    wprintf(L"FrontRawBufferSample1      : 0x%08lX\n", status->FrontRawBufferSample1);
    wprintf(L"FrontRawBufferSample2      : 0x%08lX\n", status->FrontRawBufferSample2);
    wprintf(L"FrontRawBufferSample3      : 0x%08lX\n", status->FrontRawBufferSample3);
    wprintf(L"FrontRawBufferTail0        : 0x%08lX\n", status->FrontRawBufferTail0);
    wprintf(L"FrontRawBufferTail1        : 0x%08lX\n", status->FrontRawBufferTail1);
    wprintf(L"FrontRawBufferChanged      : %lu\n", status->FrontRawBufferChanged);
    wprintf(L"FrontRawBufferTailChanged  : %lu\n", status->FrontRawBufferTailChanged);
    wprintf(L"FrontRawFileStatus         : 0x%08lX\n", status->FrontRawFileStatus);
    wprintf(L"FrontRawFileBytes          : %lu\n", status->FrontRawFileBytes);
    wprintf(L"FrontRawVcmUnbackStatus    : 0x%08lX\n", status->FrontRawVcmUnbackStatus);
    wprintf(L"FrontRawVcmReleaseStatus   : 0x%08lX\n", status->FrontRawVcmReleaseStatus);
    wprintf(L"FrontRawVcmDeactivateStatus: 0x%08lX\n", status->FrontRawVcmDeactivateStatus);
    wprintf(L"FrontRawVcmSetStateOff     : 0x%08lX\n", status->FrontRawVcmSetStateOffStatus);
    wprintf(L"FrontRawVcmDeassocStatus   : 0x%08lX\n", status->FrontRawVcmDeassocStatus);
    wprintf(L"FrontRawVcmFreeStatus      : 0x%08lX\n", status->FrontRawVcmFreeStatus);
}

static BOOL
QueryStatus(
    _In_ HANDLE Device,
    _Out_ NABU_CAM_MANUAL_STATUS *Status)
{
    DWORD bytesReturned = 0;
    BOOL ok;

    ZeroMemory(Status, sizeof(*Status));
    ok = DeviceIoControl(Device,
                         (DWORD)IOCTL_NABU_CAM_MANUAL_GET_STATUS,
                         NULL,
                         0,
                         Status,
                         sizeof(*Status),
                         &bytesReturned,
                         NULL);
    if (!ok) {
        wprintf(L"Status request failed (Win32=%lu).\n", GetLastError());
        return FALSE;
    }

    if (bytesReturned != sizeof(*Status) ||
        Status->ProtocolVersion != NABU_CAM_MANUAL_PROTOCOL_VERSION) {
        wprintf(L"Unexpected driver response (%lu bytes).\n", bytesReturned);
        return FALSE;
    }

    return TRUE;
}

int __cdecl
wmain(int argc, wchar_t **argv)
{
    HANDLE device;
    NABU_CAM_MANUAL_STATUS status;
    DWORD bytesReturned = 0;
    DWORD probeIoctl = 0;
    const wchar_t *probeName = NULL;
    BOOL probeSucceeded = TRUE;

    if (argc != 2 ||
        (_wcsicmp(argv[1], L"status") != 0 &&
         _wcsicmp(argv[1], L"probe-d0") != 0 &&
         _wcsicmp(argv[1], L"probe-front-id") != 0 &&
         _wcsicmp(argv[1], L"probe-front-program") != 0 &&
         _wcsicmp(argv[1], L"probe-front-phy") != 0 &&
         _wcsicmp(argv[1], L"probe-front-rdi") != 0 &&
         _wcsicmp(argv[1], L"probe-front-frame") != 0 &&
         _wcsicmp(argv[1], L"capture-front-raw") != 0)) {
        wprintf(L"Stage 69 manual front RAW capture.\n");
        wprintf(L"Usage: NabuCamOwnCtl.exe status|capture-front-raw\n");
        return 2;
    }
    if (_wcsicmp(argv[1], L"probe-d0") == 0) {
        probeIoctl = (DWORD)IOCTL_NABU_CAM_MANUAL_PROBE_D0;
        probeName = L"D0";
    } else if (_wcsicmp(argv[1], L"probe-front-id") == 0) {
        probeIoctl = (DWORD)IOCTL_NABU_CAM_MANUAL_PROBE_FRONT_ID;
        probeName = L"Front ID";
    } else if (_wcsicmp(argv[1], L"probe-front-program") == 0) {
        probeIoctl =
            (DWORD)IOCTL_NABU_CAM_MANUAL_PROBE_FRONT_PROGRAM;
        probeName = L"Front program";
    } else if (_wcsicmp(argv[1], L"probe-front-phy") == 0) {
        probeIoctl =
            (DWORD)IOCTL_NABU_CAM_MANUAL_PROBE_FRONT_PHY;
        probeName = L"Front PHY";
    } else if (_wcsicmp(argv[1], L"probe-front-rdi") == 0) {
        probeIoctl =
            (DWORD)IOCTL_NABU_CAM_MANUAL_PROBE_FRONT_RDI;
        probeName = L"Front RDI";
    } else if (_wcsicmp(argv[1], L"probe-front-frame") == 0) {
        probeIoctl =
            (DWORD)IOCTL_NABU_CAM_MANUAL_PROBE_FRONT_FRAME;
        probeName = L"Front frame";
    } else if (_wcsicmp(argv[1], L"capture-front-raw") == 0) {
        probeIoctl =
            (DWORD)IOCTL_NABU_CAM_MANUAL_CAPTURE_FRONT_RAW;
        probeName = L"Front RAW capture";
    }

    device = OpenNabuManualControl();
    if (device == INVALID_HANDLE_VALUE) {
        return 3;
    }

    if (probeIoctl != 0) {
        probeSucceeded = DeviceIoControl(
            device,
            probeIoctl,
            NULL,
            0,
            NULL,
            0,
            &bytesReturned,
            NULL);
        if (!probeSucceeded) {
            wprintf(L"%ls probe failed (Win32=%lu).\n",
                    probeName, GetLastError());
        }
    }

    if (!QueryStatus(device, &status)) {
        CloseHandle(device);
        return 4;
    }
    CloseHandle(device);

    PrintStatus(&status);
    return probeSucceeded ? 0 : 6;
}
