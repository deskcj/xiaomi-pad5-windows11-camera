#pragma once

#include <ntddk.h>
#include <wdf.h>

#define NABU_REAR_WIDTH       2104UL
#define NABU_REAR_HEIGHT      1560UL
#define NABU_REAR_FRAME_BYTES \
    (NABU_REAR_WIDTH * NABU_REAR_HEIGHT * sizeof(USHORT))

typedef VOID
(*NABU_REAR_RAW_CONSUMER)(
    _In_opt_ PVOID ConsumerContext,
    _In_reads_(Width * Height) const USHORT *Raw,
    _In_ ULONG Width,
    _In_ ULONG Height);

typedef VOID
(*NABU_REAR_DIAGNOSTIC_WRITER)(
    _In_opt_ PVOID WriterContext,
    _In_ PCWSTR Name,
    _In_ ULONG Value);

NTSTATUS
NabuRearBackendInitialize(_In_ WDFDEVICE Device);

NTSTATUS
NabuRearBackendAcquire(_In_ WDFDEVICE Device);

VOID
NabuRearBackendRelease(_In_ WDFDEVICE Device);

NTSTATUS
NabuRearBackendCapture(
    _In_ WDFDEVICE Device,
    _In_ NABU_REAR_RAW_CONSUMER Consumer,
    _In_opt_ PVOID ConsumerContext);

VOID
NabuRearBackendWriteDiagnostics(
    _In_ WDFDEVICE Device,
    _In_ NABU_REAR_DIAGNOSTIC_WRITER Writer,
    _In_opt_ PVOID WriterContext);

VOID
NabuRearBackendCleanup(_In_ WDFDEVICE Device);
