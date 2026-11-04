#pragma once

//
// Физическая карта MMIO камера-подсистемы Qualcomm SM8150 (кодовое имя "nabu",
// Xiaomi Pad 5). Источник: research/camera-hw-mmio-map.md — извлечено напрямую
// из реального device tree (DTB) этого планшета, не из документации (которой
// для этого чипа/устройства публично не существует).
//
// Всё в диапазоне физических адресов 0xac00000 - 0xad10000 (~16.5 МБ).
//
// (копия ../Registers.h — держать в синхроне вручную, это отдельный
// самодостаточный тестовый проект)
//

#define NABU_CAM_CCI0_PHYS_ADDR         0x0ac4a000ULL
#define NABU_CAM_CCI1_PHYS_ADDR         0x0ac4b000ULL
#define NABU_CAM_CCI_REGION_SIZE        0x1000UL

#define NABU_CAM_CSIPHY0_PHYS_ADDR      0x0ac65000ULL
#define NABU_CAM_CSIPHY1_PHYS_ADDR      0x0ac66000ULL
#define NABU_CAM_CSIPHY2_PHYS_ADDR      0x0ac67000ULL
#define NABU_CAM_CSIPHY3_PHYS_ADDR      0x0ac68000ULL
#define NABU_CAM_CSIPHY_REGION_SIZE     0x1000UL

#define NABU_CAM_CPAS_TOP_PHYS_ADDR     0x0ac40000ULL
#define NABU_CAM_CPAS_CAMNOC_PHYS_ADDR  0x0ac42000ULL
#define NABU_CAM_CPAS_CDM0_PHYS_ADDR    0x0ac48000ULL

#define NABU_CAM_CSID0_PHYS_ADDR        0x0acb3000ULL
#define NABU_CAM_VFE0_PHYS_ADDR         0x0acaf000ULL
#define NABU_CAM_CSID1_PHYS_ADDR        0x0acba000ULL
#define NABU_CAM_VFE1_PHYS_ADDR         0x0acb6000ULL
#define NABU_CAM_CSID_REGION_SIZE       0x1000UL
#define NABU_CAM_VFE_REGION_SIZE        0x4000UL

#define NABU_CAM_CAMCC_PHYS_ADDR        0x0ad00000ULL   // qcom,camcc-sm8150-v2
#define NABU_CAM_CAMCC_REGION_SIZE      0x10000UL

//
// ДОБАВЛЕНО 2026-07-02 (4-я сессия): GCC — ГЛОБАЛЬНЫЙ контроллер тактов.
// Новая гипотеза причины "titan GDSC не завершает POWER_UP_COMPLETE": домен
// CAMCC не может запитаться, пока НЕ включены камера-такты ВЕРХНЕГО уровня в
// GCC (AHB/AXI-фабрика к камера-подсистеме). Мы их ни разу не трогали. В
// отличие от VDD_CX/CAMCC-PLL (RPMh), эти такты включаются обычной записью
// CLK_ENABLE (бит0) — то есть доступны нам напрямую.
//
// Офсеты — из настоящего mainline drivers/clk/qcom/gcc-sm8150.c (GPL-2.0,
// сверено построчно 2026-07-02): все четыре — clk_branch, enable_mask BIT(0):
//   gcc_camera_ahb_clk     enable_reg = 0xb008
//   gcc_camera_hf_axi_clk  enable_reg = 0xb030
//   gcc_camera_sf_axi_clk  enable_reg = 0xb034
//   gcc_camera_xo_clk      enable_reg = 0xb044
//
#define NABU_GCC_PHYS_ADDR              0x00100000ULL
#define NABU_GCC_REGION_SIZE            0x20000UL
#define NABU_GCC_CAMERA_AHB_CBCR        0xb008UL
#define NABU_GCC_CAMERA_HF_AXI_CBCR     0xb030UL
#define NABU_GCC_CAMERA_SF_AXI_CBCR     0xb034UL
#define NABU_GCC_CAMERA_XO_CBCR         0xb044UL

// Окна широкого дампа (чистое чтение, для оффлайн-разбора всей карты тактов):
#define NABU_GCC_CAM_DUMP_START         0xb000UL   // [0xb000,0xb100) — вся камера-группа GCC
#define NABU_GCC_CAM_DUMP_COUNT         64
#define NABU_CAMCC_PLL0_DUMP_START      0x0000UL   // cam_cc_pll0 (mode/config/lock)
#define NABU_CAMCC_PLL0_DUMP_COUNT      32
#define NABU_CAMCC_MCLK_DUMP_START      0x5000UL   // mclk RCG/branch кластер
#define NABU_CAMCC_MCLK_DUMP_COUNT      32
#define NABU_CAMCC_BRANCH_DUMP_START    0xc100UL   // GDSC + все CBCR камеры
#define NABU_CAMCC_BRANCH_DUMP_COUNT    64

