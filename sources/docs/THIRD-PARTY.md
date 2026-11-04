# Third-party components / Сторонние компоненты

The project does not relicense third-party components. Company/product names identify compatible hardware and technologies.

- ARM64 DevCon: built from Microsoft's [Windows Driver Samples / setup/devcon](https://github.com/microsoft/Windows-driver-samples/tree/main/setup/devcon); sample licensing is [MS-PL](https://github.com/microsoft/Windows-driver-samples/blob/main/LICENSE).
- TraceEvent and trace-reader dependencies: [Microsoft PerfView / TraceEvent](https://github.com/microsoft/perfview), with the upstream [MIT license](https://github.com/microsoft/perfview/blob/main/LICENSE.TXT).
- Windows/WDK tracing tools are Microsoft components; their applicable upstream terms remain in force.
- The hardware Qualcomm camera stack, firmware, vendor sensor data and tuning remain vendor binary components. The published development scripts are this project's retained changes and analysis tools, not original vendor source.
- Public .cer files are supplied for test-signed drivers. Private signing keys are not included.

Русский: лицензии сторонних компонентов сохраняются. DevCon основан на MS-PL примере Microsoft, TraceEvent — на upstream MIT-компонентах. Заводская часть Qualcomm остаётся бинарной; оригинальные закрытые исходники у проекта отсутствуют. В архивы включены открытые сертификаты, без закрытых ключей подписи.
