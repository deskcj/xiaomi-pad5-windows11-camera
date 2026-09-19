# Xiaomi Pad 5 cameras on Windows 11 ARM64

[Русский](README.ru.md) · [Qualcomm ISP progress](QUALCOMM.md)

![Status: public preview](https://img.shields.io/badge/status-public%20preview-orange?style=for-the-badge) ![Device: Xiaomi Pad 5](https://img.shields.io/badge/device-Xiaomi%20Pad%205-blue?style=for-the-badge) ![Platform: Windows 11 ARM64](https://img.shields.io/badge/platform-Windows%2011%20ARM64-0078D4?style=for-the-badge)

From raw sensor captures to **two usable Windows cameras**: the front and rear cameras now produce real images in Windows Camera. This is an **experimental public preview**, not a finished release or an official Xiaomi driver. The next goal is image quality closer to Android. I am also working on the tablet's Qualcomm ISP; [that research has its own progress page](QUALCOMM.md).

**At a glance:** ✅ front + rear preview · ✅ photos, video, switching · ✅ real changing frames · 🔄 Android-like quality and sustained 30 FPS

**Installation requires Windows Test Mode and the included test certificate.** Use this package only on a Xiaomi Pad 5 (`nabu`) running Windows 11 ARM64.

## Download

[Download `Xiaomi-Pad-5-Camera-AV137-Community.zip` from GitHub Releases](https://github.com/deskcj/xiaomi-pad5-windows11-camera/releases/download/community-camera-preview/Xiaomi-Pad-5-Camera-AV137-Community.zip) · SHA-256: `135179ED0EE0D095EE4228825B0AB94DE6B6DA12473EB9EBAD1509D5F9DA8968`

The ZIP contains both camera drivers, the correct public certificate, an installer, and an ARM64 DevCon built from Microsoft's open-source MS-PL sample. It contains no private key or experimental Qualcomm driver.

## Install

Make a backup and keep a way to restore the original drivers. Close every camera app first.

1. Enable Windows **Test Mode**. In an administrator terminal run `bcdedit /set testsigning on`, then restart. If Secure Boot blocks it, follow [Microsoft's Test Mode guidance](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/the-testsigning-boot-configuration-option) and check your BitLocker recovery key first.
2. Extract the ZIP completely.
3. Right-click `Install-AV137.cmd` and select **Run as administrator**.
4. Wait for the green success message, then test both cameras in Windows Camera.

The installer checks that the device is Xiaomi Pad 5, installs both camera stacks, and avoids creating duplicate virtual cameras. It contains no hidden PowerShell installer: every installation command is visible in `Install-AV137.cmd`. If it requests a restart, restart once and run it again. More details are in `README-RU.md` inside the ZIP.

## Project progress

What the downloadable drivers already do:

- ✅ Windows detects separate front and rear cameras with real OV8856 and OV13B10 sensor frames.
- ✅ Preview, photos, video, and front/rear switching have worked in Windows Camera.
- ✅ Chrome and Telegram have been used with the camera path; other apps still need their own tests.
- ✅ A **short, three-second** 1280 × 960 check captured **79/79 unique front frames at 25.8 FPS** and **76/76 unique rear frames at 25.0 FPS**, with no read failures in that check.

What is still being worked on:

- 🔄 Android-like exposure, color, detail, and low-light noise reduction. Dark fabric can show colored noise; the front can look soft and the rear is softer and noisier than Android.
- 🔄 Reliable long sessions, faster starts, and a sustained **real** 30 FPS—not repeated copies of an old frame.
- 🔄 The [Qualcomm ISP path](QUALCOMM.md): recognizable rear frames and partial front results exist, but **there is no stable, installable two-camera Qualcomm release yet**.

The ZIP uses mostly software image processing. A 1280 × 960 output does **not** by itself prove full native sensor detail. This project is moving forward, but it would be misleading to call today's picture Android-quality.

## ❤️ Help bring better cameras to Windows on nabu

This is independent work on real hardware: reading the camera stack, building ARM64 drivers, testing both sensors, and investigating failures that can bring down the whole tablet. Progress takes careful experiments and repeated validation. If this project is useful to you, your support helps keep that work going toward sharper, more reliable front and rear cameras.

[![Support via DonationAlerts](https://img.shields.io/badge/Support-DonationAlerts-orange?style=for-the-badge)](https://www.donationalerts.com/r/deskcj) [![Support via PayPal](https://img.shields.io/badge/Support-PayPal-0070BA?style=for-the-badge&logo=paypal&logoColor=white)](https://paypal.me/vladcj)

You can also help **without donating**: star the repository, share it with other nabu owners, and report reproducible results with your Windows version, camera app, lighting, and whether the front or rear camera was used. That feedback helps prioritize the next fixes. Donations support the research, not a guaranteed result or delivery date. Thank you for helping an independent Windows on ARM project grow.

## Disclaimer

This is an independent community project, **not affiliated with or endorsed by Xiaomi, Qualcomm, Microsoft, or OmniVision**. Company and product names identify compatible hardware and technologies only; their owners retain their rights.

The drivers are experimental, test-signed kernel software supplied **as is**, without a warranty or promise of Android-equivalent quality, stability, or support. They may cause crashes, device problems, or data loss. Back up your system and keep a recovery method before installing. You choose whether to test them and do so at your own risk. This notice does not grant permission to redistribute third-party tools or override their licenses.
