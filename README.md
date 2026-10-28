# Xiaomi Pad 5 cameras on Windows 11 ARM64

[Русский](README.ru.md) · [Qualcomm ISP progress](QUALCOMM.md)

![Status: public preview](https://img.shields.io/badge/status-public%20preview-orange?style=for-the-badge) ![Device: Xiaomi Pad 5](https://img.shields.io/badge/device-Xiaomi%20Pad%205-blue?style=for-the-badge) ![Platform: Windows 11 ARM64](https://img.shields.io/badge/platform-Windows%2011%20ARM64-0078D4?style=for-the-badge)

The front and rear cameras of Xiaomi Pad 5 (`nabu`) now produce real images on Windows 11 ARM64. The project has two distinct paths:

- **The ZIPs available below use CPU ISP (software image processing).** Both cameras work for photos and video, but image quality is still noticeably below Android.
- **A separate test configuration uses the hardware Qualcomm ISP.** Both cameras now produce good full-resolution photos and high-quality previews in tested scenes. This is a significant step toward Android-level quality, but the [Qualcomm build](QUALCOMM.md) has not yet been published for installation.

This is an experimental community project, not a finished release or an official Xiaomi driver.

> [!WARNING]
> Use this package only on a Xiaomi Pad 5 (`nabu`) running Windows 11 ARM64.

**Installation requires enabled Test Mode**

## Download

[Xiaomi-Pad-5-Camera-AV137-Community-FIXED.zip](https://github.com/user-attachments/files/32478379/Xiaomi-Pad-5-Camera-AV137-Community-FIXED.zip)

If Front Camera is not working try zip below
[Xiaomi-Pad-5-Camera-CPUISP3.zip](https://github.com/user-attachments/files/32521203/Xiaomi-Pad-5-Camera-CPUISP3.zip)

The ZIP contains both camera drivers, the correct public certificate, an installer, and an ARM64 DevCon built from Microsoft's open-source MS-PL sample. It contains no private key or experimental Qualcomm driver.

## Install

> [!NOTE]
> Close every camera app first.

1. Disable Secure Boot and enable Windows **Test Mode** on your Xiaomi Pad 5 by following [this guide](https://github.com/erdilS/Port-Windows-11-Xiaomi-Pad-5/blob/main/guide/English/disable-secureboot-en.md).
2. Extract the ZIP completely.
3. Right-click `Install-AV137.cmd` and select **Run as administrator**.
4. Wait for the green success message, then test both cameras in Windows Camera.

> The installer checks that the device is Xiaomi Pad 5, installs both camera drivers, and avoids creating duplicate virtual cameras. It contains no hidden PowerShell installer: every installation command is visible in `Install-AV137.cmd`. If it requests a restart, restart once and run it again. More details are in `README-RU.md` inside the ZIP.

## What works and what does not yet

### Public ZIPs: CPU ISP

- ✅ Windows detects separate front and rear cameras with real OV8856 and OV13B10 sensor frames.
- ✅ Preview, photos, video, and front/rear switching have worked in Windows Camera.
- ✅ Chrome and Telegram have been used with the camera path; other apps still need their own tests.
- ✅ A **short, three-second** 1280 × 960 check captured **79/79 unique front frames at 25.8 FPS** and **76/76 unique rear frames at 25.0 FPS**, with no read failures in that check.

### Separate test configuration: Qualcomm ISP

- ✅ **Both cameras work through the hardware Qualcomm ISP** and produce good photos and previews in tested scenes; full-resolution photos have been tested.
- ✅ Rear autofocus works, and both cameras adjust brightness and white balance automatically. [Read more about Qualcomm ISP progress](QUALCOMM.md).
- ❌ **There is no ready-to-install public Qualcomm package yet.** The ZIPs above use CPU ISP and do not include the experimental Qualcomm driver.

### What is still being worked on

- ❌ **CPU ISP does not yet match Android image quality:** dark fabric may show colored noise; the front can look soft, while the rear is softer and noisier than Android. Exposure, color, detail, and noise reduction need further tuning.
- ❌ **Sustained real 30 FPS has not yet been confirmed in long sessions.** Work continues on freezes and startup speed; the short test above does not establish sustained frame rate or rule out repeated old frames.
- 🔄 Qualcomm ISP stability, difficult lighting, and frame rate need more testing. Full Android image-quality parity is not claimed yet.

The published ZIPs use mostly software image processing. A 1280 × 960 output does **not** by itself prove full native sensor detail. The Qualcomm ISP improvements belong to a separate test configuration, not these ZIPs.

## ❤️ Help bring better cameras to Windows on nabu

This is independent work on real hardware: reading the camera stack, building ARM64 drivers, testing both sensors, and investigating failures that can bring down the whole tablet. Progress takes careful experiments and repeated validation. If this project is useful to you, your support helps keep that work going toward sharper, more reliable front and rear cameras.

[![Support via DonationAlerts](https://img.shields.io/badge/Support-DonationAlerts-orange?style=for-the-badge)](https://www.donationalerts.com/r/deskcj) [![Support via PayPal](https://img.shields.io/badge/Support-PayPal-0070BA?style=for-the-badge&logo=paypal&logoColor=white)](https://paypal.me/vladcj)

You can also help **without donating**: star the repository, share it with other nabu owners, and report reproducible results with your Windows version, camera app, lighting, and whether the front or rear camera was used. That feedback helps prioritize the next fixes. Donations support the research, not a guaranteed result or delivery date. Thank you for helping an independent Windows on ARM project grow.

## Disclaimer

This is an independent community project, **not affiliated with or endorsed by Xiaomi, Qualcomm, Microsoft, or OmniVision**. Company and product names identify compatible hardware and technologies only; their owners retain their rights.

The drivers are experimental, test-signed kernel software supplied **as is**, without a warranty or promise of Android-equivalent quality, stability, or support. They may cause crashes, device problems, or data loss. Back up your system and keep a recovery method before installing. You choose whether to test them and do so at your own risk. This notice does not grant permission to redistribute third-party tools or override their licenses.
