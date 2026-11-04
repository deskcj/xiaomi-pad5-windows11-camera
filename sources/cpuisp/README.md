# CPUISP4 — matching driver sources / Исходники драйвера

CPUISP4 replaces the former CPUISP3 download because its C/H source snapshot is
retained. It descends from the AV137 CPU branch; it is not the unchanged AV137
or CPUISP3 release. Hardware front 226.0.0.3, rear 226.2.0.10; virtual front
226.1.0.3, rear 226.3.0.4.

Run `Build.cmd "E:\EWDK"` from Command Prompt with EWDK MSVC 14.44.35207 and
Windows SDK/WDK 10.0.26100.0. ARM64/x64 hosts build four ARM64 SYS files under
`out/`, with the matching INF files. Sign the rebuilt drivers and regenerate/sign
catalogs with your own certificate. Use the separate driver ZIP's Setup.cmd to
install the published signed binaries. EWDK and private signing keys are excluded.

## Pipeline and parameters

OV8856/OV13B10 -> CSI/CCI/CSIPHY -> hardware RAW capture -> BackendBridge IOCTL
-> virtual AVStream CPU Bayer/NV12 processing -> Windows camera applications.
The physical driver and virtual driver are separate projects. Exposure/gain,
RAW layout, color matrices, shading, noise/detail passes and timing constants
are retained in the C/H files, including the inline processing helpers.
No Qualcomm IPE or D3D12 GPU service is used in this branch.

Read NabuCamHwFront.c for front capture and sensor writes; NabuCamHwRear.c and
the neighboring NabuCamAvStreamRear/HwCapture.c for rear transport. BackendBridge*
connects capture to the virtual cameras. The virtual C/H files hold the image
processing and frame delivery logic. Keep relative include paths intact.

Русский: сохранённые C/H, проекты WDK и INF именно CPUISP4. Эта версия выросла
из CPU-ветки AV137; она отличается от AV137 и CPUISP3. Команда выше собирает
четыре ARM64 SYS. Параметры обработки, экспозиции и буферов — в C/H; цепочка
захвата и обработки указана выше. Для установки готовых подписанных файлов
скачайте отдельный архив драйвера. Для собственной сборки нужна своя подпись.

Build verification: all retained C/H bytes match the CPUISP4 snapshot, and all four ARM64 SYS rebuild successfully with the historical WDK compiler/link options. Relinking was not byte-identical to the old signed files; the driver ZIP uses the original signed CPUISP4 binaries.

Public packaging retains the original signed SYS code and updates the INF date to 11/04/2026 with new catalogs. This prevents a same-INF collision with CPUISP3; matching INF metadata is included in this source tree. Setup and Diagnose both passed real-tablet front/rear image and normal-close checks.
