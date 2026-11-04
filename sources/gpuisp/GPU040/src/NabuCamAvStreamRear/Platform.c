//
// Platform.c — клиент приватного интерфейса qccamplatform8150.sys.
// СТАДИЯ 1. Проверено на планшете (NabuCamTest билды 20260717-20260720):
// открытие по GUID, GET_PVT_INTF, диспетчер init/power/pincfg возвращают 0.
//
#include <ntddk.h>
#include <initguid.h>   // ДОЛЖЕН быть до Platform.h, чтобы DEFINE_GUID создал хранилище GUID
#include "Platform.h"

static
NTSTATUS
PlatformIrpCompletion(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp, _In_ PVOID Context)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);
    KeSetEvent((PKEVENT)Context, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

NTSTATUS
PlatformOpen(_In_ WDFDEVICE OurDevice, _Out_ PPLATFORM_CLIENT Client)
{
    NTSTATUS status;
    PWSTR symList = NULL;
    UNICODE_STRING symName;
    PIRP irp;
    KEVENT event;
    PIO_STACK_LOCATION stack;
    PVOID inBuf[1];
    UCHAR outBuf[PLAT_PVTINTF_OUTBUF_SIZE];

    RtlZeroMemory(Client, sizeof(*Client));

    status = IoGetDeviceInterfaces((LPGUID)&GUID_CAM_PLATFORM_PVTINTF, NULL, 0, &symList);
    if (!NT_SUCCESS(status) || symList == NULL || symList[0] == L'\0') {
        if (symList != NULL) { ExFreePool(symList); }
        return NT_SUCCESS(status) ? STATUS_NO_SUCH_DEVICE : status;
    }

    RtlInitUnicodeString(&symName, symList);
    status = IoGetDeviceObjectPointer(&symName, FILE_ALL_ACCESS,
                                      &Client->FileObject, &Client->DeviceObject);
    ExFreePool(symList);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    inBuf[0] = (PVOID)WdfDeviceWdmGetDeviceObject(OurDevice);
    RtlZeroMemory(outBuf, sizeof(outBuf));
    KeInitializeEvent(&event, NotificationEvent, FALSE);

    irp = IoAllocateIrp(Client->DeviceObject->StackSize, FALSE);
    if (irp == NULL) {
        ObDereferenceObject(Client->FileObject);
        RtlZeroMemory(Client, sizeof(*Client));
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    irp->UserBuffer = outBuf;                     // METHOD_NEITHER out
    stack = IoGetNextIrpStackLocation(irp);
    stack->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
    stack->Parameters.DeviceIoControl.IoControlCode = PLAT_IOCTL_GET_PVT_INTF;
    stack->Parameters.DeviceIoControl.OutputBufferLength = sizeof(outBuf);
    stack->Parameters.DeviceIoControl.InputBufferLength = sizeof(inBuf);
    stack->Parameters.DeviceIoControl.Type3InputBuffer = inBuf;
    IoSetCompletionRoutine(irp, PlatformIrpCompletion, &event, TRUE, TRUE, TRUE);

    status = IoCallDriver(Client->DeviceObject, irp);
    if (status == STATUS_PENDING) {
        KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, NULL);
        status = irp->IoStatus.Status;
    }
    IoFreeIrp(irp);

    if (!NT_SUCCESS(status)) {
        ObDereferenceObject(Client->FileObject);
        RtlZeroMemory(Client, sizeof(*Client));
        return status;
    }

    Client->Context  = *(PVOID *)(outBuf + PLAT_PVTINTF_CTX_OFFSET);
    Client->Dispatch = *(PFN_PLAT_DISPATCH *)(outBuf + PLAT_PVTINTF_DISP_OFFSET);
    if (Client->Dispatch == NULL) {
        ObDereferenceObject(Client->FileObject);
        RtlZeroMemory(Client, sizeof(*Client));
        return STATUS_INVALID_ADDRESS;
    }
    Client->DriverBase = (PVOID)((PUCHAR)Client->Dispatch - PLAT_RVA_DISPATCH);
    Client->Ready = TRUE;
    return STATUS_SUCCESS;
}

VOID
PlatformClose(_Inout_ PPLATFORM_CLIENT Client)
{
    if (Client->FileObject != NULL) {
        ObDereferenceObject(Client->FileObject);
    }
    RtlZeroMemory(Client, sizeof(*Client));
}

NTSTATUS
PlatformDispatch(_In_ PPLATFORM_CLIENT Client, _In_ ULONG Code,
                 _In_opt_ PVOID A2, _In_opt_ PVOID A3, _In_opt_ PVOID A4)
{
    NTSTATUS status = STATUS_DEVICE_NOT_READY;
    if (!Client->Ready || Client->Dispatch == NULL) {
        return status;
    }
    __try {
        status = Client->Dispatch(Client->Context, Code, A2, A3, A4);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        status = STATUS_UNHANDLED_EXCEPTION;
    }
    return status;
}

NTSTATUS
PlatformInitMaster(_In_ PPLATFORM_CLIENT Client, _In_ ULONG Master)
{
    ULONG arg[8];
    RtlZeroMemory(arg, sizeof(arg));
    arg[0] = Master;                              // [+0] = номер мастера
    return PlatformDispatch(Client, PLAT_CMD_I2C_MASTER_INIT, arg, NULL, NULL);
}

NTSTATUS
PlatformPower(_In_ PPLATFORM_CLIENT Client)
{
    ULONG arg[8];
    RtlZeroMemory(arg, sizeof(arg));
    return PlatformDispatch(Client, PLAT_CMD_POWER, arg, NULL, NULL);
}

NTSTATUS
PlatformPinConfig(_In_ PPLATFORM_CLIENT Client)
{
    ULONG arg[8];
    RtlZeroMemory(arg, sizeof(arg));
    return PlatformDispatch(Client, PLAT_CMD_PIN_CONFIG, arg, NULL, NULL);
}

NTSTATUS
PlatformRequestRtBandwidth(_In_ PPLATFORM_CLIENT Client, _In_ ULONG Index,
                           _In_ ULONGLONG CamnocBandwidth,
                           _In_ ULONGLONG ExternalBandwidth)
{
    typedef struct _PLATFORM_RT_BW_REQUEST {
        ULONG Index;
        ULONG Reserved;
        ULONGLONG CamnocBandwidth;
        ULONGLONG ExternalBandwidth;
    } PLATFORM_RT_BW_REQUEST;
    PLATFORM_RT_BW_REQUEST request;

    RtlZeroMemory(&request, sizeof(request));
    request.Index = Index;
    request.CamnocBandwidth = CamnocBandwidth;
    request.ExternalBandwidth = ExternalBandwidth;
    return PlatformDispatch(Client, PLAT_CMD_REQ_RT_BW, &request,
                            (PVOID)(ULONG_PTR)sizeof(request), NULL);
}

static
VOID
PlatformBuildI2cReq(
    _Out_writes_bytes_(0x40) UCHAR Req[0x40],
    _In_ ULONG Slot,
    _In_ USHORT Reg,
    _In_ PVOID Buffer,
    _In_ USHORT Length,
    _In_ ULONG FrequencyKHz,
    _In_ ULONG Flags,
    _In_ UCHAR Aux,
    _In_ USHORT Count)
{
    RtlZeroMemory(Req, 0x40);
    RtlCopyMemory(Req + 0x04, &FrequencyKHz, sizeof(FrequencyKHz)); // CCI freq: 100/400/1000
    RtlCopyMemory(Req + 0x08, &Aux, sizeof(Aux));         // copied by qccamplatform to xfer+0x61
    RtlCopyMemory(Req + 0x0c, &Flags, sizeof(Flags));     // bit1 => 16-bit reg addr, bit11 => count valid
    RtlCopyMemory(Req + 0x18, &Buffer, sizeof(Buffer));   // data/result buffer
    RtlCopyMemory(Req + 0x20, &Buffer, sizeof(Buffer));   // secondary data pointer
    RtlCopyMemory(Req + 0x28, &Reg, sizeof(Reg));         // register address
    RtlCopyMemory(Req + 0x2a, &Length, sizeof(Length));   // data length
    RtlCopyMemory(Req + 0x2c, &Count, sizeof(Count));     // used when bit11 is set
    RtlCopyMemory(Req + 0x3c, &Slot, sizeof(Slot));       // registered slave slot
}

NTSTATUS
PlatformI2cRead(_In_ PPLATFORM_CLIENT Client, _In_ ULONG Slot, _In_ USHORT Reg,
                _Out_writes_bytes_(Length) PVOID Buffer, _In_ USHORT Length)
{
    return PlatformI2cReadEx(Client, Slot, Reg, Buffer, Length,
                             PLAT_I2C_FREQ_FAST_KHZ, PLAT_I2C_FLAG_REG16, 0, 1);
}

NTSTATUS
PlatformI2cReadEx(_In_ PPLATFORM_CLIENT Client, _In_ ULONG Slot, _In_ USHORT Reg,
                  _Out_writes_bytes_(Length) PVOID Buffer, _In_ USHORT Length,
                  _In_ ULONG FrequencyKHz, _In_ ULONG Flags, _In_ UCHAR Aux,
                  _In_ USHORT Count)
{
    UCHAR req[0x40];
    if (Buffer == NULL || Length == 0) {
        return STATUS_INVALID_PARAMETER;
    }
    PlatformBuildI2cReq(req, Slot, Reg, Buffer, Length, FrequencyKHz, Flags, Aux, Count);
    return PlatformDispatch(Client, PLAT_CMD_I2C_READ, NULL, NULL, req);
}

ULONG
PlatformGetMasterIrqReg(_In_ PPLATFORM_CLIENT Client, _In_ ULONG Master, _In_ ULONG Index)
{
    typedef PVOID (*PFN_MASTER_LOOKUP)(_In_ ULONG Master);
    PFN_MASTER_LOOKUP lookup;
    PVOID masterCtx;
    ULONG value = 0;

    if (Client == NULL || Client->DriverBase == NULL || Index >= 7) {
        return 0;
    }

    lookup = (PFN_MASTER_LOOKUP)((PUCHAR)Client->DriverBase + PLAT_RVA_MASTER_LOOKUP);
    __try {
        masterCtx = lookup(Master);
        if (masterCtx != NULL) {
            value = *(volatile ULONG *)((PUCHAR)masterCtx + ((0x95 + Index) * sizeof(ULONG)));
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        value = 0xFFFFFFFFUL;
    }
    return value;
}

ULONG
PlatformGetMasterIrqOr(_In_ PPLATFORM_CLIENT Client, _In_ ULONG Master)
{
    ULONG irqOr = 0;
    ULONG i;
    for (i = 0; i < 7; ++i) {
        irqOr |= PlatformGetMasterIrqReg(Client, Master, i);
    }
    return irqOr;
}

ULONG
PlatformGetSlotInfo(_In_ PPLATFORM_CLIENT Client, _In_ ULONG Slot,
                    _Out_ PPLATFORM_SLOT_INFO Info)
{
    typedef PVOID (*PFN_MASTER_LOOKUP)(_In_ ULONG Master);
    PFN_MASTER_LOOKUP lookup;
    PVOID masterCtx;
    PVOID object;
    PULONG descriptor;
    ULONG i;
    ULONG master = Slot / 2;
    ULONG rem = Slot % 2;
    ULONG status = 0;

    if (Info == NULL) {
        return 0x20UL;
    }
    RtlZeroMemory(Info, sizeof(*Info));
    Info->Master = master;
    Info->Remainder = rem;

    if (Client == NULL || Client->DriverBase == NULL) {
        return 0x40UL;
    }

    lookup = (PFN_MASTER_LOOKUP)((PUCHAR)Client->DriverBase + PLAT_RVA_MASTER_LOOKUP);
    __try {
        masterCtx = lookup(master);
        if (masterCtx == NULL) {
            status = 0x40UL;
        } else {
            object = *(PVOID *)((PUCHAR)masterCtx + (((rem * 2) + 0x18) * sizeof(PVOID)));
            if (object == NULL) {
                status = 0x40UL;
            } else {
                descriptor = *(PULONG *)((PUCHAR)object + 0x08);
                Info->ObjectLo = (ULONG)(ULONG_PTR)object;
                Info->DescriptorLo = (ULONG)(ULONG_PTR)descriptor;
                Info->ObjectMaster = *(volatile ULONG *)((PUCHAR)object + 0x00);
                Info->ObjectFlags = *(volatile ULONG *)((PUCHAR)object + 0x18);
                Info->ObjectFreq = *(volatile ULONG *)((PUCHAR)object + 0x14);
                Info->ObjectRefCount = *(volatile UCHAR *)((PUCHAR)object + 0x1c);
                for (i = 0; i < 8; ++i) {
                    Info->ObjectDword[i] = *(volatile ULONG *)((PUCHAR)object + i * sizeof(ULONG));
                }
                if (descriptor != NULL) {
                    for (i = 0; i < 16; ++i) {
                        Info->Descriptor[i] = *(volatile ULONG *)(descriptor + i);
                    }
                }
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        status = 0xFFFFFFFEUL;
    }
    return status;
}

ULONG
PlatformI2cReadRawDiag(_In_ PPLATFORM_CLIENT Client, _In_ ULONG Slot, _In_ USHORT Reg,
                       _Out_writes_bytes_(Length) PVOID Buffer, _In_ USHORT Length,
                       _In_ ULONG FrequencyKHz, _In_ ULONG Flags, _In_ UCHAR Aux,
                       _In_ USHORT Count,
                       _Out_opt_ PULONG IrqBefore, _Out_opt_ PULONG IrqAfter,
                       _Out_opt_ PULONG SelectedIrqBefore, _Out_opt_ PULONG SelectedIrqAfter)
{
    typedef ULONG (*PFN_I2C_READ_INTERNAL)(_In_ PVOID Req);
    UCHAR req[0x40];
    PFN_I2C_READ_INTERNAL readInternal;
    ULONG status = 0xFFFFFFFFUL;
    ULONG master = Slot / 2;
    PLATFORM_SLOT_INFO slotInfo;
    ULONG selectedIndex = 0xFFFFFFFFUL;

    if (Client == NULL || Client->DriverBase == NULL || Buffer == NULL || Length == 0) {
        if (IrqBefore != NULL) { *IrqBefore = 0; }
        if (IrqAfter != NULL) { *IrqAfter = 0; }
        if (SelectedIrqBefore != NULL) { *SelectedIrqBefore = 0; }
        if (SelectedIrqAfter != NULL) { *SelectedIrqAfter = 0; }
        return status;
    }

    if (PlatformGetSlotInfo(Client, Slot, &slotInfo) == 0 && slotInfo.Descriptor[0] < 7) {
        selectedIndex = slotInfo.Descriptor[0];
    }
    PlatformBuildI2cReq(req, Slot, Reg, Buffer, Length, FrequencyKHz, Flags, Aux, Count);
    readInternal = (PFN_I2C_READ_INTERNAL)((PUCHAR)Client->DriverBase + PLAT_RVA_I2C_READ_INTERNAL);
    if (IrqBefore != NULL) {
        *IrqBefore = PlatformGetMasterIrqOr(Client, master);
    }
    if (SelectedIrqBefore != NULL) {
        *SelectedIrqBefore = (selectedIndex < 7) ? PlatformGetMasterIrqReg(Client, master, selectedIndex) : 0;
    }
    __try {
        status = readInternal(req);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        status = 0xFFFFFFFEUL;
    }
    if (IrqAfter != NULL) {
        *IrqAfter = PlatformGetMasterIrqOr(Client, master);
    }
    if (SelectedIrqAfter != NULL) {
        *SelectedIrqAfter = (selectedIndex < 7) ? PlatformGetMasterIrqReg(Client, master, selectedIndex) : 0;
    }
    return status;
}

ULONG
PlatformI2cReadRawStatus(_In_ PPLATFORM_CLIENT Client, _In_ ULONG Slot, _In_ USHORT Reg,
                         _Out_writes_bytes_(Length) PVOID Buffer, _In_ USHORT Length,
                         _In_ ULONG FrequencyKHz, _In_ ULONG Flags, _In_ UCHAR Aux,
                         _In_ USHORT Count)
{
    return PlatformI2cReadRawDiag(Client, Slot, Reg, Buffer, Length,
                                  FrequencyKHz, Flags, Aux, Count, NULL, NULL, NULL, NULL);
}

ULONG
PlatformI2cWriteRawDiag(_In_ PPLATFORM_CLIENT Client, _In_ ULONG Slot, _In_ USHORT Reg,
                        _In_reads_bytes_(Length) PVOID Buffer, _In_ USHORT Length,
                        _In_ ULONG FrequencyKHz, _In_ ULONG Flags, _In_ UCHAR Aux,
                        _In_ USHORT Count,
                        _Out_opt_ PULONG IrqBefore, _Out_opt_ PULONG IrqAfter,
                        _Out_opt_ PULONG SelectedIrqBefore, _Out_opt_ PULONG SelectedIrqAfter)
{
    typedef ULONG (*PFN_I2C_WRITE_INTERNAL)(_In_ PVOID Req);
    UCHAR req[0x40];
    PFN_I2C_WRITE_INTERNAL writeInternal;
    ULONG status = 0xFFFFFFFFUL;
    ULONG master = Slot / 2;
    PLATFORM_SLOT_INFO slotInfo;
    ULONG selectedIndex = 0xFFFFFFFFUL;

    if (Client == NULL || Client->DriverBase == NULL || Buffer == NULL || Length == 0) {
        if (IrqBefore != NULL) { *IrqBefore = 0; }
        if (IrqAfter != NULL) { *IrqAfter = 0; }
        if (SelectedIrqBefore != NULL) { *SelectedIrqBefore = 0; }
        if (SelectedIrqAfter != NULL) { *SelectedIrqAfter = 0; }
        return status;
    }

    if (PlatformGetSlotInfo(Client, Slot, &slotInfo) == 0 && slotInfo.Descriptor[0] < 7) {
        selectedIndex = slotInfo.Descriptor[0];
    }
    PlatformBuildI2cReq(req, Slot, Reg, Buffer, Length, FrequencyKHz, Flags, Aux, Count);
    writeInternal = (PFN_I2C_WRITE_INTERNAL)((PUCHAR)Client->DriverBase + PLAT_RVA_I2C_WRITE_INTERNAL);
    if (IrqBefore != NULL) {
        *IrqBefore = PlatformGetMasterIrqOr(Client, master);
    }
    if (SelectedIrqBefore != NULL) {
        *SelectedIrqBefore = (selectedIndex < 7) ? PlatformGetMasterIrqReg(Client, master, selectedIndex) : 0;
    }
    __try {
        status = writeInternal(req);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        status = 0xFFFFFFFEUL;
    }
    if (IrqAfter != NULL) {
        *IrqAfter = PlatformGetMasterIrqOr(Client, master);
    }
    if (SelectedIrqAfter != NULL) {
        *SelectedIrqAfter = (selectedIndex < 7) ? PlatformGetMasterIrqReg(Client, master, selectedIndex) : 0;
    }
    return status;
}

NTSTATUS
PlatformI2cWrite(_In_ PPLATFORM_CLIENT Client, _In_ ULONG Slot, _In_ USHORT Reg,
                 _In_reads_bytes_(Length) PVOID Buffer, _In_ USHORT Length)
{
    UCHAR req[0x40];
    if (Buffer == NULL || Length == 0) {
        return STATUS_INVALID_PARAMETER;
    }
    PlatformBuildI2cReq(req, Slot, Reg, Buffer, Length,
                        PLAT_I2C_FREQ_FAST_KHZ, PLAT_I2C_FLAG_REG16, 0, 1);
    return PlatformDispatch(Client, PLAT_CMD_I2C_WRITE, req, NULL, NULL);
}