// CPAS_TOP — проверка живости фабрики: HW_VERSION по offset 0x0 (тот же
// регистр, что читает настоящий qccamplatform8150.sys). База уже определена
// выше (NABU_CAM_CPAS_TOP_PHYS_ADDR = 0x0ac40000).
#define NABU_CAM_CPAS_TOP_REGION_SIZE   0x1000UL
#define NABU_CPAS_TOP_HW_VERSION_OFFSET 0x0UL

//
// ДОБАВЛЕНО 2026-07-02 (план B, RPMh): cmd_db — таблица RPMh-ресурсов (магия
// "CMD_DB", заголовок + директория "res_id(8 ASCII) -> RPMh-адрес"). Из
// vendor_dtb этого планшета: узел qcom,cmd-db@c3f000c => физ. 0x0C3F0000.
// Дамп первых 2КБ (заголовок + начало директории) для оффлайн-поиска
// ресурсов камеры / MMCX / CX и их RPMh-адресов. ЧИСТО ЧТЕНИЕ.
//
#define NABU_CMD_DB_PHYS_ADDR           0x0c3f0000ULL
#define NABU_CMD_DB_REGION_SIZE         0x10000UL
#define NABU_CMD_DB_DUMP_COUNT          512   // 2КБ (512 ULONG)

//
// ДОБАВЛЕНО 2026-07-06: APPS RSC (Resource State Coordinator) — механизм
// голосования за уровни питания рельс через RPMh. Нужен для САМОСТОЯТЕЛЬНОГО
// подъёма домена камеры (MMCX) без настоящего драйвера.
//
// Источники:
//   - DTB: mailbox@18220000/reg = {0x18220000, 0x100, 0x18220d00, 0x3000}
//   - Linux mainline: drivers/soc/qcom/rpmh-rsc.c
//   - dmesg: "TCS Busy, retrying RPMH message send" (подтверждение работы)
//   - DTB: soc/rpmh-regulator-mmcxlvl/qcom,resource-name = "mmcx.lvl"
//
#define NABU_APPS_RSC_PHYS_ADDR         0x18220000ULL  // DRV2 (единственный безопасный)
#define NABU_APPS_RSC_REGION_SIZE       0x4000UL       // DRV2 regs + TCS
// НЕ мапить 0x18200000 — фолтит/BSOD!
#define RSC_DRV2_OFFSET                 0x00000        // DRV2 = наш base
// Внутри каждого DRV — TCS начинается с +0xd00

// RPMh адрес ресурса MMCX (камерный домен питания).
// Стандартный для SM8150 ARC: slave_id=3, offset=0x0c -> addr=0x3000c.
// Подтверждено DTB: qcom,resource-name = "mmcx.lvl".
#define NABU_RPMH_CX_ADDR              0x00030000UL   // cx.lvl (CX rail, parent of MMCX)
#define NABU_RPMH_MMCX_ADDR            0x0003000cUL   // mmcx.lvl (camera/multimedia)

// Voltage corner levels (значение для голосования).
#define RPMH_CORNER_OFF                 0
#define RPMH_CORNER_RETENTION           1
#define RPMH_CORNER_MIN_SVS             2
#define RPMH_CORNER_LOW_SVS             3
#define RPMH_CORNER_SVS                 4
#define RPMH_CORNER_SVS_L1              5
#define RPMH_CORNER_NOMINAL             6
#define RPMH_CORNER_NOMINAL_L1          7
#define RPMH_CORNER_TURBO               8

// RSC DRV register offsets (from mainline rpmh-rsc.c, SM8150).
// TCS[n] начинается с DRV_base + RSC_TCS_DRV_OFFSET + n * RSC_TCS_STRIDE.
#define RSC_DRV_ID_OFFSET               0x0
#define RSC_DRV_PARAM_OFFSET            0x4     // bits[7:0]=num_tcs, bits[15:8]=ncpt (cmds per tcs)
#define RSC_TCS_DRV_OFFSET              0xd00   // base of TCS array
#define RSC_TCS_STRIDE                  0x200   // stride between TCS slots (SM8150)

// Per-TCS registers (offset from TCS[n] base).
#define RSC_TCS_CONTROL                 0x00    // write 1 to trigger
#define RSC_TCS_STATUS                  0x04    // bit0: 1=idle, 0=busy
#define RSC_TCS_CMD_WAIT_FOR_CMPL       0x10    // bitmask: which cmds wait for completion
#define RSC_TCS_CMD_ENABLE              0x1c    // bitmask: which cmd slots are active
#define RSC_TCS_CMD_ADDR(i)             (0x30 + (i) * 0x10)
#define RSC_TCS_CMD_DATA(i)             (0x34 + (i) * 0x10)
#define RSC_TCS_CMD_STATUS(i)           (0x38 + (i) * 0x10)

// Количество TCS для дампа (read-only диагностика).
#define RSC_MAX_TCS_FOR_DIAG            16
#define RSC_MAX_CMDS_PER_TCS_FOR_DIAG   4   // дампим первые 4 команды каждого TCS

