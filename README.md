<p align="center">
  <b>🌐 Language:</b>
  🇬🇧 <a href="README.md">English</a> |
  🇷🇺 <a href="README.ru.md">Русский</a>
</p>

---

# Xiaomi Pad 5 (nabu) — Windows 11 Camera Project 📷

> Independent development of native front and rear camera support for the Xiaomi Pad 5 running Windows 11 on ARM64.

![GitHub Stars](https://img.shields.io/github/stars/deskcj/xiaomi-pad5-windows11-camera?style=for-the-badge)
![Status](https://img.shields.io/badge/Status-AV87%20Validated-brightgreen?style=for-the-badge)
![Platform](https://img.shields.io/badge/Platform-Windows%2011%20ARM-blue?style=for-the-badge)
![Device](https://img.shields.io/badge/Device-Xiaomi%20Pad%205-green?style=for-the-badge)

---

## Latest status: stable 30 FPS camera prototype

As of **August 11, 2026**, private build **AV87** provides working front and rear cameras in the native Windows Camera application. **AV82** is preserved as a known-good rollback baseline, while **AV88** is under development and is not installed or validated yet.

### Confirmed results

- ✅ Windows detects and starts both custom camera devices
- ✅ Switching between the front and rear cameras works
- ✅ Photo capture works on both cameras
- ✅ Video recording works on both cameras
- ✅ AV87 front 120-second test: **30.032 delivered FPS** and **28.624 unique FPS**
- ✅ AV87 rear 120-second test: **30.047 delivered FPS** and **27.272 unique FPS**
- ✅ No frame delivery failures during either 120-second validation run
- ✅ Repeated front/rear open and switching cycles complete successfully
- ✅ Startup no longer presents a previously captured stale frame before the live stream
- ✅ Orientation, preview, photos, video, and basic adaptive tone processing work on both cameras
- ✅ Cold first live frame currently arrives in approximately **1.0–1.7 seconds**
- ✅ The conflicting stock Qualcomm AVStream device can be disabled while the custom stack is active

This is a major stability and performance milestone, but it is **not yet a public release**. Android-level image quality has not yet been reached. Sensor auto-exposure, white balance, color calibration, noise reduction, highlight recovery, long application tests, and safe release packaging still remain.

---

## About the project

The goal is to restore usable front and rear cameras on the **Xiaomi Pad 5 (nabu)** under **Windows 11 on ARM64** without depending on the non-working stock camera stack.

The work includes:

- reverse engineering the Qualcomm camera platform interface;
- controlling sensor power, GPIO, MCLK, and CCI;
- configuring CAMCC, CSIPHY, CSID, VFE, SMMU, IOVA, and DMA;
- creating custom ARM64 kernel drivers;
- building physical hardware backends and virtual AVStream camera devices;
- converting RAW10 Bayer frames into formats accepted by Windows camera applications;
- debugging kernel crashes, memory ownership, session lifetime, and stale-frame handling.

> **The camera drivers remain private and are not currently available for public download.**

---

## Confirmed camera hardware

| Camera | Sensor | Chip ID | Sensor mode | Current result |
|---|---|---:|---:|---|
| Front | OmniVision OV8856 | <code>0x885A</code> | RAW10, 3264×2448 | Live preview, photos and video |
| Rear | OmniVision OV13B10 | <code>0x0D42</code> | RAW10, validated capture at 2104×1560 | Live preview, photos and video |

Custom device layout:

- Front hardware backend: <code>ACPI\QCOM05A4\18</code>
- Front virtual camera: <code>ROOT\CAMERA\0006</code>
- Rear hardware backend: <code>ACPI\QCOM0529\15</code>
- Rear virtual camera: <code>ROOT\CAMERA\0007</code>

---

## Development milestones

- ✅ Reverse engineered the private Qualcomm platform-driver interface
- ✅ Built and loaded custom Windows ARM64 kernel drivers
- ✅ Confirmed camera power, GPIO, MCLK, CCI, and register access
- ✅ Identified the rear OV13B10 sensor as <code>0x0D42</code>
- ✅ Implemented rear-sensor initialization and stream control
- ✅ Proved the CSID → VFE → SMMU → DMA path with a test-pattern generator
- ✅ Captured the first complete real rear RAW frame in Stage39
- ✅ Identified the front OV8856 sensor as <code>0x885A</code>
- ✅ Initialized the 3264×2448 front mode and enabled sensor streaming in Stage57
- ✅ Confirmed front MIPI packets, SOF, EOF, RAW10 data type, and clean CRC/ECC state in Stage68
- ✅ Captured the first complete real front RAW frame in the corrected Stage70 build
- ✅ Created custom front and rear AVStream camera devices
- ✅ Replaced the unsafe monolithic design with separate physical backends and virtual cameras
- ✅ Displayed the first real front-camera image in Windows Camera
- ✅ Improved the front camera from isolated still frames to approximately 14–15 FPS
- ✅ Brought the rear camera from RAW-only capture to live Windows Camera preview
- ✅ Fixed rear stale-frame replay and added automatic hardware-session recovery in AV44
- ✅ Improved rear throughput and verified a clean sustained stream in AV45
- ✅ Added scene-adaptive shadow lifting in AV46
- ✅ Rejected invalid out-of-range RAW10 statistics and eliminated white-point jumps in AV47
- ✅ Progressed through extensive exposure, color, orientation, startup, and throughput tuning after AV48
- ✅ Preserved AV82 as the known-good rollback build after major image-quality improvements
- ✅ AV87 removed stale startup-frame presentation, optimized conversion, and passed clean 120-second tests near 30 FPS on both cameras
- 🔄 AV88 is adding sensor-level auto-exposure for OV8856 and OV13B10, highlight protection, smoother bright/dark transitions, and revised color balance; it is not yet validated

---

## Current project status

| Component | Status |
|---|:---:|
| Front sensor detection and initialization | ✅ |
| Rear sensor detection and initialization | ✅ |
| Real RAW capture from both sensors | ✅ |
| CSIPHY / CSID / VFE / SMMU / DMA path | ✅ |
| Custom ARM64 hardware backends | ✅ |
| Front virtual AVStream camera | ✅ |
| Rear virtual AVStream camera | ✅ |
| Windows Camera live preview | ✅ |
| Photo capture on both cameras | ✅ |
| Video recording on both cameras | ✅ |
| Camera switching | ✅ |
| Stale-frame prevention and recovery | ✅ |
| Orientation and adaptive tone processing | ✅ |
| Front performance | ✅ 30.032 delivered / 28.624 unique FPS |
| Rear performance | ✅ 30.047 delivered / 27.272 unique FPS |
| Color calibration and denoising | 🚧 |
| Sensor auto-exposure and highlight handling | 🚧 AV88 in development |
| Long Zoom/browser-call validation | 🚧 |
| Public installer and release package | ⏳ |

---

## How the custom stack works

**Front camera**

OV8856 → physical hardware backend → RAW10 capture → software conversion → virtual AVStream camera → Windows Camera

**Rear camera**

OV13B10 → physical hardware backend → persistent RAW10 capture → generation-aware bridge → software conversion → virtual AVStream camera → Windows Camera

The physical and virtual drivers are intentionally separated. This keeps hardware ownership and long-running camera sessions away from fragile AVStream lifecycle callbacks and was an important step in eliminating crashes and frozen frames.

---

## Test and development scale

The preserved development workspace contains:

- **54** distinct low-level <code>NabuCamOwnStage</code> directories;
- a hardware investigation series that reached **Stage75**;
- AVStream development from **AV1 through AV88**;
- at least **147 completed preserved build/test iterations**, with AV88 currently in development.

The real number of tablet tests is higher because many builds were installed and exercised multiple times for photo, video, camera switching, orientation, timing, crash, and long-session validation.

---

## Current development focus

1. Preserve AV82 as the known-good rollback baseline and AV87 as the current validated performance baseline.
2. Complete and validate AV88 sensor-level auto-exposure without regressing stability or 30 FPS delivery.
3. Smooth transitions between bright and dark scenes while protecting highlights.
4. Improve skin tones, white balance, color accuracy, and shadow noise on both sensors.
5. Validate repeated cold starts, camera switching, long recordings, and video calls.
6. Prepare a safe installer, rollback path, and eventual public package.

Android-level image quality has not yet been reached. Achieving it may require additional sensor tuning, color calibration, more efficient Bayer conversion, and better use of the Qualcomm imaging pipeline.

---

## Screenshots and proof of progress

### Custom camera devices

Windows successfully detects the custom front and rear camera devices.

<img width="1124" height="338" alt="Xiaomi Pad 5 camera devices in Device Manager" src="https://github.com/user-attachments/assets/01555ca4-fe18-4520-b454-3df4cc5ccbc1" />

### Front camera preview

Live image from the Xiaomi Pad 5 front camera in the native Windows Camera application.

<img width="1280" height="800" alt="Xiaomi Pad 5 front camera running in Windows" src="https://github.com/user-attachments/assets/7304e4f5-d540-4a68-98f8-e8eb4825617c" />

### Captured front-camera photo

This photo was captured directly through the native Windows Camera application.

<img width="640" height="480" alt="Photo captured with the Xiaomi Pad 5 front camera" src="https://github.com/user-attachments/assets/d14b1e05-f7d7-48cf-915f-c73e6355e3f1" />

### Front camera video recording

This video was recorded directly through the native Windows Camera application.

https://github.com/user-attachments/assets/b9e4c36c-73e8-4a89-a402-b55aa148bedd

### First rear-camera RAW milestone

Stage39 produced the first complete optical RAW frame from the rear OV13B10 sensor under Windows on ARM64. Later builds progressed from this RAW milestone to live preview, photo capture, and video recording.

<img width="258" height="191" alt="RAW frame captured from the Xiaomi Pad 5 rear camera" src="https://github.com/user-attachments/assets/7c059fe0-e10a-41e6-805f-d406f9626dba" />

---

## ❤️ Support the project

This project is developed independently in my spare time. Reverse engineering and testing kernel camera drivers on real Windows on ARM hardware requires substantial time, repeated hardware validation, and careful recovery from failed experiments.

If you would like to help move the project toward better image quality and a safe public release, you can support development using either option:

[![Support via DonationAlerts](https://img.shields.io/badge/❤️%20Support-DonationAlerts-orange?style=for-the-badge)](https://www.donationalerts.com/r/deskcj)

[![Support via PayPal](https://img.shields.io/badge/Support-PayPal-0070BA?style=for-the-badge&logo=paypal&logoColor=white)](https://paypal.me/vladcj)

- **DonationAlerts** — convenient for users from CIS countries.
- **PayPal** — suitable for international supporters.

### Your support helps with

- 💻 dedicating more time to driver development;
- 🔬 conducting hardware and stability testing;
- 🛠 continuing Qualcomm camera research and debugging;
- 📱 acquiring additional Windows on ARM hardware for validation;
- 🚀 moving toward a stable and safe public release.

You can also help by starring the repository, sharing the project, providing feedback, and participating in future testing.

Thank you for supporting independent Windows on ARM development!

---

## Driver availability

The drivers remain experimental and private. A public release will be considered only after they are sufficiently stable, reliable, and safe for everyday use.

Do not download or redistribute unofficial builds claiming to represent this project.

---

## Disclaimer

This is an independent community project. It is **not affiliated with, endorsed by, or officially supported by Xiaomi, Microsoft, Qualcomm, OmniVision, or any other company**.

Experimental kernel drivers can cause crashes, data loss, or an unbootable system. Use any future test release entirely at your own risk.

---

## Stay updated

⭐ Star this repository to follow development and future release announcements.

Thank you for your interest and support!
