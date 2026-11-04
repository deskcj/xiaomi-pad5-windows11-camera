# Xiaomi Pad 5 (nabu) cameras on Windows 11 ARM64

[![English](https://img.shields.io/badge/English-2f81f7?style=for-the-badge)](README.md) [![Русский](https://img.shields.io/badge/%D0%A0%D1%83%D1%81%D1%81%D0%BA%D0%B8%D0%B9-57606a?style=for-the-badge)](README.ru.md)

![Status: public preview](https://img.shields.io/badge/status-public%20preview-orange?style=for-the-badge) ![Device: Xiaomi Pad 5](https://img.shields.io/badge/device-Xiaomi%20Pad%205-blue?style=for-the-badge) ![Platform: Windows 11 ARM64](https://img.shields.io/badge/platform-Windows%2011%20ARM64-0078D4?style=for-the-badge)

> [!WARNING]
> Use these packages only on Xiaomi Pad 5 (`nabu`) running Windows 11 ARM64.

One camera project, three processing branches. Both front and rear cameras produce real sensor images. Choose a branch below; only one is active at a time. Full Android image-quality parity remains a development goal.

## Download

| Branch page | Driver ZIP |
| --- | --- |
| **[Qualcomm ISP](QUALCOMM.md)** — hardware IFE/IPE, reference 253 | **[Download ZIP](https://github.com/deskcj/xiaomi-pad5-windows11-camera/raw/refs/heads/main/sources/packages/Xiaomi-Pad-5-Camera-QualcommISP253-20261104.zip)** |
| **[GPU ISP](GPUISP.md)** — D3D12 compute, GPU040 + rear065/front066 | **[Download ZIP](https://github.com/deskcj/xiaomi-pad5-windows11-camera/raw/refs/heads/main/sources/packages/Xiaomi-Pad-5-Camera-GPUISP040-20261104.zip)** |
| **[CPU ISP](CPUISP.md)** — software processing, verified CPUISP3 | **[Download ZIP](https://github.com/deskcj/xiaomi-pad5-windows11-camera/raw/refs/heads/main/sources/packages/Xiaomi-Pad-5-Camera-CPUISP3-20261104.zip)** |

Each branch page includes its source-code links, results and limitations. Each driver ZIP includes the shared installer, diagnostics, signed payload and available development sources. CPUISP3's exact C/C++ sources were not preserved; its source reference is explicitly labelled CPUISP4. Qualcomm's original vendor source is closed.

## Install and diagnose

1. Disable Secure Boot and enable Windows **Test Mode** using [this guide](https://github.com/erdilS/Port-Windows-11-Xiaomi-Pad-5/blob/main/guide/English/disable-secureboot-en.md).
2. Close every camera preview/recording app and fully extract the ZIP.
3. Run **`Setup.cmd` as administrator**. The command is the same for all three branches.
4. If a restart is explicitly requested, restart normally and run `Setup.cmd` again. Wait for **SUCCESS and exit code 0**, then test both cameras in Windows Camera.
5. For troubleshooting, close camera apps, run **`Diagnose.cmd` as administrator**, and share the newest complete folder from `C:\ProgramData\XiaomiPad5Camera\Logs`. Review diagnostic images before sharing.

`Check.cmd` checks archive integrity and platform; installation and diagnosis additionally check actual camera frames.

## Current branch status

### Qualcomm ISP — reference 253 · [Branch page](QUALCOMM.md)

- ✅ Both cameras use hardware IFE/IPE and provide preview and photos.
- ✅ Full-resolution photos tested: rear 4160 × 3120, front 3264 × 2448.
- ✅ Rear autofocus, automatic brightness/white balance and rear photo flash have been checked in documented tests.
- ✅ Video1080p reached about 30 unique FPS in earlier controlled tests; this does not guarantee preview FPS in low light.
- 🔄 Mixed lighting, focus transitions, startup/switching and the reported rear activation click still need refinement. Android parity and every Windows image/app are not confirmed.

### GPU ISP — 040 + rear065/front066 + virtual034 · [Branch page](GPUISP.md)

- ✅ Both cameras supply RAW to D3D12 compute processing and expose normal virtual camera interfaces.
- ✅ Earlier clean 20-second tests measured about 28–29 unique FPS without repeated frames.
- ✅ GPU040 color/LSC/gamma tables and rear065 focus logic are retained; front066 fixes the documented RAW transport samples.
- ❌ Android-quality parity, reliable tap-to-focus in every app, rear flash and HDR are not confirmed for this GPU release.
- 🔄 Two background workers and a GPU service are required; color, exposure, optical rear focus and long-session behavior still need work.

### CPU ISP — CPUISP3 · [Branch page](CPUISP.md)

- ✅ Separate front and rear cameras use real OV8856/OV13B10 frames; Windows Camera preview, photos, video and switching have worked.
- ✅ The CPU camera path has been used in Chrome and Telegram; other apps need their own tests.
- ✅ The selected ZIP uses the verified CPUISP3 binaries, rather than AV137.
- ❌ Image quality remains visibly below Android: color noise, softness, exposure and detail need improvement.
- 🔄 Sustained real 30 FPS, Android-like focus/flash/HDR and all-app compatibility are not established.

## ❤️ Help bring better cameras to Windows on nabu

This is independent work on real hardware: reading the camera stack, building ARM64 drivers, testing both sensors, and investigating failures that can bring down the whole tablet. Progress takes careful experiments and repeated validation. If this project is useful to you, your support helps keep that work going toward sharper, more reliable front and rear cameras.

[![Support via DonationAlerts](https://img.shields.io/badge/Support-DonationAlerts-orange?style=for-the-badge)](https://www.donationalerts.com/r/deskcj)

You can also help **without donating**: star the repository, share it with other nabu owners, and report reproducible results with your Windows version, camera app, lighting, and whether the front or rear camera was used. That feedback helps prioritize the next fixes. Donations support the research, not a guaranteed result or delivery date. Thank you for helping an independent Windows on ARM project grow.

## Disclaimer

This is an independent community project, **not affiliated with or endorsed by Xiaomi, Qualcomm, Microsoft, or OmniVision**. Company and product names identify compatible hardware and technologies only; their owners retain their rights.

The drivers are experimental, test-signed kernel software supplied **as is**, without a warranty or promise of Android-equivalent quality, stability, or support. They may cause crashes, device problems, or data loss. Back up your system and keep a recovery method before installing. You choose whether to test them and do so at your own risk. This notice does not grant permission to redistribute third-party tools or override their licenses.
