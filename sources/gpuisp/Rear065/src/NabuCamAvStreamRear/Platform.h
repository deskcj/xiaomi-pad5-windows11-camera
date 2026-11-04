#pragma once
//
// Platform.h — клиент приватного интерфейса qccamplatform8150.sys.
// СТАДИЯ 1 (проверено на планшете, см. BYPASS-HANDOFF.md). Весь обмен с сенсором
// и питанием/пинами камеры идёт ТОЛЬКО через этот платформенный драйвер — наш
// прямой доступ к CCI не работает (платформенный владеет шиной).
//
#include <ntddk.h>
#include <wdf.h>

// GUID device-interface платформенного драйвера (реверс: DAT_1400134d0).
// {8D73CE35-93CF-4795-8DB9-40BBE130D859}
DEFINE_GUID(GUID_CAM_PLATFORM_PVTINTF,
    0x8D73CE35, 0x93CF, 0x4795, 0x8D, 0xB9, 0x40, 0xBB, 0xE1, 0x30, 0xD8, 0x59);

// Коды команд диспетчера (реверс FUN_140003860).
#define PLAT_IOCTL_GET_PVT_INTF   0x2326A7UL   // internal IOCTL, METHOD_NEITHER
#define PLAT_CMD_I2C_MASTER_INIT  0x232003UL   // a2 -> [+0] = номер мастера
#define PLAT_CMD_I2C_MASTER_DEINIT 0x232007UL
#define PLAT_CMD_I2C_READ         0x23200BUL   // a4 -> структура запроса (см. Sensor.c)
#define PLAT_CMD_I2C_WRITE        0x23200FUL   // a2 -> структура запроса
#define PLAT_CMD_I2C_FIXED_WRITE  0x232013UL   // writes a pre-registered slave register; not a generic speed setter
#define PLAT_CMD_POWER            0x232093UL   // без разбора входа
#define PLAT_CMD_PIN_CONFIG       0x23208FUL   // без разбора входа
#define PLAT_CMD_GET_CPAS_VERSION 0x232097UL   // возвращает CPAS HW version

// Later Ghidra + Android CCI trace: stock request +0x08 is the 8-bit
// slave address (rear sensor 0x20, CN3927 actuator 0x18); +0x3c selects
// CCI master/queue (rear 0). Earlier "registered slave slot" interpretation
// of +0x3c was wrong. The legacy Slot-named helpers below need separate
// review before using them for the actuator.
#define PLAT_I2C_SLOT_REAR        0UL
#define PLAT_CMD_REQ_RT_BW         0x23208BUL
#define PLAT_I2C_SLOT_FRONT       2UL
#define PLAT_I2C_FREQ_FAST_KHZ    400UL
#define PLAT_I2C_FLAG_REG16       0x2UL
#define PLAT_I2C_FLAG_COUNT_VALID 0x800UL
#define PLAT_RVA_DISPATCH         0x3860UL
#define PLAT_RVA_I2C_READ_INTERNAL 0x6720UL
#define PLAT_RVA_I2C_WRITE_INTERNAL 0x6930UL
#define PLAT_RVA_MASTER_LOOKUP    0xBCA8UL

// Смещения в выходной структуре GET_PVT_INTF (реверс).
#define PLAT_PVTINTF_CTX_OFFSET   0x28
#define PLAT_PVTINTF_DISP_OFFSET  0x30
#define PLAT_PVTINTF_OUTBUF_SIZE  0x40

// Сигнатура диспетчера.
typedef NTSTATUS (*PFN_PLAT_DISPATCH)(PVOID Ctx, ULONG Code, PVOID A2, PVOID A3, PVOID A4);

typedef struct _PLATFORM_CLIENT {
    PFILE_OBJECT      FileObject;   // держим ссылку на устройство платформенного
    PDEVICE_OBJECT    DeviceObject;
    PVOID             DriverBase;   // qccamplatform8150.sys base; diagnostic-only internal call
    PVOID             Context;      // ctx из GET_PVT_INTF
    PFN_PLAT_DISPATCH Dispatch;     // диспетчер из GET_PVT_INTF
    BOOLEAN           Ready;
} PLATFORM_CLIENT, *PPLATFORM_CLIENT;

typedef struct _PLATFORM_SLOT_INFO {
    ULONG Master;
    ULONG Remainder;
    ULONG ObjectLo;
    ULONG DescriptorLo;
    ULONG Descriptor[16];
    ULONG ObjectDword[8];
    ULONG ObjectMaster;
    ULONG ObjectFreq;
    ULONG ObjectRefCount;
    ULONG ObjectFlags;
} PLATFORM_SLOT_INFO, *PPLATFORM_SLOT_INFO;

