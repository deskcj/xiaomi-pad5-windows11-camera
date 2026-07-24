<p align="center">
  <b>🌐 Language:</b>
  🇬🇧 <a href="README.md">English</a> |
  🇷🇺 <a href="README.ru.md">Русский</a>
</p>

---

# Xiaomi Pad 5 (nabu) — Windows 11 Camera Project 📷

> Developing native camera support for the Xiaomi Pad 5 running Windows 11 on ARM.

![GitHub Stars](https://img.shields.io/github/stars/deskcj/xiaomi-pad5-windows11-camera?style=for-the-badge)
![Status](https://img.shields.io/badge/Status-In%20Development-orange?style=for-the-badge)
![Platform](https://img.shields.io/badge/Platform-Windows%2011%20ARM-blue?style=for-the-badge)
![Device](https://img.shields.io/badge/Device-Xiaomi%20Pad%205-green?style=for-the-badge)

---

# About the Project

The goal of this project is to implement native front and rear camera support for the **Xiaomi Pad 5 (nabu)** running **Windows 11 on ARM**.

The development process includes reverse engineering Qualcomm's camera stack, creating custom AVStream drivers, debugging low-level memory management, and integrating the cameras into the native Windows multimedia framework.

The project is currently under active development.

> **The camera drivers are not yet available for public download.**

---

# Current Progress

## ✅ Implemented

- Windows detects the custom camera devices
- Front camera initialization
- Real-time camera preview
- Photo capture
- Video recording
- Rear camera sensor detection
- RAW frame acquisition from the rear camera
- Critical memory allocation issues resolved

---

# ❤️ Support the Project

This project is developed independently in my spare time.

Bringing full camera support to the Xiaomi Pad 5 on Windows 11 is a long-term engineering effort that involves:

- reverse engineering Qualcomm camera drivers;
- developing custom AVStream drivers;
- kernel-level Windows driver debugging;
- camera and hardware testing;
- analyzing sensor, CSI, and ISP operation;
- extensive experimentation and troubleshooting.

Every new milestone requires a significant amount of development time, testing, and refinement.

If you are interested in this project and would like to help bring fully working cameras and a future public driver release closer to completion, your support genuinely makes a difference.

## ❤️ Support Development via DonationAlerts

[![Support via DonationAlerts](https://img.shields.io/badge/❤️%20Support-DonationAlerts-orange?style=for-the-badge)](https://www.donationalerts.com/r/deskcj)

### Your support helps with

- 💻 dedicating more time to development;
- 🔬 conducting hardware testing;
- 🛠 continuing driver research and debugging;
- 📱 acquiring additional Windows on ARM devices for testing;
- 🚀 moving faster toward a stable version and public driver release.

Even a small contribution helps keep the project moving forward.

If you are unable to support the project financially, you can still help by:

- ⭐ starring this repository;
- 📢 sharing the project with other users;
- 💬 providing feedback;
- 🧪 participating in future testing.

Thank you for supporting independent Windows on ARM development!

---

## 🚧 Current Development Focus

The main development focus is currently the integration of the rear camera into the Windows AVStream pipeline.

The rear camera sensor is already communicating correctly with the Qualcomm Spectra 380 ISP and successfully producing RAW image data.

The remaining tasks include:

- RAW image conversion;
- AVStream integration;
- stable frame streaming;
- driver optimization;
- real-time rear camera preview.

---

# Project Status

| Component | Status |
|-----------|:------:|
| Camera Device Detection | ✅ |
| Front Camera | ✅ |
| Live Preview | ✅ |
| Photo Capture | ✅ |
| Video Recording | ✅ |
| Rear Sensor Initialization | ✅ |
| RAW Frame Acquisition | ✅ |
| AVStream Integration | 🚧 |
| Rear Camera Preview | 🚧 |
| Driver Optimization | 🚧 |
| Public Release | ⏳ |

---

# Development Roadmap

- ✅ Camera device detection
- ✅ Front camera support
- ✅ Live camera preview
- ✅ Photo capture
- ✅ Video recording
- ✅ Rear camera sensor initialization
- ✅ RAW frame acquisition
- 🔄 Image conversion for AVStream
- 🔄 Stable rear camera preview
- 🔄 Driver optimization
- ⏳ Public driver release

---

# Screenshots and Proof of Progress

## Custom Camera Devices

Windows successfully detects the custom front and rear camera devices.

<img width="1124" height="338" alt="Xiaomi Pad 5 camera devices in Device Manager" src="https://github.com/user-attachments/assets/01555ca4-fe18-4520-b454-3df4cc5ccbc1" />

---

## Front Camera Preview

Live image from the Xiaomi Pad 5 front camera in the native Windows Camera application.

<img width="1280" height="800" alt="Xiaomi Pad 5 front camera running in Windows" src="https://github.com/user-attachments/assets/7304e4f5-d540-4a68-98f8-e8eb4825617c" />

---

## Captured Photo

This photo was captured directly using the Xiaomi Pad 5 front camera through the native Windows Camera application.

<img width="640" height="480" alt="Photo captured with the Xiaomi Pad 5 front camera" src="https://github.com/user-attachments/assets/d14b1e05-f7d7-48cf-915f-c73e6355e3f1" />

---

## Front Camera Video Recording

This video was recorded directly using the Xiaomi Pad 5 front camera through the native Windows Camera application.

https://github.com/user-attachments/assets/b9e4c36c-73e8-4a89-a402-b55aa148bedd

---

## Rear Camera Development

The rear camera sensor is successfully communicating with the Qualcomm Spectra 380 ISP.

A complete RAW frame has already been captured from the sensor. This confirms that sensor communication, the CSI interface, and the ISP pipeline are functioning correctly.

Development is currently focused on converting the RAW image data into a standard video stream and integrating it into Windows through AVStream.

<img width="258" height="191" alt="RAW frame captured from the Xiaomi Pad 5 rear camera" src="https://github.com/user-attachments/assets/7c059fe0-e10a-41e6-805f-d406f9626dba" />

---

# Technical Highlights

- Custom AVStream driver
- Qualcomm Spectra 380 ISP integration
- Working front camera support
- Rear camera sensor initialization
- RAW frame acquisition from the rear camera
- Native Windows Camera application support
- Ongoing AVStream pipeline development

---

# Driver Availability

The drivers are currently private.

They remain experimental and are intended only for internal development and testing.

A public release will be considered once the drivers become sufficiently stable, reliable, and safe for everyday use.

Do not download or redistribute unofficial builds claiming to represent this project.

---

# Disclaimer

This is an independent community project.

The project is **not affiliated with, endorsed by, or officially supported by Xiaomi, Microsoft, Qualcomm, or any other company.**

Use of experimental software is entirely at your own risk.

---

# Stay Updated

⭐ Star this repository to follow the project's development and receive updates about future releases.

Thank you for your interest and support!
