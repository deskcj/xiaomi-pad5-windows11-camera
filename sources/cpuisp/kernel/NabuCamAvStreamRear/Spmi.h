#pragma once

#include <ntddk.h>
#include "Registers.h"

//
// SPMI PMIC Arbiter (версия v5) — клиент для доступа к регуляторам питания
// сенсора (cam_vio/cam_vana/cam_vdig), которые управляются через PMIC GPIO
// (pm8150l), а не через TLMM. См. подробный разбор и источники в
// Registers.h и research/sensor-gpio-regulator-findings.md.
//
// (копия ../Spmi.h — держать в синхроне вручную, это отдельный
// самодостаточный тестовый проект)
//
// ОБНОВЛЕНО 2026-07-02 (первый реальный тест ЧТЕНИЯ прошёл успешно на
// живом железе — см. research/sensor-gpio-regulator-findings.md): нашли
// APID=316 для GPIO3, WriteEe=0 СОВПАДАЕТ с нашим EE=0 — значит у нас есть
// право записи. MODE_CTL=1 показал direction=INPUT (не output) — пин сейчас
// не управляет ничем активно. Пользователь явно решил попробовать запись —
// добавлена SpmiArbWritePeripheral (RW-канал, EXT_WRITEL), используется в
// TestDriver.c для перевода GPIO3 в OUTPUT+HIGH (гипотеза: активный уровень
// enable-линии cam_vdig — HIGH, стандартная конвенция для таких линий).
//

typedef struct _SPMI_ARB_CONTEXT {
    PUCHAR CoreBase;      // MmMapIoSpaceEx(SPMI_ARB_CORE_PHYS_ADDR, SPMI_ARB_CORE_REGION_SIZE)
    PUCHAR CnfgBase;      // MmMapIoSpaceEx(SPMI_ARB_CNFG_PHYS_ADDR, SPMI_ARB_CNFG_REGION_SIZE)
    PUCHAR ObsrvrBase;    // MmMapIoSpaceEx(SPMI_ARB_OBSRVR_PHYS_ADDR, SPMI_ARB_OBSRVR_REGION_SIZE)
    PUCHAR ChnlsBase;     // MmMapIoSpaceEx(SPMI_ARB_CHNLS_PHYS_ADDR, SPMI_ARB_CHNLS_REGION_SIZE) — только для записи

    ULONG HwVersion;      // сырое значение регистра VERSION (core+0x0)
    ULONG ApidCount;      // FEATURES & PERIPH_MASK, ограничено размером CoreBase (см. Spmi.c)
} SPMI_ARB_CONTEXT, *PSPMI_ARB_CONTEXT;

//
// Читает VERSION/FEATURES. Требует, чтобы CoreBase/CnfgBase/ObsrvrBase уже
// были замаплены вызывающим кодом (мы не мапим память сами — TestDriver.c
// решает, когда и что мапить/размапливать, как и для CCI/TLMM/CAMCC).
//
NTSTATUS
SpmiArbInitialize(
    _Inout_ PSPMI_ARB_CONTEXT Ctx
    );

//
// Ищет APID для периферии (Sid,Pid) — сканирует таблицу core+0x900+4*apid
// (формула v5), останавливаясь либо на ApidCount, либо когда офсет выходит
// за пределы замапленного CoreBase (в точности как это делает сам mainline
// Linux-драйвер — см. комментарий в Registers.h). WriteEe — значение поля
// владельца записи (не обязательно равно нашему EE — если не равно, запись
// через RW-канал будет отвергнута аппаратно, EPERM-подобная ситуация).
//
NTSTATUS
SpmiArbFindApid(
    _In_ PSPMI_ARB_CONTEXT Ctx,
    _In_ UCHAR Sid,
    _In_ UCHAR Pid,
    _Out_ PULONG Apid,
    _Out_ PULONG WriteEe
    );

//
// Читает 1..4 байта регистра периферии через OBS-канал (SPMI-опкод
// EXT_READL) — НЕ требует владения APID, безопасно для диагностики.
// Ee — наш собственный execution environment (подтверждён 0 на этом
// планшете живым device tree, qcom,ee).
//
NTSTATUS
SpmiArbReadPeripheral(
    _In_ PSPMI_ARB_CONTEXT Ctx,
    _In_ UCHAR Sid,
    _In_ UCHAR Pid,
    _In_ UCHAR RegOffset,
    _In_ ULONG Ee,
    _In_ ULONG Apid,
    _Out_ PULONG Value,
    _In_ ULONG ByteCount
    );

//
// Пишет 1..4 байта регистра периферии через RW-канал (SPMI-опкод
// EXT_WRITEL) — ТРЕБУЕТ владения APID (аппаратура сама это проверяет через
// формулу офсета pmic_arb_offset_v5 для CHANNEL_RW; если владелец другой EE,
// получим STATUS_DEVICE_PROTOCOL_ERROR/DENIED). Подтверждено живым тестом
// (2026-07-02): для нашей целевой периферии (GPIO3, pm8150l) WriteEe==0==
// наш EE, запись должна быть разрешена.
//
NTSTATUS
SpmiArbWritePeripheral(
    _In_ PSPMI_ARB_CONTEXT Ctx,
    _In_ UCHAR Sid,
    _In_ UCHAR Pid,
    _In_ UCHAR RegOffset,
    _In_ ULONG Apid,
    _In_ ULONG Value,
    _In_ ULONG ByteCount
    );
