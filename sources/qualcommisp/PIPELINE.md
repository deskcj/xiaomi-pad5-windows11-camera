# Working chain, settings and remaining work / Цепочка и параметры

Preview/video logical path: OV13B10 rear / OV8856 front -> CSI/CSIPHY -> IFE
capture/front-end processing -> intermediate image buffers -> IPE/CDM/ICP
processing -> output surfaces -> AVStream/DeviceMFT -> Media Foundation app.
Full-resolution photo also uses BPS IQ modules; preview IFE/IPE color tables
alone do not fix saved-photo BPS color. Rear output is 4160x3120, front 3264x2448
for the qualified photo path; advertised modes depend on the app.

## Why the earlier IPE output was zero

The earlier zero output was not proof that IPE was broken or that its reader
WIDTH=0 was the final cause. The working configuration includes the retained
SMMU stream/context mapping below. It was accepted with both hardware IPE images.
Do not substitute CPU/GPU/IFE-only bridge images as proof of IPE output.

| Item | Retained value |
| --- | --- |
| CB22 descriptor | `0\S2CB\0x01\0x04\0x16` |
| CB22 MAP bytes | `ff20800020ff21000020` |
| CB17 descriptor | `0\S2CB\0x01\0x04\0x11` |
| CB17 MAP bytes | `ff20c00000ff21400000` |
| qcsmmu8150.sys reference SHA256 | `2fa1883f3252d2e50d63911fc9ff435f88e75210f429e86686e395e0827e1299` |

These are BSP/system prerequisites, not files rebuilt by this kit. Retain the
published installer's CameraDma handling; verify the actual installed SMMU before
changing mappings. Query350c0/AT/PTE operations can change state. Do not free or
remap an active IFE/IPE buffer because a read sees zero; check request identity,
format/stride, ownership, completion, cache visibility and normal closure first.

## Exact retained code changes

Rear exposure: original Android FillExposureSettings writes 3500/3501/3502 as
three direct line-count bytes, without the generic Windows `<<4` encoding.
AG 3508/3509 unity is 0x0100; DG 350a/350b/350c unity is 0x010000.
At lines>=32760, 3822=4 and FLL is not emitted; ordinary mode uses 3822=0x14.
Group hold command ABI uses 40-byte slots. See RearFill062.c; ground truth was
Android com.qti.sensor.nabu_ov13b10.so RVA1178..1660, tested against native code.
The DeviceMFT lineage places rear callback at RVA b35000 and front Fill at b38000.
Verify the exact MFT hash and instruction block before using these addresses.

FocusFlags075b replaces MFT RVA16d91c..16d974 (0x5c bytes), falls through to
16d978, preserves non-mode bits and AUTO lock, clears stale mode/range bits.
It handles runtime mode0..5/range0..2. The ASM source is the accepted block.

SYS080 changes only one release-path BL at RVA7f5bc: 0x94004b51 -> 0x94000e65
(little-endian 514b0094 -> 650e0094). It removes the dead relay by calling the
stock release path. This change was about cadence, not colors or IPE geometry.
release-files.json identifies the exact final signed SYS/DLL/firmware files.

## Factory tuning lineage

Front223 transferred Android CC12 IFE + CC13 IPE + refPtV1 + sensorCalV1 graph171,
retaining factory LSC155 and Metering164. Rear223 retained factory color167/155/139.
Front243 adds exact Android CC13 BPS233 plus LSC34 BPS240; rear243 adds CC13 BPS233.
QISP253 changes rear Metering only: Android root87 -> Windows root109 (2792 bytes),
and seven links953..959 ->2009..2015. Full final rear SHA256:
`b2ae0cfe6f4759f4c394cba8e05c9eca4f473e9c747bed858695e8ddf81bf33c`.
Original Android rear SHA256:
`169f8f4f6063b12d134dee392c81e38cc840ca2d469776bfc2e628c9df9f0e8e`.
The native Metering reader verified all transferred values. Parameters include
region adjustment, lux/histogram luma targets, ADRC adjustment, extreme-color
luma targets and non-linear suppression; exact values/hashes are in parameters/.
Directory offsets/tags are format-specific; do not transplant raw offsets to a
different Chromatix BIN or assume every Windows/Android module layout is equal.

## What is and is not qualified

Both cameras produced hardware processed preview/photos; rear AF and photo flash
were checked in earlier tests. Android equality, all-app tap focus/HDR, sustained
30 FPS in every light, and the reported rear activation click are unresolved.
Test lighting transitions, preview/photo consistency, focus independently of
ROI/AE, unique frames/time gaps, native closure and full traces without loss.
Dark ROI metering can brighten a frame independently of focus-motor motion.

Русский: выше описана рабочая цепочка IFE/IPE, системные условия SMMU, точные
изменения экспозиции/фокуса/освобождения кадров и происхождение таблиц Android.
Все числовые настройки — из сохранённых исходников и принятых передач, без
подбора новых цветов. Полное равенство Android не достигнуто. Для продолжения
используйте эти файлы и проверенный бинарный комплект, фиксируя отдельный хеш
каждого нового варианта. Старые номера обозначают происхождение компонентов,
а не необходимость последовательно устанавливать все экспериментальные сборки.