//
// CCI queue-engine — регистровые формулы, подтверждённые декомпиляцией
// cam_cci_i2c_write/cam_cci_core_cfg из реального kernel_Image этого планшета.
// См. research/cci-code-analysis.md для полного разбора.
//
// Каждый CCI-блок имеет 2 аппаратных I2C-мастера (M0/M1), у каждого мастера —
// 2 очереди команд (Q0 = обычная запись, Q1 = чтение и sync-write).
//

#define CCI_MASTER_COUNT   2
#define CCI_QUEUE_WRITE    0
#define CCI_QUEUE_READ     1

#define CCI_QUEUE_BASE(master, queue) \
    (((ULONG)(master) * 0x200) + ((ULONG)(queue) * 0x100))

#define CCI_EXEC_WORD_CNT_OFFSET(master, queue)   (CCI_QUEUE_BASE(master, queue) + 0x300)
#define CCI_CUR_WORD_CNT_OFFSET(master, queue)    (CCI_QUEUE_BASE(master, queue) + 0x304)
#define CCI_LOAD_DATA_ADDR_OFFSET(master, queue)  (CCI_QUEUE_BASE(master, queue) + 0x310)

// Общий регистр триггера/IRQ на все 2 мастера x 2 очереди (4 бита), НЕ per-queue.
#define CCI_QUEUE_TRIGGER_OFFSET   0x8
#define CCI_QUEUE_TRIGGER_BIT(master, queue) \
    (1UL << ((ULONG)(queue) + (ULONG)(master) * 2))

// Отдельная область регистров результата ЧТЕНИЯ — НЕ там же, где очередь команд.
#define CCI_READ_BUF_BASE(master)          ((ULONG)(master) * 0x100)
#define CCI_READ_BUF_DATA_OFFSET(master)   (CCI_READ_BUF_BASE(master) + 0x118)
#define CCI_READ_BUF_LEVEL_OFFSET(master)  (CCI_READ_BUF_BASE(master) + 0x11c)

//
// ДОБАВЛЕНО 2026-07-02: сверка с ДРУГИМ, независимым настоящим источником —
// современным mainline Linux-драйвером drivers/i2c/busses/i2c-qcom-cci.c
// (GPL-2.0, скачан и сверен построчно 2026-07-02). См. подробный комментарий
// в ../Registers.h (эта копия держится в синхроне вручную) — совпадения
// подтверждают наши офсеты (CCI_QUEUE_START=0x8, HW_VERSION=0x0, READ_BUF
// формула), а также показывают недостающий шаг: регистры тайминга I2C-шины
// (SCL_CTL/SDA_CTL_0/1/2/MISC_CTL), которых нет в нашем CciMasterInit.
//
#define CCI_SCL_CTL_OFFSET(master)     (0x100UL + 0x100UL * (ULONG)(master))
#define CCI_SDA_CTL_0_OFFSET(master)   (0x104UL + 0x100UL * (ULONG)(master))
#define CCI_SDA_CTL_1_OFFSET(master)   (0x108UL + 0x100UL * (ULONG)(master))
#define CCI_SDA_CTL_2_OFFSET(master)   (0x10cUL + 0x100UL * (ULONG)(master))
#define CCI_MISC_CTL_OFFSET(master)    (0x110UL + 0x100UL * (ULONG)(master))

#define CCI_IRQ_STATUS_0_OFFSET             0xc0cUL

// Completion/report bits — ИСПРАВЛЕНО 2026-07-08 под ТОЧНУЮ раскладку mainline
// i2c-qcom-cci.c (прежние значения были неверны -> completion не детектился, вечный
// таймаут). Подтверждено замером: на реальном железе Q1_REPORT master1 = BIT(11)=0x800.
//   M0: RD_DONE=BIT(0), Q0_REPORT=BIT(8), Q1_REPORT=BIT(9)
//   M1: RD_DONE=BIT(12), Q0_REPORT=BIT(10), Q1_REPORT=BIT(11)
#define CCI_IRQ_STATUS_0_M0_RD_DONE_BIT      0x00000001UL  // BIT(0)
#define CCI_IRQ_STATUS_0_M0_Q0_REPORT_BIT    0x00000100UL  // BIT(8)
#define CCI_IRQ_STATUS_0_M0_Q1_REPORT_BIT    0x00000200UL  // BIT(9)
#define CCI_IRQ_STATUS_0_M1_RD_DONE_BIT      0x00001000UL  // BIT(12)
#define CCI_IRQ_STATUS_0_M1_Q0_REPORT_BIT    0x00000400UL  // BIT(10)
#define CCI_IRQ_STATUS_0_M1_Q1_REPORT_BIT    0x00000800UL  // BIT(11)

// NACK bits
#define CCI_IRQ_STATUS_0_M0_Q0_NACK_ERR_BIT  0x08000000UL  // BIT(27)
#define CCI_IRQ_STATUS_0_M0_Q1_NACK_ERR_BIT  0x10000000UL  // BIT(28)
#define CCI_IRQ_STATUS_0_M1_Q0_NACK_ERR_BIT  0x20000000UL  // BIT(29)
#define CCI_IRQ_STATUS_0_M1_Q1_NACK_ERR_BIT  0x40000000UL  // BIT(30)

