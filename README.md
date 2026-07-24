# Xiaomi Pad 5 (nabu) — Windows 11 Camera Project 📷

> Bringing native camera support to the Xiaomi Pad 5 running Windows 11 on ARM.

![GitHub stars](https://img.shields.io/github/stars/deskcj/xiaomi-pad5-windows11-camera?style=for-the-badge)
![Status](https://img.shields.io/badge/Status-In%20Development-orange?style=for-the-badge)
![Platform](https://img.shields.io/badge/Platform-Windows%2011%20ARM-blue?style=for-the-badge)
![Device](https://img.shields.io/badge/Device-Xiaomi%20Pad%205-green?style=for-the-badge)

---

## About

This project aims to bring native camera support to the **Xiaomi Pad 5 (nabu)** running **Windows 11 on ARM**.

It focuses on reverse engineering the Qualcomm camera stack, developing custom AVStream drivers, debugging low-level memory management, and integrating the camera into the native Windows camera framework.

The project is currently under active development.

**Camera drivers are not publicly available yet.**

---

# Current Progress

## ✅ Completed

- Windows detects custom camera devices
- Front camera initialization
- Live camera preview
- Photo capture
- Video recording
- Rear camera sensor detection
- RAW image acquisition from the rear camera
- Critical memory allocation issues resolved

## 🚧 Currently Working On

The current focus is integrating the rear camera into the Windows AVStream pipeline.

The rear sensor is already producing RAW image data and communicating correctly with the Qualcomm Spectra ISP.

The remaining work involves:

- image conversion
- AVStream integration
- stable frame streaming
- final driver optimization

---

# Development Roadmap

- ✅ Camera device detection
- ✅ Front camera support
- ✅ Live preview
- ✅ Photo capture
- ✅ Video recording
- ✅ Rear sensor initialization
- ✅ RAW frame acquisition
- 🔄 AVStream image conversion
- 🔄 Stable rear camera preview
- 🔄 Driver optimization
- 🔄 Public driver release

---

# Screenshots

## Custom Camera Devices

Windows successfully detects the custom front and rear camera devices.


<!-- INSERT DEVICE MANAGER SCREENSHOT HERE -->

---

## Front Camera Preview

Live preview from the Xiaomi Pad 5 front camera using the native Windows Camera application.

<!-- INSERT CAMERA SCREENSHOT HERE -->

---

## Captured Photo

Photo captured directly using the Windows Camera application.

<!-- INSERT CAPTURED PHOTO HERE -->

---

## Rear Camera Development

The rear camera sensor is already communicating with the Qualcomm Spectra ISP.

RAW image data has been successfully received and validated.

Current work focuses on converting RAW frames into a standard image stream through the AVStream pipeline.

<!-- INSERT RAW TEST IMAGE HERE -->

---

# Video Demonstration

A complete development demonstration will be available on YouTube.

The video will include:

- Windows 11 running on Xiaomi Pad 5
- Device Manager
- Driver initialization
- Front camera preview
- Photo capture
- Video recording
- Rear camera development
- Current progress

🎥 **YouTube video**

**(Insert your YouTube link here)**

---

# Support the Project ❤️

Developing camera support for Windows on ARM requires hundreds of hours of reverse engineering, kernel debugging, testing, and driver development.

If you enjoy this project and would like to help accelerate development, you can support it here:

## Donate

### ❤️ DonationAlerts

https://www.donationalerts.com/r/deskcj

Every donation helps fund hardware testing, development time, and future improvements.

Thank you for supporting independent Windows on ARM development!

---

# Driver Availability

The drivers are currently private.

They are still experimental and intended only for development and testing.

A public release will be considered once the drivers become stable enough for everyday use.

---

# Disclaimer

This project is an independent community effort.

It is **not affiliated with Xiaomi, Microsoft, Qualcomm, or any other company.**

Use experimental software at your own risk.

---

⭐ If you like this project, don't forget to Star the repository!
