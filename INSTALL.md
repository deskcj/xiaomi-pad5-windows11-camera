# Install and diagnose all three camera branches

[Русский](INSTALL.ru.md)

## Install and diagnostics

Disable Secure Boot and enable **Test Mode** using [the guide](https://github.com/erdilS/Port-Windows-11-Xiaomi-Pad-5/blob/main/guide/English/disable-secureboot-en.md). Close all camera apps, extract the ZIP fully and run **`Setup.cmd` as administrator**. If a restart is requested, restart normally and run it again; wait for SUCCESS / exit code 0. Then test both cameras in Windows Camera.

For a fresh image test, close camera apps and run **`Diagnose.cmd` as administrator**. Send the newest complete folder in `C:\ProgramData\XiaomiPad5Camera\Logs`, including `result.json` and `setup.log`; review saved diagnostic frames first. `Check.cmd` checks files and platform only. [Project home](README.md).

## Changing branches and safe failures

Only one branch is active. Close camera apps and run the chosen ZIP's Setup.cmd; keep the ZIP fully extracted. The installer uses hardware identities and generated INF versions, not localized device labels. It disables the alternative camera path, binds the chosen signed drivers, and accepts success only after both images and normal closure pass.

If Windows requires first-time DMA/logging startup preparation, one normal restart is required, then rerun the same Setup.cmd. It does not restart automatically or bypass buffer-lifetime gates. If a camera is held by an app, close its preview and retry. Unknown driver/BSP states are rejected rather than assumed compatible.

GPU activation requires 900 MiB available RAM; CPU/Qualcomm profiles require 650 MiB. Installation requires at least 1 GiB free on C:. Backup-CurrentDrivers.cmd exports current driver packages; it is not an automatic full-system rollback. Keep a system backup and a recovery method.

The packages have been checked against known CPU/GPU/Qualcomm configurations. One tablet and locale regression tests cannot guarantee installation on every third-party Windows image. See [release validation](VALIDATION.md).

During installation, a native Qualcomm opening that returns zero frames can be tested once more only after normal stream closure and complete loss-free tracing are confirmed. The first logs are retained. A second failure, API error or green/empty image remains a failure; no device reset or automatic restart is performed.