#define CCI_IRQ_STATUS_0_M0_ERROR_MASK        0x18000ee6UL // все ошибки master0
#define CCI_IRQ_STATUS_0_RST_DONE_ACK_BIT     0x01000000UL // BIT(24)

// ДОБАВЛЕНО 2026-07-02: CCI_RESET_CMD, буквально из mainline
// i2c-qcom-cci.c — см. подробный комментарий в ../Registers.h.
#define CCI_RESET_CMD_OFFSET           0x004UL
#define CCI_RESET_CMD_M0_MASK          0x000003f1UL
#define CCI_RESET_CMD_M1_MASK          0x0003f001UL
#define CCI_IRQ_CLEAR_0_OFFSET         0xc08UL
#define CCI_IRQ_GLOBAL_CLEAR_CMD_OFFSET 0xc00UL

// Опкоды командных слов CCI (подтверждены буквально в декомпилированном
// cam_cci_core_cfg, не по аналогии с legacy msm_cci).
#define CCI_OPCODE_SET_PARAM      0x1UL
#define CCI_OPCODE_LOCK           0x6UL
#define CCI_OPCODE_UNLOCK         0x7UL
#define CCI_OPCODE_READ_N_BYTES   0xAUL
#define CCI_OPCODE_WRITE_ADDR     0xBUL

// cmd_word = (slaveAddr7bit << 4) | (retries << 0x10) | (id << 0x12) | SET_PARAM(1)
#define CCI_SET_PARAM_WORD(slaveAddr7bit, retries, id) \
    ((((ULONG)(slaveAddr7bit)) << 4) | \
     (((ULONG)(retries)) << 0x10) | \
     (((ULONG)(id)) << 0x12) | \
     CCI_OPCODE_SET_PARAM)

// (addrByteCount << 4) | WRITE_ADDR(0xb) — "далее следует N байт адреса регистра"
#define CCI_WRITE_ADDR_WORD(addrByteCount) \
    ((((ULONG)(addrByteCount)) << 4) | CCI_OPCODE_WRITE_ADDR)

// (numBytesToRead << 4) | READ_N_BYTES(0xa)
#define CCI_READ_N_BYTES_WORD(numBytes) \
    ((((ULONG)(numBytes)) << 4) | CCI_OPCODE_READ_N_BYTES)

//
// I2C slave-адреса сенсоров — подтверждены из dmesg реальной загрузки Android
// (research/android-boot-camera-dmesg.md).
//
//
// ВАЖНО 2026-07-02: В Android DT (qcom,slave-id) и dmesg адрес хранится как
// 8-битный SCCB write-адрес (конвенция OmniVision). В CCI hardware нужен
// 7-битный адрес = addr >> 1 (подтверждено cam_cci_core.c:
//   slave_addr = sensor_slave_info->slave_addr >> 1).
// NABU_SENSOR_I2C_SLAVE_ADDR_7BIT используется ВЕЗДЕ в CCI_SET_PARAM_WORD.
#define NABU_SENSOR_I2C_SLAVE_ADDR        0x20  // 8-битный write-адрес (Android/dmesg, исходник)
#define NABU_SENSOR_I2C_SLAVE_ADDR_7BIT   0x10  // 7-битный для CCI = 0x20 >> 1

// Chip ID сенсоров — подтверждены из dmesg (slot 2 = фронт OV8856, slot 0 = тыл
// OV13B10) И независимо из ACPI DSDT этого же планшета в Windows-режиме
// (research/qccamavs-and-sensor-driver-analysis.md, раздел "Сверка с ACPI DSDT").
#define NABU_SENSOR_CHIPID_REG_ADDR     0x300B   // общий для обоих (серия OV)
#define NABU_SENSOR_FRONT_OV8856_CHIPID 0x885A
#define NABU_SENSOR_REAR_OV13B10_CHIPID 0x0D42

//
// ДОБАВЛЕНО 2026-07-02: TLMM (GPIO-контроллер) — сброс сенсоров.
//
// Формат регистров одного пина (CFG/IN_OUT) — из mainline
// drivers/pinctrl/qcom/pinctrl-sm8150.c, макрос PINGROUP (скачан и сверен
// построчно 2026-07-02):
//   ctl_reg = 0x1000 * id   (id = ГЛОБАЛЬНЫЙ номер GPIO, не локальный внутри тайла)
//   io_reg  = ctl_reg + 0x4
//   mux_bit=2 (3 бита, функция; 0 = "gpio"), pull_bit=0, drv_bit=6,
//   oe_bit=9 (1 бит, output-enable), в IO_REG: in_bit=0, out_bit=1.
//
// TLMM на этом чипе в mainline разбит на 4 отдельных resource (тайла),
// подтверждено в mainline sm8150.dtsi:
//   west=0x03100000 east=0x03500000 north=0x03900000 south=0x03d00000
//   (каждый размер 0x300000; GPIO 28 и GPIO 30 оба в тайле EAST — см.
//   sm8150_groups[] в pinctrl-sm8150.c).
//
// НО живой /proc/device-tree ЭТОГО планшета (Android, см.
// research/sensor-gpio-regulator-findings.md) показывает другой (downstream,
// Linux 4.14 CAF) биндинг — ОДИН флэт регион "pinctrl", reg=0x03000000,
// размер 0xdc2000 (+маленький доп. регион "spi_cfg"), а не 4 именованных
// тайла. Формат САМИХ регистров от этого не меняется — это то же самое
// аппаратное ядро (compatible="qcom,sm8150-pinctrl" совпадает в обоих
// случаях), просто другое программное разбиение MMIO на именованные куски.
//
// Проверка (не циклическая): абсолютные адреса WEST/EAST/NORTH из mainline
// dtsi физически попадают внутрь диапазона живого региона этого планшета
// [0x03000000, 0x03dc2000) — значит это один и тот же физический блок.
// Мапим EAST напрямую по его АБСОЛЮТНОМУ mainline-адресу.
//
#define NABU_TLMM_EAST_PHYS_ADDR    0x03500000ULL
#define NABU_TLMM_EAST_REGION_SIZE  0x20000UL   // с запасом на GPIO 28 и 30

