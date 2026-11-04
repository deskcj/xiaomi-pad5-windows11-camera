# QISP253 — development continuation / Продолжение разработки

This is the project-owned code and exact working configuration of QISP253:
SYS080 + DeviceMFT075b + front243/rear253 tuning. The Qualcomm vendor driver,
CamX/IQ modules and IPE firmware original C/C++ source are unavailable.
This kit is not a full EWDK rebuild of the vendor stack. It lets another developer
inspect the working chain and parameters, rebuild the retained code additions,
and make guarded offline changes to the exact released binaries.

## Starting from the published driver

Use Python 3 and the separate QISP253 driver ZIP:

```bat
python tools/Workspace.py import "Xiaomi-Pad-5-Camera-QualcommISP253-20261104.zip" work
python tools/Workspace.py tuning work/RearData/com.qti.tuned.nabu_ofilm_ov13b10_wide.bin rear-directory.json
Build.cmd "E:\EWDK"
```

Import verifies every file against release-files.json before writing a new work
directory. It does not install drivers. Tuning export gives exact directory tags,
file offsets, sizes and bytes; it deliberately does not invent float meanings.
Use the module schema/native parser before relocating referenced structures.
`parameters/` retains the validated rear Android Metering graph, transfer hashes
and native values. The full accepted sensor/tuning files come from the driver ZIP.

`src/RearFill062.c` is the Android OV13B10 exposure callback port used in the
DeviceMFT lineage. `src/FocusFlags075b.asm` is the accepted focus flag fix.
Build.cmd compiles these with EWDK MSVC 14.44.35207, targeting ARM64; it does not
inject or sign them. Read PIPELINE.md for addresses, settings and current limits.

## Further edits

Workspace.py patch accepts an exact inputSHA256 and non-overlapping same-size
patches with offset (or RVA), beforeHex and afterHex. It verifies the old bytes
and writes a new offline file. An optional outputSHA256 checks a known result.
Changed binaries need new signing/catalogs. PE code insertion also needs correct
ABI, relocations, .pdata/.xdata and an independent unwind check; the simple patch
tool does not automate those tasks or declare a candidate safe to install.
Use the published Setup/Diagnose commands for known builds. Requalify new builds
on hardware, keep buffer ownership until closure, and preserve a known rollback.

Русский: это код наших доработок и точная рабочая конфигурация QISP253 для
продолжения разработки. Полных заводских исходников Qualcomm/CamX/IPE нет.
Здесь C-перенос экспозиции, ASM фокуса, точные параметры и описание IFE/IPE.
Команды выше импортируют проверенные файлы из отдельного архива драйвера,
экспортируют таблицы и собирают наши C/ASM через EWDK. Они не устанавливают
драйвер и не являются полной пересборкой Qualcomm. Новые бинарные изменения
требуют собственной подписи и отдельных тестов. Фото, ETL и старые эксперименты
в комплект не включены.
