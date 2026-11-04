//
// Csiphy.c — CSIPHY v1.1 bring-up (порт mainline camss-csiphy-3ph-1-0.c, GPL-2.0).
// СТАДИЯ 3. НЕ протестировано на железе — проверять по шагам с дампом.
//
#include "Csiphy.h"

#define RD32(b,o)     (*(volatile ULONG *)((PUCHAR)(b) + (o)))
#define WR32(b,o,v)   (*(volatile ULONG *)((PUCHAR)(b) + (o)) = (ULONG)(v))

//
// Таблица инициализации линий, GEN2 1.1 (qcom,csiphy-v1.1). Из mainline
// lane_regs_sc8280xp[]. Формат: {reg, data, delay_us, type}. Порядок КРИТИЧЕН.
// Блоки на линию идут со смещением 0x200 (lane0 = 0x000.., lane-clk = 0x700..).
//
static const CSIPHY_LANE_REG g_LaneRegs_v11[] = {
    // lane 0
    {0x0004,0x0C,0,CSIPHY_DEFAULT_PARAMS},{0x002C,0x01,0,CSIPHY_DEFAULT_PARAMS},
    {0x0034,0x0F,0,CSIPHY_DEFAULT_PARAMS},{0x001C,0x0A,0,CSIPHY_DEFAULT_PARAMS},
    {0x0014,0x60,0,CSIPHY_DEFAULT_PARAMS},{0x0028,0x00,0,CSIPHY_DNP_PARAMS},
    {0x003C,0xB8,0,CSIPHY_DEFAULT_PARAMS},{0x0000,0x90,0,CSIPHY_DEFAULT_PARAMS},
    {0x0008,0x0E,0,CSIPHY_SETTLE_CNT_LOWER_BYTE},{0x000C,0x00,0,CSIPHY_DNP_PARAMS},
    {0x0010,0x52,0,CSIPHY_DEFAULT_PARAMS},{0x0038,0xFE,0,CSIPHY_DEFAULT_PARAMS},
    {0x0060,0x00,0,CSIPHY_DEFAULT_PARAMS},{0x0064,0x7F,0,CSIPHY_DEFAULT_PARAMS},
    // clock lane (0x700)
    {0x0704,0x0C,0,CSIPHY_DEFAULT_PARAMS},{0x072C,0x01,0,CSIPHY_DEFAULT_PARAMS},
    {0x0734,0x0F,0,CSIPHY_DEFAULT_PARAMS},{0x071C,0x0A,0,CSIPHY_DEFAULT_PARAMS},
    {0x0714,0x60,0,CSIPHY_DEFAULT_PARAMS},{0x0728,0x04,0,CSIPHY_DEFAULT_PARAMS},
    {0x073C,0xB8,0,CSIPHY_DEFAULT_PARAMS},{0x0700,0x80,0,CSIPHY_DEFAULT_PARAMS},
    {0x0708,0x0E,0,CSIPHY_SETTLE_CNT_LOWER_BYTE},{0x070C,0xA5,0,CSIPHY_DEFAULT_PARAMS},
    {0x0710,0x52,0,CSIPHY_DEFAULT_PARAMS},{0x0738,0x1F,0,CSIPHY_DEFAULT_PARAMS},
    {0x0760,0x00,0,CSIPHY_DEFAULT_PARAMS},{0x0764,0x7F,0,CSIPHY_DEFAULT_PARAMS},
    // lane 1 (0x200)
    {0x0204,0x0C,0,CSIPHY_DEFAULT_PARAMS},{0x022C,0x01,0,CSIPHY_DEFAULT_PARAMS},
    {0x0234,0x0F,0,CSIPHY_DEFAULT_PARAMS},{0x021C,0x0A,0,CSIPHY_DEFAULT_PARAMS},
    {0x0214,0x60,0,CSIPHY_DEFAULT_PARAMS},{0x0228,0x00,0,CSIPHY_DNP_PARAMS},
    {0x023C,0xB8,0,CSIPHY_DEFAULT_PARAMS},{0x0200,0x90,0,CSIPHY_DEFAULT_PARAMS},
    {0x0208,0x0E,0,CSIPHY_SETTLE_CNT_LOWER_BYTE},{0x020C,0x00,0,CSIPHY_DNP_PARAMS},
    {0x0210,0x52,0,CSIPHY_DEFAULT_PARAMS},{0x0238,0xFE,0,CSIPHY_DEFAULT_PARAMS},
    {0x0260,0x00,0,CSIPHY_DEFAULT_PARAMS},{0x0264,0x7F,0,CSIPHY_DEFAULT_PARAMS},
    // lane 2 (0x400)
    {0x0404,0x0C,0,CSIPHY_DEFAULT_PARAMS},{0x042C,0x01,0,CSIPHY_DEFAULT_PARAMS},
    {0x0434,0x0F,0,CSIPHY_DEFAULT_PARAMS},{0x041C,0x0A,0,CSIPHY_DEFAULT_PARAMS},
    {0x0414,0x60,0,CSIPHY_DEFAULT_PARAMS},{0x0428,0x00,0,CSIPHY_DNP_PARAMS},
    {0x043C,0xB8,0,CSIPHY_DEFAULT_PARAMS},{0x0400,0x90,0,CSIPHY_DEFAULT_PARAMS},
    {0x0408,0x0E,0,CSIPHY_SETTLE_CNT_LOWER_BYTE},{0x040C,0x00,0,CSIPHY_DNP_PARAMS},
    {0x0410,0x52,0,CSIPHY_DEFAULT_PARAMS},{0x0438,0xFE,0,CSIPHY_DEFAULT_PARAMS},
    {0x0460,0x00,0,CSIPHY_DEFAULT_PARAMS},{0x0464,0x7F,0,CSIPHY_DEFAULT_PARAMS},
    // lane 3 (0x600)
    {0x0604,0x0C,0,CSIPHY_DEFAULT_PARAMS},{0x062C,0x01,0,CSIPHY_DEFAULT_PARAMS},
    {0x0634,0x0F,0,CSIPHY_DEFAULT_PARAMS},{0x061C,0x0A,0,CSIPHY_DEFAULT_PARAMS},
    {0x0614,0x60,0,CSIPHY_DEFAULT_PARAMS},{0x0628,0x00,0,CSIPHY_DNP_PARAMS},
    {0x063C,0xB8,0,CSIPHY_DEFAULT_PARAMS},{0x0600,0x90,0,CSIPHY_DEFAULT_PARAMS},
    {0x0608,0x0E,0,CSIPHY_SETTLE_CNT_LOWER_BYTE},{0x060C,0x00,0,CSIPHY_DNP_PARAMS},
    {0x0610,0x52,0,CSIPHY_DEFAULT_PARAMS},{0x0638,0xFE,0,CSIPHY_DEFAULT_PARAMS},
    {0x0660,0x00,0,CSIPHY_DEFAULT_PARAMS},{0x0664,0x7F,0,CSIPHY_DEFAULT_PARAMS},
};

