# Xiaomi Pad 5 cameras on Windows 11 ARM64

[English](docs/README.md) · [Русский](docs/README.ru.md) · [简体中文](docs/README.zh-CN.md)

**[Download EXE — Qualcomm ISP 253](https://github.com/deskcj/xiaomi-pad5-windows11-camera/raw/refs/heads/main/downloads/Qualcomm-ISP-Drivers-Setup.exe)**

Qualcomm ISP drivers for Xiaomi Pad 5 on Windows 11, with device scanning, installation repair, verification and live camera preview. Requires **Xiaomi Pad 5 / nabu, Windows 11 ARM64, Test Mode and Secure Boot disabled**.

1. Run `Qualcomm-ISP-Drivers-Setup.exe`. It creates a desktop folder and shortcut.
2. After the device scan, click **Install** and approve administrator elevation.
3. When **Restart required** appears, click **Restart system**. Sign in after restarting and wait a little (normally 15–30 seconds): **the app opens automatically and continues**. Repeat if another restart is requested; keep the desktop folder until completion.
4. Wait for **Installation complete / 100%**, then open **Cameras** and select **Rear / Front** to check live images.

For detailed installation and troubleshooting, open the [English](docs/QUALCOMM/README.md), [Russian](docs/QUALCOMM/README.ru.md) or [Chinese](docs/QUALCOMM/README.zh-CN.md) Qualcomm ISP page. Logs stay in the desktop app folder under `Data\Logs`. If automatic launch does not occur, open the same desktop shortcut and click Install.

Other processing branches: [GPU ISP](docs/GPUISP/README.md) · [CPU ISP](docs/CPUISP/README.md). These retain their own ZIP installers.

[Contact developer](https://t.me/deskcj) · [Project status and release scope](docs/README.md)