#define NABU_TLMM_GPIO_CTL_REG_OFFSET(gpio)  (0x1000UL * (ULONG)(gpio))
#define NABU_TLMM_GPIO_IO_REG_OFFSET(gpio)   (0x1000UL * (ULONG)(gpio) + 0x4UL)

#define NABU_TLMM_GPIO_MUX_BIT_SHIFT   2   // 3 бита, значение 0 = "gpio"
#define NABU_TLMM_GPIO_MUX_MASK        (0x7UL << NABU_TLMM_GPIO_MUX_BIT_SHIFT)
#define NABU_TLMM_GPIO_OE_BIT          (1UL << 9)
#define NABU_TLMM_GPIO_IO_OUT_BIT      (1UL << 1)
#define NABU_TLMM_GPIO_IO_IN_BIT       (1UL << 0)

// Номера GPIO — из живого device tree (research/sensor-gpio-regulator-findings.md):
// CAM_RESET0 (тыл, OV13B10, наш CciMaster0) = GPIO 28
// CAM_RESET2 (фронт, OV8856, CciMaster1)    = GPIO 30
#define NABU_SENSOR_REAR_RESET_GPIO   28
#define NABU_SENSOR_FRONT_RESET_GPIO  30

//
// ДОБАВЛЕНО 2026-07-02 (вторая сессия): MCLK — внешний тактовый сигнал
// СЕНСОРА (XVCLK). До сих пор мы включали только такты шины CCI, но НЕ такт
// самого сенсора — а у OmniVision-сенсоров вся цифровая логика (включая
// I2C-блок!) тактируется от XVCLK: без него сенсор физически не может
// ответить на I2C, даже при идеальном питании и снятом reset. Это
// вероятное объяснение картины "всё включено, reset снят, а шина молчит
// с нулём ошибок".
//
// Пины (живой device tree, research/sensor-gpio-regulator-findings.md):
// CAMIF_MCLK0 = GPIO 13 (тыл, OV13B10), CAMIF_MCLK2 = GPIO 14 (фронт, OV8856).
//
// В mainline pinctrl-sm8150.c (сверено построчно 2026-07-02):
//   [13] = PINGROUP(13, NORTH, cam_mclk, qdss, ...)
//   [14] = PINGROUP(14, NORTH, cam_mclk, qdss, ...)
// — оба пина в тайле NORTH (НЕ EAST, где reset-пины!), функция cam_mclk
// стоит первой (f1) => значение мультиплексора = 1 (0 = "gpio").
// NORTH из того же mainline sm8150.dtsi = 0x03900000 (та же проверка
// принадлежности живому региону [0x03000000, 0x03dc2000), что и для EAST).
//
#define NABU_TLMM_NORTH_PHYS_ADDR    0x03900000ULL
// РАЗМЕР 0x20000, НЕ 0x10000! GPIO31 (VANA фронта) = офсет 0x1F000 — при
// размере 0x10000 обращение к нему выходило за границу маппинга => BSOD
// (реально случился на планшете 2026-07-02, вторая сессия). GPIO13/14 —
// офсеты 0xD000/0xE000, GPIO11 — 0xB000, все внутри 0x20000.
#define NABU_TLMM_NORTH_REGION_SIZE  0x20000UL

#define NABU_TLMM_GPIO_MUX_CAM_MCLK  1UL  // f1 в PINGROUP(13/14, NORTH, cam_mclk, ...)

#define NABU_SENSOR_REAR_MCLK_GPIO   13
#define NABU_SENSOR_FRONT_MCLK_GPIO  14

