# CPU ISP cameras for Xiaomi Pad 5 (nabu)

[![English](https://img.shields.io/badge/English-2f81f7?style=for-the-badge)](CPUISP.md) [![Русский](https://img.shields.io/badge/%D0%A0%D1%83%D1%81%D1%81%D0%BA%D0%B8%D0%B9-57606a?style=for-the-badge)](CPUISP.ru.md) [![简体中文](https://img.shields.io/badge/%E7%AE%80%E4%BD%93%E4%B8%AD%E6%96%87-57606a?style=for-the-badge)](CPUISP.zh-CN.md) [![Project home](https://img.shields.io/badge/Project%20home-57606a?style=for-the-badge&logo=github&logoColor=white)](README.md)

![Status: public preview](https://img.shields.io/badge/status-public%20preview-orange?style=for-the-badge) ![Device: Xiaomi Pad 5](https://img.shields.io/badge/device-Xiaomi%20Pad%205-blue?style=for-the-badge) ![Platform: Windows 11 ARM64](https://img.shields.io/badge/platform-Windows%2011%20ARM64-0078D4?style=for-the-badge)

**[Download verified CPUISP4 driver ZIP](https://github.com/deskcj/xiaomi-pad5-windows11-camera/raw/refs/heads/main/downloads/Xiaomi-Pad-5-Camera-CPUISP4-20261104.zip)**

Selected CPUISP4 binaries: front hardware **226.0.0.3**, rear hardware **226.2.0.10**, virtual front **226.1.0.3**, virtual rear **226.3.0.4**. The ZIP retains the original signed CPUISP4 SYS code with updated INF dates and signed catalogs and uses the common Setup/Check/Diagnose installer commands.

**[Download CPUISP4 C/H sources](https://github.com/deskcj/xiaomi-pad5-windows11-camera/raw/refs/heads/main/downloads/Xiaomi-Pad-5-Camera-CPUISP4-Sources-20261104.zip)** · [Browse code](sources/cpuisp/README.md).

CPUISP4 was selected because its C/H source snapshot and WDK projects are retained. Its four ARM64 drivers were rebuilt with EWDK using the historical compiler options; all published C/H files match the retained snapshot. The rebuilt files are not byte-identical to the original signed binaries.

Public Setup.cmd and Diagnose.cmd passed both 1280 × 960 camera image checks and normal closure on our tablet, from a Cyrillic path with spaces. No restart was required. Updated INF/catalog identity prevents the previous CPUISP3 front-package collision.

## How it works

The hardware backends configure the OV8856 front and OV13B10 rear sensors and capture their RAW Bayer data. Software performs most of the image processing on the CPU, then AVStream virtual cameras deliver the result to Windows applications. This release does not use the GPU ISP service or the Qualcomm IPE final-image path.

## What works and limitations

- ✅ Separate front and rear cameras with real sensor frames.
- ✅ Windows Camera preview, photos, video and front/rear switching have worked.
- ✅ Chrome and Telegram have been used with the CPU camera path; other apps need separate tests.
- ✅ The CPUISP4 rear DMA page-list fallback avoids the original requirement for one large contiguous allocation competing with the front camera.
- ❌ Image quality is noticeably below Android: dark fabric may show colored noise; the front can look soft, while the rear is softer and noisier. Exposure, color, detail and noise reduction need refinement.
- ❌ Sustained real 30 FPS and Android-equivalent focus/flash/HDR are not confirmed.
- 🔄 Historical CPUISP4 comparisons reported rear delivery improving from about 9.9 to 22 FPS, and a restored front detail pass with about 9% higher edge energy at 3% more noise in that comparison. Rear refinement kept its earlier budget because the heavier pass reduced delivery from 15.6 to 12.9 FPS. These are scene-specific development measurements, not guaranteed performance.

CPUISP4 restores front detail reconstruction and noise-adaptive luma refinement, improves rear frame scheduling and caps rear exposure 64 lines below the sensor period. The source ZIP retains those timing and image-processing constants in C/H. The exact CPUISP3 snapshot was not retained; CPUISP4 replaces that download so development can continue from a retained source tree.

A 1280 × 960 output does not by itself prove full native sensor detail. Both camera availability and startup depend on hardware state and application ownership.


## Install and diagnostics

Disable Secure Boot and enable **Test Mode** using [the guide](https://github.com/erdilS/Port-Windows-11-Xiaomi-Pad-5/blob/main/guide/English/disable-secureboot-en.md). Close all camera apps, extract the ZIP fully and run **`Setup.cmd` as administrator**. If a restart is requested, restart normally and run it again; wait for SUCCESS / exit code 0. Then test both cameras in Windows Camera.

For a fresh image test, close camera apps and run **`Diagnose.cmd` as administrator**. Send the newest complete folder in `C:\ProgramData\XiaomiPad5Camera\Logs`, including `result.json` and `setup.log`; review saved diagnostic frames first. `Check.cmd` checks files and platform only.


## ❤️ Help bring better cameras to Windows on nabu

This is independent work on real hardware: reading the camera stack, building ARM64 drivers, testing both sensors, and investigating failures that can bring down the whole tablet. Progress takes careful experiments and repeated validation. If this project is useful to you, your support helps keep that work going toward sharper, more reliable front and rear cameras.

[![Support via DonationAlerts](https://img.shields.io/badge/Support-DonationAlerts-orange?style=for-the-badge)](https://www.donationalerts.com/r/deskcj)

You can also help **without donating**: star the repository, share it with other nabu owners, and report reproducible results with your Windows version, camera app, lighting, and whether the front or rear camera was used. That feedback helps prioritize the next fixes. Donations support the research, not a guaranteed result or delivery date. Thank you for helping an independent Windows on ARM project grow.

## Disclaimer

This is an independent community project, **not affiliated with or endorsed by Xiaomi, Qualcomm, Microsoft, or OmniVision**. Company and product names identify compatible hardware and technologies only; their owners retain their rights.

The drivers are experimental, test-signed kernel software supplied **as is**, without a warranty or promise of Android-equivalent quality, stability, or support. They may cause crashes, device problems, or data loss. Back up your system and keep a recovery method before installing. You choose whether to test them and do so at your own risk. This notice does not grant permission to redistribute third-party tools or override their licenses.
