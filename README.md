# Xiaomi Pad 5 (nabu) — Windows 11 Camera Project 📷

> 🚀 Bringing native camera support to the Xiaomi Pad 5 running Windows 11 on ARM.

![GitHub stars](https://img.shields.io/github/stars/deskcj/xiaomi-pad5-windows11-camera?style=for-the-badge)
![Status](https://img.shields.io/badge/Status-In%20Development-orange?style=for-the-badge)
![Platform](https://img.shields.io/badge/Platform-Windows%2011%20ARM-blue?style=for-the-badge)
![Device](https://img.shields.io/badge/Device-Xiaomi%20Pad%205-green?style=for-the-badge)

---

# About

This project aims to bring native front and rear camera support to the **Xiaomi Pad 5 (nabu)** running **Windows 11 on ARM**.

The project focuses on reverse engineering Qualcomm's camera stack, developing custom AVStream drivers, debugging low-level memory management, and integrating the cameras into the native Windows imaging framework.

The project is currently under active development.

> **Camera drivers are not publicly available at this time.**

---

# Current Progress

## ✅ Completed

- Windows detects custom camera devices
- Front camera initialization
- Live camera preview
- Photo capture
- Video recording
- Rear camera sensor detection
- RAW frame acquisition from the rear camera
- Critical memory allocation issues resolved

---
# ❤️ Support the Project

This project is developed independently in my spare time.

Bringing full camera support to the Xiaomi Pad 5 on Windows 11 is a long-term reverse engineering effort that involves:

- Reverse engineering Qualcomm camera drivers
- Developing custom AVStream drivers
- Kernel debugging and driver testing
- Hardware validation
- Countless hours of experimentation and troubleshooting

Every new milestone requires significant time, testing, and continuous refinement.

If you enjoy this project, find it useful, or simply want to help bring full camera support to the Xiaomi Pad 5 on Windows, your support makes a real difference.

## ❤️ Support Development

[![Donate via DonationAlerts](https://img.shields.io/badge/❤️%20Donate-DonationAlerts-orange?style=for-the-badge)](https://www.donationalerts.com/r/deskcj)

### Your support helps with

- 💻 Development time
- 🔬 Hardware testing
- 🛠 Driver research and debugging
- 📱 Additional Windows on ARM devices for testing
- 🚀 Faster progress toward a public driver release

Even a small contribution helps keep the project moving forward.

If you are unable to donate, you can still help by:

- ⭐ Starring this repository
- 📢 Sharing the project with others
- 💬 Providing feedback and testing in the future

Thank you for supporting independent Windows on ARM development!
---

## 🚧 Currently Working On

The current development focus is integrating the rear camera into the Windows AVStream pipeline.

The rear sensor is already communicating correctly with the Qualcomm Spectra 380 ISP and successfully producing RAW image data.

The remaining work includes:

- RAW image conversion
- AVStream integration
- Stable frame streaming
- Driver optimization
- Rear camera live preview

---

# Project Status

| Component | Status |
|-----------|:------:|
| Device Detection | ✅ |
| Front Camera | ✅ |
| Live Preview | ✅ |
| Photo Capture | ✅ |
| Video Recording | ✅ |
| Rear Sensor | ✅ |
| RAW Frame Acquisition | ✅ |
| AVStream Integration | 🚧 |
| Rear Camera Preview | 🚧 |
| Driver Optimization | 🚧 |
| Public Release | ⏳ |

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

<img width="1124" height="338" alt="photo_2026-07-24_15-03-30" src="https://github.com/user-attachments/assets/ee59ffbb-2c68-4da4-879a-da1a139d271e" />

---

## Front Camera Preview

Live preview from the Xiaomi Pad 5 front camera using the native Windows Camera application.

<img width="1280" height="800" alt="photo_2026-07-24_15-03-31" src="https://github.com/user-attachments/assets/6ef4bfe5-6f55-4271-889b-6ee0e294fbea" />

---

## Captured Photo and video

Photo captured directly using the Windows Camera application.

<img width="640" height="480" alt="photo_2026-07-24_15-03-33" src="https://github.com/user-attachments/assets/7aeedc86-b700-45c6-9b43-2e6e9793cc16" />

https://github.com/user-attachments/assets/b9e4c36c-73e8-4a89-a402-b55aa148bedd

---

## Rear Camera Development

The rear camera sensor is successfully communicating with the Qualcomm Spectra 380 ISP.

A complete RAW frame has been captured directly from the sensor, confirming that the sensor, CSI interface, and ISP pipeline are functioning correctly.

Current development focuses on converting RAW image data into a standard image stream and integrating it into the Windows AVStream pipeline.

<img width="1280" height="796" alt="photo_2026-07-24_15-03-32" src="https://github.com/user-attachments/assets/6b7026e5-8646-4d12-b1e1-4b4f025c4f3d" />

---

# Video Demonstration

A full demonstration video will be available on YouTube.

The demonstration includes:

- Windows 11 running on Xiaomi Pad 5
- Device Manager
- Driver initialization
- Front camera live preview
- Photo capture
- Video recording
- Rear camera development
- Current project status
  
---

# Technical Highlights

- Custom AVStream driver
- Qualcomm Spectra 380 ISP integration
- Front and rear camera support
- RAW frame acquisition
- Windows Camera compatibility
- Ongoing AVStream pipeline development

---

# Driver Availability

The drivers are currently private.

They remain experimental and are intended only for internal development and testing.

A public release will be considered after the drivers become stable, reliable, and safe for everyday use.

Please do not download or redistribute unofficial builds claiming to represent this project.

---

# Disclaimer

This is an independent community project.

It is **not affiliated with or endorsed by Xiaomi, Microsoft, Qualcomm, or any other company.**

Use experimental software entirely at your own risk.

---

# Stay Updated

⭐ Star this repository to follow development progress and receive updates about future releases.

Thank you for your interest and support!