// Открыть платформенный драйвер и получить приватный интерфейс. PASSIVE_LEVEL.
NTSTATUS PlatformOpen(_In_ WDFDEVICE OurDevice, _Out_ PPLATFORM_CLIENT Client);

// Освободить (ObDereference).
VOID PlatformClose(_Inout_ PPLATFORM_CLIENT Client);

// Обёртки над командами диспетчера (все под SEH внутри). Возвращают NTSTATUS команды.
NTSTATUS PlatformInitMaster(_In_ PPLATFORM_CLIENT Client, _In_ ULONG Master);
NTSTATUS PlatformPower(_In_ PPLATFORM_CLIENT Client);
NTSTATUS PlatformPinConfig(_In_ PPLATFORM_CLIENT Client);
NTSTATUS PlatformRequestRtBandwidth(_In_ PPLATFORM_CLIENT Client, _In_ ULONG Index,
                                    _In_ ULONGLONG CamnocBandwidth,
                                    _In_ ULONGLONG ExternalBandwidth);
NTSTATUS PlatformI2cRead(_In_ PPLATFORM_CLIENT Client, _In_ ULONG Slot, _In_ USHORT Reg,
                         _Out_writes_bytes_(Length) PVOID Buffer, _In_ USHORT Length);
NTSTATUS PlatformI2cReadEx(_In_ PPLATFORM_CLIENT Client, _In_ ULONG Slot, _In_ USHORT Reg,
                           _Out_writes_bytes_(Length) PVOID Buffer, _In_ USHORT Length,
                           _In_ ULONG FrequencyKHz, _In_ ULONG Flags, _In_ UCHAR Aux,
                           _In_ USHORT Count);
ULONG PlatformI2cReadRawStatus(_In_ PPLATFORM_CLIENT Client, _In_ ULONG Slot, _In_ USHORT Reg,
                               _Out_writes_bytes_(Length) PVOID Buffer, _In_ USHORT Length,
                               _In_ ULONG FrequencyKHz, _In_ ULONG Flags, _In_ UCHAR Aux,
                               _In_ USHORT Count);
ULONG PlatformI2cReadRawDiag(_In_ PPLATFORM_CLIENT Client, _In_ ULONG Slot, _In_ USHORT Reg,
                             _Out_writes_bytes_(Length) PVOID Buffer, _In_ USHORT Length,
                             _In_ ULONG FrequencyKHz, _In_ ULONG Flags, _In_ UCHAR Aux,
                             _In_ USHORT Count,
                             _Out_opt_ PULONG IrqBefore, _Out_opt_ PULONG IrqAfter,
                             _Out_opt_ PULONG SelectedIrqBefore, _Out_opt_ PULONG SelectedIrqAfter);
ULONG PlatformGetMasterIrqOr(_In_ PPLATFORM_CLIENT Client, _In_ ULONG Master);
ULONG PlatformGetMasterIrqReg(_In_ PPLATFORM_CLIENT Client, _In_ ULONG Master, _In_ ULONG Index);
ULONG PlatformGetSlotInfo(_In_ PPLATFORM_CLIENT Client, _In_ ULONG Slot,
                          _Out_ PPLATFORM_SLOT_INFO Info);
ULONG PlatformI2cWriteRawDiag(_In_ PPLATFORM_CLIENT Client, _In_ ULONG Slot, _In_ USHORT Reg,
                              _In_reads_bytes_(Length) PVOID Buffer, _In_ USHORT Length,
                              _In_ ULONG FrequencyKHz, _In_ ULONG Flags, _In_ UCHAR Aux,
                              _In_ USHORT Count,
                              _Out_opt_ PULONG IrqBefore, _Out_opt_ PULONG IrqAfter,
                              _Out_opt_ PULONG SelectedIrqBefore, _Out_opt_ PULONG SelectedIrqAfter);
NTSTATUS PlatformI2cWrite(_In_ PPLATFORM_CLIENT Client, _In_ ULONG Slot, _In_ USHORT Reg,
                          _In_reads_bytes_(Length) PVOID Buffer, _In_ USHORT Length);

// Универсальный безопасный вызов диспетчера (SEH). status ошибки при исключении.
NTSTATUS PlatformDispatch(_In_ PPLATFORM_CLIENT Client, _In_ ULONG Code,
                          _In_opt_ PVOID A2, _In_opt_ PVOID A3, _In_opt_ PVOID A4);
