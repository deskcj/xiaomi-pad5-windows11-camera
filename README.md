# Xiaomi Pad 5 (nabu) — Windows 11 Camera Project 📷

An independent research and development project focused on bringing camera support to the **Xiaomi Pad 5 (nabu)** running **Windows 11 on ARM**.

This repository serves as a public project page, proof-of-concept showcase, and development progress tracker.

> **Important:** Camera drivers and private development files are not currently included in this repository.

---

## Project Overview

Camera support is one of the major missing components on the Xiaomi Pad 5 running Windows 11.

This project involves low-level driver development, hardware research, reverse engineering, memory management debugging, CSI configuration, sensor initialization, and extensive testing on real hardware.

The goal is to achieve stable and usable camera support for Windows 11 on the Xiaomi Pad 5.

---

## Current Status

Significant progress has been made with both the camera hardware and the Windows driver stack.

### What has been achieved

* The camera sensors can be detected by Windows.
* The front camera path has produced a working live image.
* The rear camera sensor has been successfully identified and initialized during development tests.
* The rear sensor, four-lane CSI connection, and RAW data path have been validated.
* A critical memory allocation issue between AVStream and the Qualcomm SMMU has been identified and addressed.
* The camera driver packages can be installed with valid test signatures.

### Work in progress

The current development focus is the rear camera video stream.

The hardware is responding, but additional work is required to stabilize buffer handling, CSI data capture, image conversion, and continuous video delivery to the Windows Camera application.

The drivers remain in private testing because they are experimental and may cause crashes, system instability, or require manual configuration.

---

## Development Progress

* [x] Camera devices detected by Windows
* [x] Front camera sensor initialization
* [x] Front camera live image
* [x] Rear camera hardware detection
* [x] Rear camera sensor identification
* [x] Four-lane CSI and RAW path validation
* [x] Critical SMMU memory issue identified
* [x] Updated memory allocation implementation
* [ ] Stable rear camera frame capture
* [ ] Image orientation and color correction
* [ ] Long-term stability testing
* [ ] Simplified installation process
* [ ] Public driver release

---

## Video Demonstration

A detailed video demonstration will be published on YouTube.

The video will include:

* Windows 11 running on the Xiaomi Pad 5
* Camera devices in Device Manager
* Driver installation and detection
* Front camera operation
* Rear camera development progress
* Current limitations and next development steps

> 🎬 **YouTube demonstration coming soon**

After the video is published, its preview and link will be added here.

---

## Screenshots and Proof

Screenshots and additional development evidence will be added to this section.

### Device Manager

> Screenshot coming soon

### Windows Camera Application

> Screenshot coming soon

### Rear Camera Development Tests

> Additional proof coming soon

---

## Support the Project

Developing camera support for an unsupported Windows on ARM device requires extensive reverse engineering, kernel-level debugging, repeated testing, and many hours of independent work.

Financial support helps cover development hardware, testing equipment, software tools, and the time required to continue improving the drivers.

Supporting the project does not purchase immediate access to the experimental drivers and does not guarantee a specific release date. It directly supports continued research and development.

### DonationAlerts

[![Support via DonationAlerts](https://img.shields.io/badge/Support-DonationAlerts-orange?style=for-the-badge)](https://www.donationalerts.com/r/deskcj)

**[➡️ Support the Xiaomi Pad 5 Camera Project](https://www.donationalerts.com/r/deskcj)**

Every contribution, regardless of size, helps move the project forward. Thank you for supporting independent Windows on ARM development.

---

## Driver Availability

The camera drivers are not publicly available yet.

They are currently undergoing private testing and may:

* Require manual installation
* Require test-signing mode
* Cause system crashes or instability
* Work only with specific Windows builds
* Require device-specific registry configuration
* Change frequently during development

A public release will be considered when the installation process and camera operation become sufficiently stable and safe for broader testing.

Please do not download camera drivers from unofficial sources claiming to represent this project.

---

## Disclaimer

This is an independent, experimental project.

It is not affiliated with, endorsed by, or supported by Xiaomi, Microsoft, or Qualcomm.

All development builds are used at the user's own risk. Experimental kernel drivers can cause crashes, data loss, boot problems, or hardware instability.

---

## Contact and Updates

Follow this repository to receive future development updates, screenshots, test results, and the upcoming video demonstration.

⭐ Star the repository if you want to support the project and follow its progress.
