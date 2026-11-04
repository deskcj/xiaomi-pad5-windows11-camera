#pragma once

#include <ntddk.h>
#include <wdf.h>

#define NABU_FRONT_WIDTH        1632UL
#define NABU_FRONT_HEIGHT       1224UL
#define NABU_FRONT_FRAME_BYTES  (NABU_FRONT_WIDTH * NABU_FRONT_HEIGHT * 2UL)

typedef VOID
(*NABU_FRONT_RAW_CONSUMER)(
    _In_opt_ PVOID ConsumerContext,
    _In_reads_(Width * Height) const USHORT *Raw,
    _In_ ULONG Width,
    _In_ ULONG Height);

typedef VOID
(*NABU_FRONT_DIAGNOSTIC_WRITER)(
    _In_opt_ PVOID WriterContext,
    _In_ PCWSTR Name,
    _In_ ULONG Value);

NTSTATUS
NabuFrontBackendInitialize(_In_ WDFDEVICE Device);

NTSTATUS
NabuFrontBackendAcquire(_In_ WDFDEVICE Device);

NTSTATUS
NabuFrontBackendPublishPhysicalProperties(
    _In_ WDFDEVICE Device,
    _In_ PDEVICE_OBJECT VirtualPdo);

VOID
NabuFrontBackendRelease(_In_ WDFDEVICE Device);

NTSTATUS
NabuFrontBackendCapture(
    _In_ WDFDEVICE Device,
    _In_ NABU_FRONT_RAW_CONSUMER Consumer,
    _In_opt_ PVOID ConsumerContext);

VOID
NabuFrontBackendWriteDiagnostics(
    _In_ WDFDEVICE Device,
    _In_ NABU_FRONT_DIAGNOSTIC_WRITER Writer,
    _In_opt_ PVOID WriterContext);

VOID
NabuFrontBackendCleanup(_In_ WDFDEVICE Device);