//
// ДОБАВЛЕНО 2026-07-02: SPMI PMIC Arbiter — регуляторы питания сенсора
// (cam_vio/cam_vana/cam_vdig) висят на PMIC GPIO (pm8150l), НЕ на TLMM —
// доступ к ним идёт не через прямой флэт-MMIO, а через аппаратный командный
// "арбитр" шины SPMI. Все формулы ниже — из настоящего mainline
// drivers/spmi/spmi-pmic-arb.c (GPL-2.0, скачан и сверен построчно
// 2026-07-02), версия арбитра (v5) определена ТАК ЖЕ, как это делает сам
// Linux-драйвер — чтением регистра VERSION в рантайме, не предположением.
//
// Адреса регионов — из ЖИВОГО /proc/device-tree/soc/qcom,spmi@c440000
// этого планшета (reg-names = "core","chnls","obsrvr","intr","cnfg";
// #address-cells=1, #size-cells=1 у родителя "soc"). Раскодировано из
// od-дампа сырых байт (FDT big-endian, byte-swap на чтении od -tx4 на
// little-endian ARM64) и перекрёстно проверено: obsrvr начинается РОВНО
// там, где заканчивается chnls, а intr — где заканчивается obsrvr —
// однозначное подтверждение, что разбор байт верный.
//
#define SPMI_ARB_CORE_PHYS_ADDR      0x0c440000ULL
#define SPMI_ARB_CORE_REGION_SIZE    0x1100UL
#define SPMI_ARB_CNFG_PHYS_ADDR      0x0c40a000ULL
#define SPMI_ARB_CNFG_REGION_SIZE    0x26000UL
#define SPMI_ARB_CHNLS_PHYS_ADDR     0x0c600000ULL   // "chnls" (wr_base, запись) — 32МБ, мапим только маленькое окно на конкретный APID при реальной записи
#define SPMI_ARB_CHNLS_REGION_SIZE   0x02000000UL
#define SPMI_ARB_OBSRVR_PHYS_ADDR    0x0e600000ULL   // "obsrvr" (rd_base, чтение)
#define SPMI_ARB_OBSRVR_REGION_SIZE  0x00100000UL

#define SPMI_ARB_VERSION_OFFSET       0x0000UL
#define SPMI_ARB_VERSION_V5_MIN       0x50000000UL
#define SPMI_ARB_VERSION_V7_MIN       0x70000000UL
#define SPMI_ARB_FEATURES_OFFSET      0x0004UL
#define SPMI_ARB_FEATURES_PERIPH_MASK 0x7ffUL   // GENMASK(10,0)

// Таблица APID->PPID (версия v5): core + 0x900 + 4*apid. Формула и
// смещение 0x900 — буквально pmic_arb_apid_map_offset_v5() в mainline.
#define SPMI_ARB_APID_MAP_OFFSET_V5(apid)  (0x900UL + 4UL * (ULONG)(apid))
#define SPMI_ARB_PPID_MASK            0xfffUL   // GENMASK(11,0)
#define SPMI_ARB_PPID_SHIFT           8

// Владелец/write_ee (общая для v2/v3/v5): cnfg + 0x700 + 4*apid —
// буквально pmic_arb_apid_owner_v2() в mainline (используется и версией v5).
#define SPMI_ARB_APID_OWNER_OFFSET(apid)   (0x700UL + 4UL * (ULONG)(apid))
#define SPMI_ARB_OWNER_EE_MASK        0x7UL

// Канальные регистры (одинаковы для чтения через obsrvr и записи через
// chnls, отличается только база и формула офсета на канал):
#define SPMI_ARB_CHNL_CMD_OFFSET      0x00UL
#define SPMI_ARB_CHNL_STATUS_OFFSET   0x08UL
#define SPMI_ARB_CHNL_WDATA0_OFFSET   0x10UL
#define SPMI_ARB_CHNL_WDATA1_OFFSET   0x14UL
#define SPMI_ARB_CHNL_RDATA0_OFFSET   0x18UL
#define SPMI_ARB_CHNL_RDATA1_OFFSET   0x1cUL

// Формулы офсета канала (версия v5, pmic_arb_offset_v5 в mainline):
//   OBS (чтение, не требует владения): 0x10000*ee + 0x80*apid
//   RW  (запись, требует write_ee==наш ee): 0x10000*apid
// EE этого планшета подтверждён живым device tree (qcom,ee = 0).
#define SPMI_ARB_OBS_OFFSET(ee, apid)  (0x10000UL * (ULONG)(ee) + 0x80UL * (ULONG)(apid))
#define SPMI_ARB_RW_OFFSET(apid)       (0x10000UL * (ULONG)(apid))

#define SPMI_ARB_STATUS_DONE_BIT      (1UL << 0)
#define SPMI_ARB_STATUS_FAILURE_BIT   (1UL << 1)
#define SPMI_ARB_STATUS_DENIED_BIT    (1UL << 2)
#define SPMI_ARB_STATUS_DROPPED_BIT   (1UL << 3)

// Внутренние опкоды арбитра (enum pmic_arb_cmd_op_code в mainline — НЕ
// путать с "проводными" SPMI-опкодами SPMI_CMD_EXT_*, это другое число).
#define SPMI_ARB_OP_EXT_WRITEL   0UL
#define SPMI_ARB_OP_EXT_READL    1UL

