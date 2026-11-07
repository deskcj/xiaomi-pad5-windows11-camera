# Qualcomm ISP drivers for Xiaomi Pad 5 on Windows 11

Installer 1.0.6: ISP253 / SYS080 / MFT075b.

Run `Qualcomm-ISP-Drivers-Setup.exe`. It creates **Qualcomm ISP Drivers** and a shortcut on your desktop. Open it, wait for scanning, click **Install** and approve administrator access. Requires Xiaomi Pad 5 (nabu), Windows 11 ARM64, Test Mode enabled and Secure Boot disabled.

Missing Qualcomm Camera Platform/JPEG/Flash prerequisites are repaired before ISP installation. The exact active SYS/MFT/firmware/data/configuration is checked, then rear and front cameras are tested sequentially. Missing optional native debug messages produce a warning only when frames, WinRT Stop/Close and full MFT decoding pass. Explicit native failures, lost trace events, blank output and an unclosed camera remain failures. Conflicting front/rear sensor Extension packages that redirect CameraImagesPath are exported and detached with Windows PnP tools before verification. Old disconnected native/known virtual camera nodes and unused camera-only OEM packages are then removed. Active primary drivers, Platform/JPEG/Flash, SMMU/display and unrelated packages are retained; no force deletion is used.

The PowerShell ZIP uses the same engine: extract the entire archive, run Install-QualcommISP.cmd, approve administrator access, and keep the folder until 100% / Installation complete. Choose R to restart or L to postpone; a temporary logon task resumes PowerShell after sign-in.

If a restart is required, click **Restart system**. Sign in and wait about 15 seconds; the desktop app opens and continues. Successful binding operations that request a restart are completed as a batch before restarting. Persistent unsuccessful restarts stop with a report instead of looping indefinitely. **Restart later** keeps continuation pending.

**Cameras** shows continuous preview of the selected rear/front camera. **Test (10 s)** measures frames/FPS and stream format. One camera is opened at a time.

The app has no Windows Apps entry, Start menu shortcut or Program Files installation. App logs/preferences remain in the desktop `Data` folder. Drivers remain in Windows DriverStore; permanent camera data/recovery journals are stored in `C:\Windows\System32\drivers\XiaomiPad5Camera`. Verified old data is migrated from ProgramData without deleting the old copy. After completion with no pending restart, delete the desktop folder/shortcut or run its optional `Uninstall.exe`; working drivers remain installed.

Build: Windows PowerShell 5.1, `src\Build.ps1 -SkipNative`, then `src\Build-Setup.ps1`. Omit `-SkipNative` to rebuild the ARM64 probe with EWDK. Prerequisite catalogs use the same development certificate as ISP253. `src\Sign-Prerequisites.ps1` requires its private key; private keys are not included. `tests` contains regression/safety checks. `Test-AppLifecycle.ps1` intentionally installs/removes the desktop app and is separate.

Developer: https://t.me/deskcj
Project: https://github.com/deskcj/xiaomi-pad5-windows11-camera

Complete buildable sources: [Installer 1.0.6 ZIP](../../downloads/Qualcomm-ISP-Installer-Sources-1.0.6.zip).
