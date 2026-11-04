#include "Spmi.h"

#define SPMI_ARB_POLL_INTERVAL_US   5
#define SPMI_ARB_POLL_TIMEOUT_US    2000

NTSTATUS
SpmiArbInitialize(
    _Inout_ PSPMI_ARB_CONTEXT Ctx
    )
{
    ULONG features;

    if (Ctx->CoreBase == NULL) {
        return STATUS_INVALID_DEVICE_STATE;
    }

    Ctx->HwVersion = *(volatile ULONG *)(Ctx->CoreBase + SPMI_ARB_VERSION_OFFSET);
    features = *(volatile ULONG *)(Ctx->CoreBase + SPMI_ARB_FEATURES_OFFSET);
    Ctx->ApidCount = features & SPMI_ARB_FEATURES_PERIPH_MASK;

    return STATUS_SUCCESS;
}

NTSTATUS
SpmiArbFindApid(
    _In_ PSPMI_ARB_CONTEXT Ctx,
    _In_ UCHAR Sid,
    _In_ UCHAR Pid,
    _Out_ PULONG Apid,
    _Out_ PULONG WriteEe
    )
{
    ULONG targetPpid;
    ULONG apid;
    ULONG apidCount;

    *Apid = 0;
    *WriteEe = 0;

    if (Ctx->CoreBase == NULL || Ctx->CnfgBase == NULL) {
        return STATUS_INVALID_DEVICE_STATE;
    }

    targetPpid = (((ULONG)Sid) << 8) | (ULONG)Pid;

    // Ограничиваем сканирование и значением ApidCount (из FEATURES), и
    // тем, что офсет 0x900+4*apid реально влезает в замапленный CoreBase —
    // ТОЧНО так же, как это делает сам mainline-драйвер
    // (`if (offset >= pmic_arb->core_size) break;`), не догадка с нашей
    // стороны, а буквальное повторение той же безопасной границы.
    apidCount = Ctx->ApidCount;
    for (apid = 0; apid < apidCount; apid++) {
        ULONG offset = SPMI_ARB_APID_MAP_OFFSET_V5(apid);
        ULONG regval;
        ULONG ppid;

        if (offset + sizeof(ULONG) > SPMI_ARB_CORE_REGION_SIZE) {
            break;
        }

        regval = *(volatile ULONG *)(Ctx->CoreBase + offset);
        if (regval == 0) {
            continue;
        }

        ppid = (regval >> SPMI_ARB_PPID_SHIFT) & SPMI_ARB_PPID_MASK;
        if (ppid == targetPpid) {
            ULONG ownerOffset = SPMI_ARB_APID_OWNER_OFFSET(apid);
            ULONG ownerRegval = 0;

            *Apid = apid;

            if (ownerOffset + sizeof(ULONG) <= SPMI_ARB_CNFG_REGION_SIZE) {
                ownerRegval = *(volatile ULONG *)(Ctx->CnfgBase + ownerOffset);
            }
            *WriteEe = ownerRegval & SPMI_ARB_OWNER_EE_MASK;

            return STATUS_SUCCESS;
        }
    }

    return STATUS_NOT_FOUND;
}

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
    )
{
    ULONG offset;
    ULONG addr16;
    ULONG bc;
    ULONG cmd;
    ULONG elapsedUs = 0;
    ULONG status;

    // Sid не используется напрямую: формула офсета OBS-канала (v5) зависит
    // только от Apid/Ee, а Pid уже полностью определяет 16-битный SPMI-адрес
    // периферии (addr16 ниже) — Sid участвовал только на этапе поиска APID
    // (SpmiArbFindApid). Параметр оставлен в сигнатуре для симметрии с
    // SpmiArbFindApid и для будущей функции записи (там та же сигнатура
    // понадобится для логики владения).
    UNREFERENCED_PARAMETER(Sid);

    *Value = 0;

    if (Ctx->ObsrvrBase == NULL) {
        return STATUS_INVALID_DEVICE_STATE;
    }

    if (ByteCount < 1 || ByteCount > 4) {
        return STATUS_INVALID_PARAMETER;
    }

    offset = SPMI_ARB_OBS_OFFSET(Ee, Apid);

    if (offset + SPMI_ARB_CHNL_RDATA1_OFFSET + sizeof(ULONG) > SPMI_ARB_OBSRVR_REGION_SIZE) {
        return STATUS_INVALID_DEVICE_STATE;
    }

    addr16 = (((ULONG)Pid) << 8) | (ULONG)RegOffset;
    bc = ByteCount - 1;

    // Запускаем EXT_READL-транзакцию — это командный протокол, не прямое
    // чтение MMIO (тот же принцип, что и у CCI, см. Cci.c): пишем CMD,
    // ждём бит DONE в STATUS, потом читаем RDATA0/RDATA1.
    cmd = SPMI_ARB_FMT_CMD(SPMI_ARB_OP_EXT_READL, addr16, bc);
    *(volatile ULONG *)(Ctx->ObsrvrBase + offset + SPMI_ARB_CHNL_CMD_OFFSET) = cmd;

    for (;;) {
        status = *(volatile ULONG *)(Ctx->ObsrvrBase + offset + SPMI_ARB_CHNL_STATUS_OFFSET);

        if (status & SPMI_ARB_STATUS_DONE_BIT) {
            break;
        }

        if (elapsedUs >= SPMI_ARB_POLL_TIMEOUT_US) {
            return STATUS_IO_TIMEOUT;
        }

        KeStallExecutionProcessor(SPMI_ARB_POLL_INTERVAL_US);
        elapsedUs += SPMI_ARB_POLL_INTERVAL_US;
    }

    if (status & (SPMI_ARB_STATUS_FAILURE_BIT | SPMI_ARB_STATUS_DENIED_BIT | SPMI_ARB_STATUS_DROPPED_BIT)) {
        return STATUS_DEVICE_PROTOCOL_ERROR;
    }

    {
        ULONG rdata0 = *(volatile ULONG *)(Ctx->ObsrvrBase + offset + SPMI_ARB_CHNL_RDATA0_OFFSET);
        *Value = rdata0 & (ByteCount >= 4 ? 0xffffffffUL : ((1UL << (ByteCount * 8)) - 1UL));
    }

    return STATUS_SUCCESS;
}