// cmd = (opc<<27) | ((addr&0xff)<<4) | (bc&0x7) — буквально pmic_arb_fmt_cmd_v2().
#define SPMI_ARB_FMT_CMD(opc, addr, bc) \
    ((((ULONG)(opc)) << 27) | ((((ULONG)(addr)) & 0xffUL) << 4) | (((ULONG)(bc)) & 0x7UL))

// Целевая периферия — pinctrl@c000 (qcom,spmi-gpio) на pm8150l@4 в живом
// device tree: SID = 4 (unit-address узла), базовый PID = 0xc0 (reg=0xc000
// узла pinctrl@c000, PID = reg>>8), 12 пинов с шагом 0x100/пин
// (research/sensor-gpio-regulator-findings.md + mainline
// pinctrl-spmi-gpio.c, PMIC_GPIO_ADDRESS_RANGE=0x100).
//
// cam_sensor_wide_vdig_enable/disable -> pins="gpio3" (1-индексация в DT
// самого qcom,spmi-gpio) -> внутренний индекс пина = 2 -> PID = 0xc0+2 = 0xc2.
#define SPMI_PM8150L_SID              4UL
#define SPMI_PM8150L_GPIO_BASE_PID    0xc0UL
#define SPMI_PM8150L_GPIO3_PID        (SPMI_PM8150L_GPIO_BASE_PID + 2UL)
#define SPMI_OUR_EE                   0UL   // подтверждено живым device tree (qcom,ee)

// Регистры одного пина PMIC-GPIO относительно его PID-страницы (addr&0xff) —
// из mainline drivers/pinctrl/qcom/pinctrl-spmi-gpio.c.
#define SPMI_PMIC_GPIO_REG_MODE_CTL      0x40UL
#define SPMI_PMIC_GPIO_REG_DIG_VIN_CTL   0x41UL
#define SPMI_PMIC_GPIO_REG_DIG_PULL_CTL  0x42UL
#define SPMI_PMIC_GPIO_REG_DIG_IN_CTL    0x43UL
#define SPMI_PMIC_GPIO_REG_DIG_OUT_SRC   0x44UL
#define SPMI_PMIC_GPIO_REG_DIG_OUT_CTL   0x45UL
#define SPMI_PMIC_GPIO_REG_EN_CTL        0x46UL

//
// ДОБАВЛЕНО 2026-07-02 (вторая сессия). РАЗГАДКА "записи MODE_CTL не
// работают": pm8150l — PMIC с GPIO НОВОГО подтипа LV/MV (mainline
// pinctrl-spmi-gpio.c, сверено построчно). У LV/MV:
//   - MODE_CTL принимает ТОЛЬКО биты[1:0]: 0=input, 1=output, 2=in/out,
//     3=analog-pass. Наше прочитанное MODE_CTL=1 => пин УЖЕ цифровой выход
//     (старая расшифровка "direction=INPUT по битам[6:4]" была для
//     СТАРОГО подтипа и неверна здесь).
//   - Наша запись 0x11 ложилась в зарезервированные биты => железо её
//     маскировало => "значение не изменилось". Никакой TZ-блокировки:
//     запись DIG_PULL_CTL прошла во всех трёх прогонах (4<->5).
//   - УРОВЕНЬ выхода задаётся отдельным регистром 0x44
//     (LV_MV_DIG_OUT_SOURCE_CTL): бит7 = значение, биты[3:0] = источник
//     (0 = обычная константа "normal").
// Подтип проверяем чтением TYPE(0x4)/SUBTYPE(0x5): TYPE должен быть 0x10
// (GPIO), SUBTYPE 0x10/0x11 (LV/MV).
//
#define SPMI_PMIC_GPIO_REG_TYPE          0x4UL
#define SPMI_PMIC_GPIO_REG_SUBTYPE       0x5UL
#define SPMI_PMIC_GPIO_TYPE_GPIO         0x10UL
#define SPMI_PMIC_GPIO_SUBTYPE_LV        0x10UL
#define SPMI_PMIC_GPIO_SUBTYPE_MV        0x11UL
#define SPMI_PMIC_GPIO_LV_MV_OUT_HIGH    0x80UL  // бит7=1 (уровень HIGH), источник 0

//
// VANA-линии сенсоров — ОБЫЧНЫЕ TLMM GPIO (не PMIC!), из живого device tree
// (research/sensor-gpio-regulator-findings.md): CAM_VANA0 (тыл) = GPIO 11,
// CAM_VANA2 (фронт) = GPIO 31. Оба в тайле NORTH (PINGROUP [11]/[31] NORTH
// в mainline pinctrl-sm8150.c) — тот же маппинг, что и MCLK-пины.
//
#define NABU_SENSOR_REAR_VANA_GPIO   11
#define NABU_SENSOR_FRONT_VANA_GPIO  31
#define SPMI_PMIC_GPIO_EN_MASTER_BIT     (1UL << 7)

