# Release validation / Проверки релиза — 2026-11-04

The public driver archives use the accepted CPUISP3, GPU040+065+066+034 and Qualcomm253 payloads unchanged. Public **Setup.cmd, Check.cmd and Diagnose.cmd** were run on a real Xiaomi Pad 5 under Windows 11 ARM64, from fully extracted archives in paths with spaces.

| Direction / Переход | Result / Результат |
| --- | --- |
| GPU ISP → CPUISP3 | ✅ Both images + normal close / кадры обеих камер и штатное закрытие |
| CPUISP3 → Qualcomm ISP | ✅ Both images + normal close |
| Qualcomm ISP → GPU ISP | ✅ Both images + normal close |
| GPU ISP → Qualcomm ISP | ✅ Both images + normal close |
| Qualcomm ISP → CPUISP3 | ✅ Both images + normal close |
| CPUISP3 → GPU ISP | ✅ Both images + normal close |

All three profiles also passed a separate public Diagnose.cmd run. Qualcomm captures had native close verification and complete MFT trace decoding with zero lost events. CPU/GPU captures were virtual-camera tests with WinRT lifetime closure; they do not claim native Qualcomm MFT tracing. An additional final transition restored the initial GPU configuration when needed.

Fourteen regression-suite runs covered **ru-RU and en-US** runtime cultures: camera identity selection, DMA plans/rollback, creation of missing native logging parents, real isolated HKCU write/readback/restore and restart recovery. Two further warm-start suites checked 15 safety cases per locale. The command wrapper also passed an isolated Cyrillic path/space fixture. All shipped PowerShell scripts were parsed in Windows PowerShell 5.1; manifests and ZIP round trips were checked.

The published common source layout also passed eight regression suites under each culture (16 runs). It includes only reduced numeric result/closure fixtures and a synthetic missing-key state; personal logs were not published. These source tests do not open cameras or install drivers.

No automatic restart, camera-owner termination or weakened buffer-lifetime/SMMU gate was used. A preliminary controller used an obsolete saved boot baseline from before the user's Android visit; the final matrix uses the actual kernel baseline, and the original failed controller receipt was retained. This was not a driver crash during the matrix.

A preliminary CPU→Qualcomm transition exposed a first front-camera opening that returned no frames. An unchanged-driver diagnosis then succeeded. The release now allows exactly one native image-test retry during installation, only after proven WinRT/native retirement and complete loss-free tracing, with no API error, no control changes and no pending/unsafe owner. It does not accept green/empty-pixel frames, rebind/reset devices or retry a second failure. The first failure is retained under verificationRetries and separate logs. Diagnosis and CPU/GPU retain first-failure behavior.

**Limits:** one real tablet plus locale fixtures do not prove installation on every Windows image, localization, BSP or prior modified driver state. First-time startup preparation may require a normal user restart. Busy/unknown/unsafe states are rejected. These functional installation/preview tests do not requalify historical full-resolution photo, flash, optical autofocus or long-duration FPS results. Android-equivalent image quality is not claimed.

---

Русский: проверены настоящие публичные команды установки/проверки/диагностики и все шесть направлений перехода между CPU, GPU и Qualcomm на одном планшете. Обе камеры выдавали реальные кадры и штатно закрывались. Для Qualcomm дополнительно проверены native-close и полный MFT trace без потерь; CPU/GPU проверялись как виртуальные камеры через WinRT.

В 14 запусках регрессионных наборов проверены локали ru-RU/en-US, создание отсутствующих родительских ключей журналирования, транзакции/откат, выбор камер по аппаратному идентификатору и необходимость перезагрузки. Ещё два набора проверили 15 условий безопасного повтора на каждой локали. Это не две отдельные чистые локализованные установки Windows. Дополнительно проверена оболочка CMD в кириллическом пути с пробелами. Автоперезагрузок и принудительного завершения владельцев камеры не было.

Предварительный переход CPU→Qualcomm выявил первый запуск фронтальной без кадров; новая диагностика без изменения драйвера прошла. В релизе разрешён ровно один повтор проверки native-камеры при установке, только после подтверждённого WinRT/native закрытия и полного trace без потерь, без API-ошибок, изменения controls и живых/небезопасных owners. Первые логи сохраняются; второй отказ окончательный. Пустые/зелёные пиксели не принимаются как успех, reset/rebind не выполняется. В диагностике и CPU/GPU такого повтора нет.

Проверки установки и кадров не заменяют отдельные тесты оптического фокуса, вспышки, полноразмерных фото и длительного FPS; исторические результаты отдельно указаны на страницах веток. Работа на всех сторонних образах/драйверах и качество Android не гарантируются.

[Machine-readable result / Результаты JSON](release-validation.json) · [English project home](../../README.md) · [Главная на русском](../../README.ru.md)
