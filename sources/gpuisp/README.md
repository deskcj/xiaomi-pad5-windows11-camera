# GPU ISP sources

The released stack combines GPU040 user-mode D3D12 processing, Rear065 hardware,
Front066 hardware and Virtual034 AVStream drivers. The snapshots are named separately
so the newer physical driver is not confused with the GPU processing version.
GPU040 includes compute-shader source and its generated shader headers.
The signed release binaries are supplied in the driver ZIP; these snapshots are development sources.
Use Windows ARM64, the Windows Driver Kit/EWDK and MSVC to rebuild; paths in historical
build projects/scripts may need adjusting. Use your own test-signing certificate.

Русский: исходники выбранной связки GPU040 + задняя065 + фронтальная066 + virtual034.
Каталоги явно разделены по версии. Для сборки нужны WDK/EWDK и MSVC ARM64;
исторические пути нужно адаптировать, для подписи используйте свой сертификат.
