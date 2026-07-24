# xiaomi-pad5-windows11-camera
# Xiaomi Pad 5 (nabu) — Windows 11 Camera Project 🚀
## 📊 Current Status & Technical Progress

We have made significant progress in bringing the Xiaomi Pad 5 rear camera to life on Windows 11. 

### 🔧 What has been achieved
* **Hardware Validation:** Tests have successfully proven that the rear camera sensor, the four CSI lines, and the RAW data path are completely functional under Windows 11.
* **Memory Allocation Fix:** The main roadblock causing early system crashes has been identified and resolved. Previously, the `AVStream` driver architecture passed an incompatible memory structure (pool-MDL) to the SMMU (System Memory Management Unit). 
* **The Solution:** The rear camera driver has been updated to use the exact same, proven Qualcomm memory allocation method that already makes the front camera work flawlessly.

### ⚠️ Current Situation & Next Steps
The core camera driver now features valid digital signatures, and the memory mapping issue is completely resolved. The camera sensor is recognized, and initialization steps are executing properly.

Right now, the project is focused on configuring and stabilizing the CSI lane settings, since the front camera uses 2 lines while the rear camera requires 4 lines. We are actively fine-tuning the driver to stabilize the live video stream from the rear sensor.

## 💖 Support the Project & Speed Up the Release!

Bringing full camera support to the Xiaomi Pad 5 running Windows 11 requires hundreds of hours of complex reverse engineering, driver kernel debugging, and endless testing. This is a independent, passion-driven project made to push the boundaries of what Windows on ARM can do on tablets.

If you appreciate this work and want to see the final, stable driver released to the public much faster, please consider supporting the project! Your donations help cover hardware costs, development tools, and motivate me to spend every free hour on this breakthrough.

### 🚀 How to Support:
* **Support via DonationAlerts:** [➡️ Click here to donate via DonationAlerts ⬅️](https://www.donationalerts.com/r/deskcj)

Every single contribution, big or small, makes a huge difference and keeps this project alive. Thank you for your amazing support! 🙏
