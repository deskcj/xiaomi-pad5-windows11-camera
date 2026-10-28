# Qualcomm ISP cameras for Xiaomi Pad 5

[Русский](QUALCOMM.ru.md) · [Project home](README.md)

![Status](https://img.shields.io/badge/status-both%20cameras%20working-success?style=for-the-badge) ![Rear](https://img.shields.io/badge/rear-working-success?style=for-the-badge) ![Front](https://img.shields.io/badge/front-working-success?style=for-the-badge)

**Both cameras work through the hardware Qualcomm ISP on Xiaomi Pad 5 (nabu) running Windows 11 ARM64.** The tested configuration uses the IFE/IPE image-processing pipeline and provides live preview and photos with good image quality, improved color, rear autofocus and automatic brightness adjustment.

*Page updated: October 28, 2026.*

## 🎬 Video demo: both cameras working with Qualcomm ISP

Watch the demonstration of the **rear and front cameras on Xiaomi Pad 5 (nabu) running Windows 11 ARM64** using the Qualcomm ISP. The video shows a development milestone; image quality and stability are still being refined.

[![Watch Xiaomi Pad 5 (nabu) — both cameras working with Qualcomm ISP](https://img.youtube.com/vi/ue8ANiYQOg0/hqdefault.jpg)](https://www.youtube.com/watch?v=ue8ANiYQOg0)

▶️ **[Watch the demonstration on YouTube](https://www.youtube.com/watch?v=ue8ANiYQOg0)**

## ✅ What is working

| Feature | Rear camera | Front camera |
| --- | --- | --- |
| Live preview in Windows Camera | ✅ Working | ✅ Working |
| Photo capture | ✅ Working | ✅ Working |
| Full-resolution photos tested | ✅ 4160 × 3120, approximately 13 MP | ✅ 3264 × 2448, approximately 8 MP |
| Automatic brightness and white balance | ✅ Working; further tuning continues | ✅ Working; further tuning continues |
| Focus | ✅ Autofocus | ✅ Fixed focus |
| Photo and preview color matching | ✅ Improved and checked | ✅ Improved and checked |

Available photo resolutions and camera controls depend on the application. Front-camera fixed focus is a hardware characteristic.

## 📷 Better photos and preview

- ✅ Both cameras produce complete, detailed images in the tested configuration.
- ✅ Good photo and preview quality in the tested indoor and daylight scenes.
- ✅ Improved color reproduction and brightness, with Android measurements used as the reference for further refinement.
- ✅ The previously observed green cast and washed-out saved photos have been corrected in the checked comparisons.
- ✅ Saved-photo brightness and color now closely match the preview in the checked scenes.
- ✅ Full-resolution photo capture has been tested on both cameras.

## 🎯 Focus and automatic adjustments

- ✅ Rear autofocus works for nearby objects and more distant scenes.
- ✅ Automatic brightness and white-balance adjustments work on both cameras.
- ✅ The previously reported rear-camera flickering during lighting changes has been addressed and checked in user testing.
- ✅ Rear flash operation has been confirmed in a photo test.

Brightness transitions, difficult lighting and focus response remain part of ongoing refinement. Front-camera autofocus and a front LED flash are not hardware features of this tablet.

## 🔄 Further improvements

The next goal is to bring both cameras closer to Android in everyday use:

- 🔄 More consistent color and exposure across bright, dark and mixed lighting.
- 🔄 Better preservation of highlights, shadows and fine detail.
- 🔄 Faster startup, smoother brightness transitions and more responsive rear focus.
- 🔄 Further frame-rate, low-light and long-session stability testing.
- 🔄 More reliable recovery after changing between different camera drivers.

**Both cameras working is a confirmed milestone; complete Android image-quality parity and stable 30 FPS in every lighting condition are still being worked toward.**

## 📌 About this page

This page shares general Qualcomm ISP progress and confirmed camera capabilities. No Qualcomm driver download or release is provided here. The existing community camera package is documented on the [project home page](README.md).

## ❤️ Support continued development

Development involves many hours of work, real-tablet testing and comparison with Android. Support helps improve image quality, focus, speed and reliability on both cameras.

[![Support via DonationAlerts](https://img.shields.io/badge/Support-DonationAlerts-orange?style=for-the-badge)](https://www.donationalerts.com/r/deskcj) [![Support via PayPal](https://img.shields.io/badge/Support-PayPal-0070BA?style=for-the-badge&logo=paypal&logoColor=white)](https://paypal.me/vladcj)

You can also help by starring the repository, sharing the project and reporting your experience with the Windows version, application, lighting and selected camera. Donations support continued development; they do not guarantee a delivery date or a specific result.

## Disclaimer

This is an independent community project, **not affiliated with or endorsed by Xiaomi, Qualcomm, Microsoft, or OmniVision**. Company and product names are used only to identify compatible hardware and technologies; rights to those names belong to their respective owners.

The drivers are experimental, test-signed kernel software provided **“as is”**, without any guarantee of Android-level image quality, stability, or support. Crashes, device problems, and data loss are possible. Before installing, keep a backup and a way to recover your device. The decision to test and the associated risks are yours. This notice does not grant permission to redistribute third-party tools contrary to their licenses.
