# Camera development / Разработка камер

| Branch | Included material |
| --- | --- |
| [GPU ISP](gpuisp/README.md) | GPU040 + rear065/front066 + virtual034 C/H, C++/HLSL, profiles, WDK projects and checked EWDK build |
| [CPU ISP](cpuisp/README.md) | CPUISP4 retained C/H, WDK projects and checked EWDK build |
| [Qualcomm ISP](qualcommisp/README.md) | Project C/ASM modifications, exact QISP253 tuning, IFE/IPE chain and offline workspace tools; original vendor C/C++ unavailable |

Prepared driver ZIPs and separate development ZIPs are in [downloads](../downloads).
The driver archives contain the tested installer and signed payload; source archives
contain only development material, without the old research runs and installer suites.

Русский: GPU и CPU — C/H-проекты, собираемые через EWDK; GPU также содержит C++/HLSL.
Qualcomm — исходники наших доработок, точные параметры и описание аппаратной цепочки.
Его заводской закрытый стек полностью пересобрать из исходников невозможно.
Готовые драйверы скачиваются отдельно; старые эксперименты в исходники не включены.
