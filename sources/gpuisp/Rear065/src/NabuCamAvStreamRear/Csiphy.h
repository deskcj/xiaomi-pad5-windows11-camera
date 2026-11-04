#pragma once
//
// Csiphy.h — CSIPHY (MIPI D-PHY) bring-up. СТАДИЯ 3.
// Рецепт — из mainline drivers/media/platform/qcom/camss/camss-csiphy-3ph-1-0.c
// (GPL-2.0). Наш блок = qcom,csiphy-v1.1 → таблица lane_regs_sc8280xp[] (GEN2 1.1).
// Fallback-кандидат: lane_regs_sdm845[] (GEN2 1.0), если v1.1 не заведётся.
//
// НЕ протестировано на железе. Требует: домен камеры поднят (через Platform.c),
// CSIPHY-такты включены, база замаплена. Проверять по одному шагу с дампом.
//
#include <ntddk.h>

// Физ. адреса CSIPHY (MMIO-карта, research/camera-hw-mmio-map.md). Каждый инстанс 0x1000.
// Задняя камера (OV13B10): csiphy-sd-index=1 (dtbo fragment@94) → CSIPHY1.
// Передняя (OV8856): csiphy-sd-index=2 → CSIPHY2.
#define CSIPHY0_PHYS_ADDR   0x0AC65000ULL
#define CSIPHY1_PHYS_ADDR   0x0AC66000ULL   // задняя
#define CSIPHY2_PHYS_ADDR   0x0AC67000ULL   // передняя
#define CSIPHY3_PHYS_ADDR   0x0AC68000ULL
#define CSIPHY_REGION_SIZE  0x1000UL

// Общий блок управления (CMN): offset 0x800, регистры CTRLn = 0x800 + 4*n.
#define CSIPHY_CMN_OFFSET             0x800UL
#define CSIPHY_CMN_STATUS_OFFSET      0xB0UL
#define CSIPHY_CMN_CTRLn(n)           (CSIPHY_CMN_OFFSET + 0x4UL * (n))
#define CSIPHY_CMN_STATUSn(n)         (CSIPHY_CMN_OFFSET + CSIPHY_CMN_STATUS_OFFSET + 0x4UL * (n))
#define CSIPHY_CTRL5_CLK_ENABLE       (1UL << 7)   // бит7 в CTRL5
#define CSIPHY_CTRL6_COMMON_PWRDN_B   (1UL << 0)
#define CSIPHY_CTRL6_SHOW_REV_ID      (1UL << 1)

// Тип поля в таблице настроек линий.
typedef enum _CSIPHY_PARAM_TYPE {
    CSIPHY_DEFAULT_PARAMS = 0,
    CSIPHY_LANE_ENABLE = 1,
    CSIPHY_SETTLE_CNT_LOWER_BYTE = 2,
    CSIPHY_SETTLE_CNT_HIGHER_BYTE = 3,
    CSIPHY_DNP_PARAMS = 4,      // do-not-program (пропустить)
    CSIPHY_2PH_REGS = 5,
    CSIPHY_3PH_REGS = 6,
    CSIPHY_SKEW_CAL = 7         // пропустить (skew cal off по умолчанию)
} CSIPHY_PARAM_TYPE;

typedef struct _CSIPHY_LANE_REG {
    ULONG RegAddr;
    ULONG RegData;
    ULONG DelayUs;
    CSIPHY_PARAM_TYPE ParamType;
} CSIPHY_LANE_REG;

// Software reset: CTRL0=1, 5мс, CTRL0=0.
VOID CsiphyReset(_In_ PUCHAR Base);

// Чтение HW-версии (для валидации, что база верная): CTRL6=SHOW_REV_ID → STATUS12..15.
ULONG CsiphyReadHwVersion(_In_ PUCHAR Base);

// Расчёт settle count по частоте линии CSI2 (link_freq, Гц) и такту таймера CSIPHY (Гц).
UCHAR CsiphySettleCntCalc(_In_ LONGLONG LinkFreqHz, _In_ ULONG TimerClkRateHz);

// Включить линии: CMN CTRL5/6/7/0, затем таблица lane_regs (settle подставляется),
// затем маски IRQ = 0. NumDataLanes для OV13B10 = 4.
VOID CsiphyLanesEnable(_In_ PUCHAR Base, _In_ ULONG NumDataLanes, _In_ UCHAR SettleCnt);
VOID CsiphyIrqClear(_In_ PUCHAR Base);

// Выключить линии (CTRL5=0, CTRL6=0).
VOID CsiphyLanesDisable(_In_ PUCHAR Base);
