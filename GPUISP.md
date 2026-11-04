# GPU ISP cameras for Xiaomi Pad 5 (nabu)

[![English](https://img.shields.io/badge/English-2f81f7?style=for-the-badge)](GPUISP.md) [![Русский](https://img.shields.io/badge/%D0%A0%D1%83%D1%81%D1%81%D0%BA%D0%B8%D0%B9-57606a?style=for-the-badge)](GPUISP.ru.md) [![Project home](https://img.shields.io/badge/Project%20home-57606a?style=for-the-badge&logo=github&logoColor=white)](README.md)

![Status: public preview](https://img.shields.io/badge/status-public%20preview-orange?style=for-the-badge) ![Device: Xiaomi Pad 5](https://img.shields.io/badge/device-Xiaomi%20Pad%205-blue?style=for-the-badge) ![Platform: Windows 11 ARM64](https://img.shields.io/badge/platform-Windows%2011%20ARM64-0078D4?style=for-the-badge)

**[Download GPU ISP driver ZIP](https://github.com/deskcj/xiaomi-pad5-windows11-camera/raw/refs/heads/main/sources/packages/Xiaomi-Pad-5-Camera-GPUISP040-20261104.zip)** · **[Download GPU source snapshots](https://github.com/deskcj/xiaomi-pad5-windows11-camera/raw/refs/heads/main/sources/packages/Xiaomi-Pad-5-Camera-GPUISP040-Sources-20261104.zip)** · [Browse sources](sources/gpuisp/README.md).

Selected accepted stack: **GPU040** processing/host, physical **rear065**, physical **front066**, and **virtual034** cameras. Later experimental numbers were not chosen merely because they were newer. Accepted driver and processing parameter bytes are unchanged; the package adds the shared guarded installer.

## How it works

The physical drivers capture RAW Bayer from the real sensors. Two `GpuCameraPump` workers process it using **D3D12 compute shaders**, including color/white-balance handling, lens shading, tone/gamma, noise reduction and sharpening. Processed frames are published in order to the virtual AVStream front/rear cameras, so ordinary Windows camera applications can use them. `CodexNabuGpuIsp` runs the workers; it releases GPU frame resources while idle.

This is software image processing on the GPU. Qualcomm ISP uses the hardware IFE/IPE pipeline instead. Moving work to the GPU does not automatically reproduce CamX/Chromatix behavior.

## What works and limitations

| Feature | Status |
| --- | --- |
| Front and rear live frames / Windows virtual interfaces | ✅ Working in accepted-stack tests |
| GPU processing and ordered frame publication | ✅ D3D12 worker path |
| Earlier clean 20-second delivery check | ✅ Rear **28.957 unique FPS**, front **28.387 unique FPS**, no repeats/read errors in that check |
| Rear focus logic | ✅ rear065 corrects premature coarse-search success and stale confidence; optical accuracy still needs testing |
| Front transport correction | ✅ front066 removed four documented `0xABAB` samples; three retained RAW checks showed zero |
| Front autofocus / physical front LED flash | No such hardware: fixed-focus front sensor, no front LED |
| Android-equivalent color, exposure, detail and noise | ❌ Not achieved/confirmed |
| Tap-to-focus across apps, rear flash and HDR | ❌ Not qualified for this GPU release |
| Long-session/low-light stability and constant 30 FPS | 🔄 Further testing required |

Those FPS values come from earlier controlled tests of this exact accepted stack, not a guarantee for every Windows build or lighting condition. A later restoration test measured approximately 27.856 front and 29.286 rear FPS. GPU040 still showed a significant tone difference in offline Android-RAW comparisons. Android-quality parity is not claimed.

## Advantages and tradeoffs

GPU processing reduces reliance on the heavier CPU image-processing path and exposes an editable compute-shader pipeline with retained source snapshots. It requires a service, two workers and free RAM; it still needs independent AE/AWB, tone and focus work. Use at least **900 MiB available RAM** during activation. The front camera is fixed-focus in hardware.

## Install and diagnostics

Disable Secure Boot and enable **Test Mode** using [the guide](https://github.com/erdilS/Port-Windows-11-Xiaomi-Pad-5/blob/main/guide/English/disable-secureboot-en.md). Close all camera apps, extract the ZIP fully and run **`Setup.cmd` as administrator**. If a restart is requested, restart normally and run it again; wait for SUCCESS / exit code 0. Then test both cameras in Windows Camera.

For a fresh image test, close camera apps and run **`Diagnose.cmd` as administrator**. Send the newest complete folder in `C:\ProgramData\XiaomiPad5Camera\Logs`, including `result.json` and `setup.log`; review saved diagnostic frames first. `Check.cmd` checks files and platform only.


## ❤️ Help bring better cameras to Windows on nabu

This is independent work on real hardware: reading the camera stack, building ARM64 drivers, testing both sensors, and investigating failures that can bring down the whole tablet. Progress takes careful experiments and repeated validation. If this project is useful to you, your support helps keep that work going toward sharper, more reliable front and rear cameras.

[![Support via DonationAlerts](https://img.shields.io/badge/Support-DonationAlerts-orange?style=for-the-badge)](https://www.donationalerts.com/r/deskcj)

You can also help **without donating**: star the repository, share it with other nabu owners, and report reproducible results with your Windows version, camera app, lighting, and whether the front or rear camera was used. That feedback helps prioritize the next fixes. Donations support the research, not a guaranteed result or delivery date. Thank you for helping an independent Windows on ARM project grow.

## Disclaimer

This is an independent community project, **not affiliated with or endorsed by Xiaomi, Qualcomm, Microsoft, or OmniVision**. Company and product names identify compatible hardware and technologies only; their owners retain their rights.

The drivers are experimental, test-signed kernel software supplied **as is**, without a warranty or promise of Android-equivalent quality, stability, or support. They may cause crashes, device problems, or data loss. Back up your system and keep a recovery method before installing. You choose whether to test them and do so at your own risk. This notice does not grant permission to redistribute third-party tools or override their licenses.