//
// ДОБАВЛЕНО 2026-07-02 (третья сессия): регулятор cam_vio — PM8150L LDO1.
// Источник: vendor_dtb.dtb этого планшета (резолв phandle 1209 из живого
// /proc/device-tree): cam_vio-supply -> /soc/rpmh-regulator-ldoc1/
// regulator-pm8150l-l1, regulator-name="pm8150l_l1", min=max=1800000 мкВ.
// Это 1.8В I/O-рельса сенсора — без неё I2C-часть сенсора обесточена, что
// точно совпадает с наблюдаемой "тишиной" (IRQ_STATUS=0 — ни ACK, ни NACK).
// cam_vana (phandle 1217) -> pm8150l_bob (BOB), cam_vdig (phandle из dtbo)
// -> camera_wide_vdig_ldo — GPIO-регулятор на PM8150L GPIO3, которым мы уже
// управляем выше (и который по чтению уже включён: OUT_SRC=0x80=HIGH).
//
// Адресация SPMI: конвенция PMIC этого поколения (PM8998/PM8150-семейство,
// mainline qcom_spmi-regulator.c): LDOn = периферия 0x4000+(n-1)*0x100 на
// первом SID PMIC-а. PM8150L: SID=4 (подтверждён живым DT, pm8150l@4) ->
// LDO1 = PID 0x40. BOB = периферия 0xa000 -> PID 0xa0 (для BOB — ТОЛЬКО
// чтение, ошибка в предположении безопасна: увидим неожиданный TYPE и не
// будем доверять значениям).
//
// Регистры периферии-регулятора (offset внутри PID-страницы), mainline
// qcom_spmi-regulator.c: TYPE=0x04, SUBTYPE=0x05, STATUS1=0x08 (бит7 =
// VREG_READY у нового поколения), VSET_LB/UB=0x40/0x41 (вольтаж в мВ,
// 16 бит LE — поколение FTSMPS426/LDO510), MODE=0x45, EN_CTL=0x46 (бит7 =
// enable).
//
// БЕЗОПАСНОСТЬ ЗАПИСИ: пишем ТОЛЬКО EN_CTL (бит7=1) и ТОЛЬКО если:
// (а) TYPE == LDO (0x04); (б) уже запрограммированный VSET == ровно 1800 мВ
// (вольтаж выставлен загрузчиком, мы его НЕ трогаем); (в) write_ee == наш
// EE. Иначе — только чтение/диагностика.
//
// ОБНОВЛЕНО (третья сессия, по живому дампу таблицы APID->PPID): регуляторы
// PM8150L живут на SID=5 (вторая секция PMIC), НЕ на SID=4 (там GPIO/прочее).
// Подтверждено дампом: SID5/0x4000..0x4a00 (LDO1..11, APID 174..184),
// SID5/0xa000 (BOB, APID 185), SID5/0x1400..0x2b00 (SMPS S1..S8).
#define SPMI_PM8150L_REG_SID             5UL
#define SPMI_PM8150L_LDO1_PID            0x40UL  // cam_vio, 1.8V (SID=5!)
#define SPMI_PM8150L_BOB_PID             0xa0UL  // cam_vana (SID=5, только чтение!)
// S8 buck (SMPS8) — вариант cam_vdig для ДРУГОЙ ревизии платы (fixup
// fragment@92 в dtbo). Только чтение — диагностика "вдруг наша плата всё же
// использует S8". S8_CTRL = 0x1400 + 7*0x300 = 0x2900 (есть в дампе, APID 171).
#define SPMI_PM8150L_S8_CTRL_PID         0x29UL
#define SPMI_VREG_REG_TYPE               0x04UL
#define SPMI_VREG_REG_SUBTYPE            0x05UL
#define SPMI_VREG_REG_STATUS1            0x08UL
#define SPMI_VREG_REG_VSET_LB            0x40UL
#define SPMI_VREG_REG_VSET_UB            0x41UL
#define SPMI_VREG_REG_MODE               0x45UL
#define SPMI_VREG_REG_EN_CTL             0x46UL
#define SPMI_VREG_EN_BIT                 0x80UL
#define SPMI_VREG_TYPE_LDO               0x04UL
#define SPMI_VREG_EXPECTED_VIO_MV        1800UL

//
// ДОБАВЛЕНО (третья сессия, гипотеза #4): муксинг I2C-ножек CCI в TLMM.
// Downstream DT: cci0_active = gpio17/gpio18, cci1_active = gpio19/gpio20,
// function "cci_i2c". Если пины остались в func=0 (gpio), CCI-контроллер
// работает "в воздух" — полная тишина на шине при живом контроллере, ровно
// наш симптом. Тайл этих пинов достоверно неизвестен — читаем CTL из ВСЕХ
// четырёх тайлов (чистые чтения, безопасно) и разбираем на ноуте.
// Базы тайлов — mainline sm8150.dtsi: west/east/north/south.
//
#define NABU_TLMM_WEST_PHYS_ADDR   0x03100000UL
#define NABU_TLMM_SOUTH_PHYS_ADDR  0x03d00000UL
#define NABU_TLMM_TILE_PROBE_SIZE  0x16000UL   // хватает до пина 21 (0x15004)
#define NABU_CCI_I2C_FIRST_GPIO    17
#define NABU_CCI_I2C_LAST_GPIO     20
