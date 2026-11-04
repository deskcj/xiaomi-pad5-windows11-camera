# GPU ISP 040 — driver source / Исходники драйвера

Sources for the published GPU040 + rear065 + front066 + virtual034 stack.
This directory contains only the code, projects, INF files, processing profiles
and build command needed for the camera stack. No research logs or installer tests.

## Build with EWDK

Use EWDK with MSVC **14.44.35207** and Windows SDK/WDK **10.0.26100.0**.
Run from Command Prompt, replacing the path with your EWDK location:

```bat
Build.cmd "E:\EWDK"
```

The command supports ARM64 and x64 build hosts, always targeting Windows ARM64.
It compiles four kernel drivers, compiles all eight compute-shader entry points
from `user/gpuisp/shaders/IspCs.hlsl`, and builds `GpuCameraPump.exe` and
`GpuIspHost.exe`. Generated shader headers and object files are written locally;
they are not substitutes for the HLSL source.

Outputs are under `out/FrontHardware`, `out/RearHardware`, `out/FrontVirtual`,
`out/RearVirtual` and `out/Runtime`. `kernel/` holds C/H and WDK projects;
`user/` holds C++/HLSL; `profiles/` holds the published processing parameters.

The rebuilt `.text` sections of all four SYS and both EXE files were compared
with the published signed binaries and matched byte for byte. Entire-file hashes
differ because of signing and build metadata, source paths and compiler RTTI names.
This verifies the compiled code; it is not a new image-quality or stability test.

To install the published, tested binaries, download the separate driver ZIP and
run its `Setup.cmd` as administrator. Rebuilt drivers are unsigned: use your own
test certificate and regenerate/sign their catalogs before installing them.
EWDK, vendor tool binaries and private signing keys are not included here.

## Сборка

Это исходники опубликованной связки GPU040 + rear065 + front066 + virtual034:
C/H, C++/HLSL, проекты WDK, INF, профили и `Build.cmd`.
Нужны EWDK, MSVC **14.44.35207** и SDK/WDK **10.0.26100.0**.
Запустите команду выше, указав свою папку EWDK. Результат — четыре ARM64 SYS
и два ARM64 EXE в `out/`; восемь шейдеров собираются из HLSL.

Побайтово проверено совпадение секций машинного кода `.text` всех шести файлов
с опубликованными драйверами. Подпись, метаданные и имена путей/RTTI отличаются.
Для установки готовой проверенной сборки используйте отдельный ZIP драйвера.
Собранные самостоятельно драйверы требуют собственной тестовой подписи
и пересоздания/подписи каталогов. EWDK и закрытые ключи в архив не входят.

`user/bench/src/GpuIspBench.cpp` is reused by GpuCameraPump.cpp as part of the production worker implementation. Its historical filename does not mean it is an optional test file.