//
// ДОБАВЛЕНО 2026-07-02 (после успешного живого теста SpmiArbReadPeripheral,
// см. Spmi.h и research/sensor-gpio-regulator-findings.md): запись через
// RW-канал (chnls/wr_base). Формула офсета — ТОЛЬКО 0x10000*apid (без EE —
// см. pmic_arb_offset_v5 в mainline, случай PMIC_ARB_CHANNEL_RW), в отличие
// от OBS, где было 0x10000*ee+0x80*apid. Аппаратура сама проверит владение
// (write_ee) при выполнении транзакции — мы не эмулируем эту проверку в
// софте, просто честно возвращаем ошибку, если STATUS покажет DENIED.
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
    )
{
    ULONG offset;
    ULONG addr16;
    ULONG bc;
    ULONG cmd;
    ULONG elapsedUs = 0;
    ULONG status;

    // Sid не используется — 16-битный SPMI-адрес периферии полностью
    // определяется Pid+RegOffset (addr16 ниже), Sid участвовал только на
    // этапе поиска APID (SpmiArbFindApid). Оставлен в сигнатуре для
    // симметрии с остальными функциями этого файла.
    UNREFERENCED_PARAMETER(Sid);

    if (Ctx->ChnlsBase == NULL) {
        return STATUS_INVALID_DEVICE_STATE;
    }

    if (ByteCount < 1 || ByteCount > 4) {
        return STATUS_INVALID_PARAMETER;
    }

    offset = SPMI_ARB_RW_OFFSET(Apid);

    if (offset + SPMI_ARB_CHNL_WDATA1_OFFSET + sizeof(ULONG) > SPMI_ARB_CHNLS_REGION_SIZE) {
        return STATUS_INVALID_DEVICE_STATE;
    }

    addr16 = (((ULONG)Pid) << 8) | (ULONG)RegOffset;
    bc = ByteCount - 1;

    // Порядок ВАЖЕН и буквально повторяет mainline
    // (pmic_arb_write_cmd_unlocked): сначала данные в WDATA0, ПОТОМ запись
    // в CMD запускает транзакцию.
    *(volatile ULONG *)(Ctx->ChnlsBase + offset + SPMI_ARB_CHNL_WDATA0_OFFSET) = Value;

    cmd = SPMI_ARB_FMT_CMD(SPMI_ARB_OP_EXT_WRITEL, addr16, bc);
    *(volatile ULONG *)(Ctx->ChnlsBase + offset + SPMI_ARB_CHNL_CMD_OFFSET) = cmd;

    for (;;) {
        status = *(volatile ULONG *)(Ctx->ChnlsBase + offset + SPMI_ARB_CHNL_STATUS_OFFSET);

        if (status & SPMI_ARB_STATUS_DONE_BIT) {
            break;
        }

        if (elapsedUs >= SPMI_ARB_POLL_TIMEOUT_US) {
            return STATUS_IO_TIMEOUT;
        }

        KeStallExecutionProcessor(SPMI_ARB_POLL_INTERVAL_US);
        elapsedUs += SPMI_ARB_POLL_INTERVAL_US;
    }

    if (status & SPMI_ARB_STATUS_DENIED_BIT) {
        return STATUS_ACCESS_DENIED;
    }

    if (status & (SPMI_ARB_STATUS_FAILURE_BIT | SPMI_ARB_STATUS_DROPPED_BIT)) {
        return STATUS_DEVICE_PROTOCOL_ERROR;
    }

    return STATUS_SUCCESS;
}
