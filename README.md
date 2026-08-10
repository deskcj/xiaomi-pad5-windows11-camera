<p align="center">
  <b>🌐 Language:</b>
  🇬🇧 <a href="README.md">English</a> |
  🇷🇺 <a href="README.ru.md">Русский</a>
</p>

---

# Xiaomi Pad 5 (nabu) — Windows 11 Camera Project 📷

> Independent development of native front and rear camera support for the Xiaomi Pad 5 running Windows 11 on ARM64.

![GitHub Stars](https://img.shields.io/github/stars/deskcj/xiaomi-pad5-windows11-camera?style=for-the-badge)
![Status](https://img.shields.io/badge/Status-Working%20Prototype-brightgreen?style=for-the-badge)
![Platform](https://img.shields.io/badge/Platform-Windows%2011%20ARM-blue?style=for-the-badge)
![Device](https://img.shields.io/badge/Device-Xiaomi%20Pad%205-green?style=for-the-badge)

---

## Latest status: both cameras are working

As of **August 10, 2026**, the private AV44–AV47 development builds provide working front and rear cameras in the native Windows Camera application.

### Confirmed results

- ✅ Windows detects and starts both custom camera devices
- ✅ Switching between the front and rear cameras works
- ✅ Photo capture works on both cameras
- ✅ Video recording works on both cameras
- ✅ Front camera produces approximately **14–15 real FPS**
- ✅ Rear AV45 warm test: **947 new real frames in 55 seconds** — approximately **17.1 real FPS**
- ✅ Rear AV45 delivery rate: approximately **21.3 FPS**
- ✅ Rear video recording was also tested continuously for **1 minute 5 seconds**
- ✅ No rear-camera freezes, stale RAW frames, capture errors, or session restarts during the clean validation interval
- ✅ Orientation and general exposure are functional
- ✅ The conflicting stock Qualcomm AVStream device can be disabled while the custom stack is active

This is a major functional milestone, but it is **not yet a public release**. Color calibration, image noise, highlight handling, performance tuning, long-duration application testing, and safe release packaging still remain.

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
- 🔄 AV48 is developing a software-only highlight shoulder curve; it is not yet a confirmed baseline

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
| Rear stale-frame recovery | ✅ |
| Orientation and basic exposure | ✅ |
| Front performance | 🚧 14–15 FPS |
| Rear capture performance | 🚧 ~17.1 real FPS |
| Color calibration and denoising | 🚧 |
| Highlight handling | 🚧 AV48 in development |
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
- AVStream development from **AV1 through AV48**;
- at least **108 completed preserved build/test iterations**, with AV48 currently in progress.

The real number of tablet tests is higher because many builds were installed and exercised multiple times for photo, video, camera switching, orientation, timing, crash, and long-session validation.

---

## Current development focus

1. Preserve the AV44/AV45 stability baseline.
2. Complete and validate AV48 highlight compression without touching sensor registers.
3. Reduce the remaining color cast and shadow noise.
4. Tune exposure, gain, and white balance safely.
5. Improve front-camera throughput beyond 14–15 FPS where possible.
6. Test long video calls in Zoom, browsers, Telegram, and other applications.
7. Prepare a safe installer, rollback path, and eventual public package.

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