VOID
CsiphyReset(_In_ PUCHAR Base)
{
    WR32(Base, CSIPHY_CMN_CTRLn(0), 0x1);
    KeStallExecutionProcessor(6000);   // 5-8 мс
    WR32(Base, CSIPHY_CMN_CTRLn(0), 0x0);
}

ULONG
CsiphyReadHwVersion(_In_ PUCHAR Base)
{
    ULONG v;
    WR32(Base, CSIPHY_CMN_CTRLn(6), CSIPHY_CTRL6_SHOW_REV_ID);
    v  =  RD32(Base, CSIPHY_CMN_STATUSn(12));
    v |= (RD32(Base, CSIPHY_CMN_STATUSn(13)) << 8);
    v |= (RD32(Base, CSIPHY_CMN_STATUSn(14)) << 16);
    v |= (RD32(Base, CSIPHY_CMN_STATUSn(15)) << 24);
    return v;
}

UCHAR
CsiphySettleCntCalc(_In_ LONGLONG LinkFreqHz, _In_ ULONG TimerClkRateHz)
{
    ULONGLONG ui, tHsSettle, timerPeriod;
    if (LinkFreqHz <= 0 || TimerClkRateHz == 0) {
        return 0x0E;   // безопасный дефолт из таблицы v1.1
    }
    ui = 1000000000000ULL / (ULONGLONG)LinkFreqHz;   // пс
    ui /= 2;
    tHsSettle = 85000ULL + 6ULL * ui;                // пс
    timerPeriod = 1000000000000ULL / (ULONGLONG)TimerClkRateHz;
    if (timerPeriod == 0) { return 0x0E; }
    return (UCHAR)((tHsSettle / timerPeriod) - 6);
}

