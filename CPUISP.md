# CPU ISP cameras for Xiaomi Pad 5 (nabu)

[Русский](CPUISP.ru.md) · [Project home](README.md)

![Status: public preview](https://img.shields.io/badge/status-public%20preview-orange?style=for-the-badge)

![Device: Xiaomi Pad 5](https://img.shields.io/badge/device-Xiaomi%20Pad%205-blue?style=for-the-badge) ![Platform: Windows 11 ARM64](https://img.shields.io/badge/platform-Windows%2011%20ARM64-0078D4?style=for-the-badge)

**[Download verified CPUISP3 driver ZIP](https://github.com/deskcj/xiaomi-pad5-windows11-camera/releases/download/camera-public-preview-20261104/Xiaomi-Pad-5-Camera-CPUISP3-20261104.zip)** · **[Download available CPU source reference](https://github.com/deskcj/xiaomi-pad5-windows11-camera/releases/download/camera-public-preview-20261104/Xiaomi-Pad-5-Camera-CPUISP3-Sources-20261104.zip)** · [Browse sources](sources/cpuisp/README.md).

The selected binaries are CPUISP3: front hardware **226.0.0.3**, rear hardware **226.2.0.5**, front virtual camera **226.1.0.1**, rear virtual camera **226.3.0.1**. The driver bytes match the [previously published CPUISP3 package](https://github.com/user-attachments/files/32521203/Xiaomi-Pad-5-Camera-CPUISP3.zip); the shared installer and diagnostics use the same commands as the other branches.

## How it works

The hardware backends configure the OV8856 front and OV13B10 rear sensors and capture their RAW Bayer data. Software performs most of the image processing on the CPU, then AVStream virtual cameras deliver the result to Windows applications. This release does not use the GPU ISP service or the Qualcomm IPE final-image path.

## What works and limitations

- ✅ Separate front and rear cameras with real sensor frames.
- ✅ Windows Camera preview, photos, video and front/rear switching have worked.
- ✅ Chrome and Telegram have been used with the CPU camera path; other apps need separate tests.
- ✅ The CPUISP3 rear DMA page-list fallback avoids the original requirement for one large contiguous allocation competing with the front camera.
- ❌ Image quality is noticeably below Android: dark fabric may show colored noise; the front can look soft, while the rear is softer and noisier. Exposure, color, detail and noise reduction need refinement.
- ❌ Sustained real 30 FPS and Android-equivalent focus/flash/HDR are not confirmed.
- 🔄 An earlier **three-second CPU ISP** test at 1280 × 960 captured front **79/79 unique frames at 25.8 FPS** and rear **76/76 at 25.0 FPS** with no read failures. These are historical CPU-branch results from the former home page, not a new sustained CPUISP3 benchmark. The original CPUISP3 notes describe roughly 20 FPS front preview and 2–3-second startup.

A 1280 × 960 output does not by itself prove full native sensor detail. Both camera availability and startup depend on hardware state and application ownership.

## Source availability

The **exact CPUISP3 C/C++ snapshot was not preserved** during the original development. The source link contains the surviving **CPUISP4-reference** tree, clearly identified as a later version, plus the shared installer/probe source. It does not claim to rebuild the CPUISP3 binaries byte for byte.

## Install and diagnostics

Disable Secure Boot and enable **Test Mode** using [the guide](https://github.com/erdilS/Port-Windows-11-Xiaomi-Pad-5/blob/main/guide/English/disable-secureboot-en.md). Close all camera apps, extract the ZIP fully and run **`Setup.cmd` as administrator**. If a restart is requested, restart normally and run it again; wait for SUCCESS / exit code 0. Then test both cameras in Windows Camera.

For a fresh image test, close camera apps and run **`Diagnose.cmd` as administrator**. Send the newest complete folder in `C:\ProgramData\XiaomiPad5Camera\Logs`, including `result.json` and `setup.log`; review saved diagnostic frames first. `Check.cmd` checks files and platform only. [Full common instructions](INSTALL.md).


## ❤️ Help bring better cameras to Windows on nabu

This is independent work on real hardware: reading the camera stack, building ARM64 drivers, testing both sensors, and investigating failures that can bring down the whole tablet. Progress takes careful experiments and repeated validation. If this project is useful to you, your support helps keep that work going toward sharper, more reliable front and rear cameras.

[![Support via DonationAlerts](https://img.shields.io/badge/Support-DonationAlerts-orange?style=for-the-badge)](https://www.donationalerts.com/r/deskcj) [![Support via PayPal](https://img.shields.io/badge/Support-PayPal-0070BA?style=for-the-badge&logo=paypal&logoColor=white)](https://paypal.me/vladcj)

You can also help **without donating**: star the repository, share it with other nabu owners, and report reproducible results with your Windows version, camera app, lighting, and whether the front or rear camera was used. That feedback helps prioritize the next fixes. Donations support the research, not a guaranteed result or delivery date. Thank you for helping an independent Windows on ARM project grow.

## Disclaimer

This is an independent community project, **not affiliated with or endorsed by Xiaomi, Qualcomm, Microsoft, or OmniVision**. Company and product names identify compatible hardware and technologies only; their owners retain their rights.

The drivers are experimental, test-signed kernel software supplied **as is**, without a warranty or promise of Android-equivalent quality, stability, or support. They may cause crashes, device problems, or data loss. Back up your system and keep a recovery method before installing. You choose whether to test them and do so at your own risk. This notice does not grant permission to redistribute third-party tools or override their licenses.