VOID
CsiphyLanesEnable(_In_ PUCHAR Base, _In_ ULONG NumDataLanes, _In_ UCHAR SettleCnt)
{
    ULONG val, i;
    const CSIPHY_LANE_REG *r;
    static const UCHAR irqMasks[11] = {
        0xFF, 0xFF, 0xFB, 0xFF, 0x7F, 0xFF,
        0xFF, 0xEF, 0xFF, 0xFF, 0xFF
    };

    // CTRL5 = CLK_ENABLE(бит7) | для каждой data-линии BIT(pos*2).
    val = CSIPHY_CTRL5_CLK_ENABLE;
    for (i = 0; i < NumDataLanes; i++) {
        val |= (1UL << (i * 2));
    }
    WR32(Base, CSIPHY_CMN_CTRLn(5), val);
    WR32(Base, CSIPHY_CMN_CTRLn(6), CSIPHY_CTRL6_COMMON_PWRDN_B);
    WR32(Base, CSIPHY_CMN_CTRLn(7), 0x02);
    // Qualcomm v1.1 requires this one-millisecond reset pulse.
    WR32(Base, CSIPHY_CMN_CTRLn(0), 0x01);
    KeStallExecutionProcessor(1000);
    WR32(Base, CSIPHY_CMN_CTRLn(0), 0x00);

    // Таблица линий (gen2). settle подставляется в SETTLE_CNT_LOWER_BYTE; SKEW/DNP пропуск.
    for (i = 0; i < RTL_NUMBER_OF(g_LaneRegs_v11); i++) {
        r = &g_LaneRegs_v11[i];
        if (r->ParamType == CSIPHY_SKEW_CAL || r->ParamType == CSIPHY_DNP_PARAMS) {
            continue;
        }
        if (r->ParamType == CSIPHY_SETTLE_CNT_LOWER_BYTE) {
            val = (ULONG)(SettleCnt & 0xFF);
        } else {
            val = r->RegData;
        }
        WR32(Base, r->RegAddr, val);
        if (r->DelayUs) {
            KeStallExecutionProcessor(r->DelayUs);
        }
    }

    // Маски IRQ (CTRL11..21) = 0 — отключить все прерывания PHY.
    for (i = 11; i < 22; i++) {
        WR32(Base, CSIPHY_CMN_CTRLn(i), irqMasks[i - 11]);
    }
}

VOID
CsiphyIrqClear(_In_ PUCHAR Base)
{
    ULONG i, status;

    for (i = 0; i < 11; i++) {
        status = RD32(Base, 0x8B0 + (i * 4));
        WR32(Base, 0x858 + (i * 4), status);
        WR32(Base, 0x858 + (i * 4), 0);
    }
    WR32(Base, 0x828, 1);
    WR32(Base, 0x828, 0);
}

VOID
CsiphyLanesDisable(_In_ PUCHAR Base)
{
    WR32(Base, CSIPHY_CMN_CTRLn(5), 0);
    WR32(Base, CSIPHY_CMN_CTRLn(6), 0);
}
