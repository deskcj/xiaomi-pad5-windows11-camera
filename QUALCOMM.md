# Qualcomm ISP research for Xiaomi Pad 5

[Русский](QUALCOMM.ru.md) · [Download the usable camera preview](README.md)

![Status](https://img.shields.io/badge/status-active%20research-orange?style=for-the-badge) ![Rear](https://img.shields.io/badge/rear-real%20frames-yellow?style=for-the-badge) ![Front](https://img.shields.io/badge/front-partial%20frames-red?style=for-the-badge)

The goal is to make the Xiaomi Pad 5 cameras under Windows behave much closer to Android: correct color and exposure, better detail, lower noise, stable real 30 FPS, and reliable front/rear switching.

This page covers the experimental **Qualcomm ISP/Spectra path**. It is separate from the [public community drivers](README.md), which already provide two usable cameras through a safer software-processing path. The Qualcomm work is **not yet an installable release**.

## Status at a glance

| Path | Confirmed result | Status |
| --- | --- | --- |
| Public front + rear drivers | Preview, photos, video, switching, Chrome and Telegram; short tests around 25–26 unique FPS | ✅ Public preview |
| Rear OV13B10 through Qualcomm | Recognizable real scene; one short run produced 79/79 unique frames at about 15.6 FPS | 🟡 Working proof, not stable |
| Front OV8856 through Qualcomm | Changing, partly recognizable frames; one saved run produced 4/4 unique frames at about 1.62 FPS before stalling | 🟡 Partial proof, output path unstable |
| Android-quality Qualcomm package | Stable dual streams, correct orientation, color, 3A and real 30 FPS | ⬜ Not ready |

## Confirmed milestones

- ✅ Identified the physical sensors: front **OV8856** and rear **OV13B10**, both with BGGR RAW input.
- ✅ Confirmed sensor control, MIPI/CSID/VFE/DMA access and real changing sensor data under Windows.
- ✅ Preserved a usable fallback path so experiments can return to working Windows cameras.
- ✅ Measured Android ground truth: both sensors deliver approximately 30 unique FPS at 1280 × 960; Android also exposes 1440 × 1080 and 1920 × 1080 streams around 30 FPS.
- ✅ Collected Android camera characteristics, runtime measurements and relevant factory tuning material for comparison.
- ✅ Captured recognizable real rear frames through the Qualcomm path.
- ✅ Captured changing, partly recognizable front frames through the Qualcomm path.
- ✅ Narrowed the latest front failure to the ISP BUS/output stage rather than a missing sensor or completely dead MIPI input.

## Rear camera — OV13B10

### Already demonstrated

- ✅ Real scene data reaches the Qualcomm capture path.
- ✅ Frames change instead of replaying one frozen buffer.
- ✅ One saved short run delivered **79/79 unique frames at about 15.6 FPS**.
- ✅ The scene is recognizable, proving that the rear sensor, MIPI route and output-buffer chain can work together.

### Remaining work

- 🔄 Make startup reliable; a measured first frame took about **3.5 seconds**.
- 🔄 Correct orientation and color in every capture.
- 🔄 Raise throughput to a stable real 30 FPS without duplicate frames.
- 🔄 Apply Android-grounded exposure, white balance, color matrices, denoise, tone mapping and sharpening through a compatible Windows pipeline.
- 🔄 Pass repeated-open, switching and long-session tests without a crash or stall.

**Rear status:** the hardware route is proven and this is the more advanced Qualcomm path, but it is not ready to replace the public driver.

## Front camera — OV8856

### Already demonstrated

- ✅ The sensor initializes and produces changing data.
- ✅ Short experiments produced partly recognizable scene information instead of only a static green buffer.
- ✅ One saved run delivered **4/4 unique frames at about 1.62 FPS** before the pipeline stopped.
- ✅ Diagnostics captured the failure state needed to investigate the ISP output route.

### Remaining work

- 🔄 Remove the fixed pattern and reconstruct the full frame with the correct stride, planes and output-buffer ownership.
- 🔄 Stop intermittent stalls and the current ISP BUS error.
- 🔄 Establish a continuous correctly decoded stream before image-quality tuning.
- 🔄 Match Android orientation, color, exposure, white balance, denoise and frame rate.

The latest bounded front open delivered no frames and timed out after 25 seconds. At the first recorded overflow, top ISP interrupt status was `0x1e00` and BUS status was `0x4000`. The debug selector was not enabled in that run, so the zero debug value could not identify the exact faulty output. Xiaomi's [VFE170 register map](https://raw.githubusercontent.com/MiCode/Xiaomi_Kernel_OpenSource/nabu-r-oss/drivers/media/platform/msm/camera/cam_isp/isp_hw_mgr/isp_hw/vfe_hw/vfe17x/cam_vfe170.h) and [Android BUS initialization](https://raw.githubusercontent.com/MiCode/Xiaomi_Kernel_OpenSource/nabu-r-oss/drivers/media/platform/msm/camera/cam_isp/isp_hw_mgr/isp_hw/vfe_hw/vfe_bus/cam_vfe_bus_ver2.c) show how to enable the relevant diagnostics. An offline diagnostic package exists, but it is instrumentation—not an image-quality fix or public driver.

**Front status:** real changing input has been proven; the complete reliable output-buffer path remains the main blocker.

## Roadmap to Android-like quality

1. ✅ Detect and control both sensors.
2. ✅ Preserve a usable public Windows fallback package.
3. 🟡 Make rear Qualcomm capture start reliably and sustain a real stream.
4. 🟡 Fix the front BUS/output-buffer path and obtain a clean continuous frame.
5. ⬜ Combine both cameras in one stable Windows package with correct orientation and switching.
6. ⬜ Implement or connect AE/AWB, color matrices, denoise, tone mapping and sharpening based on Android measurements.
7. ⬜ Validate unique FPS, low light, repeated opens, long sessions, Windows Camera, Telegram, browsers and conferencing apps.
8. ⬜ Publish a safe Qualcomm preview only after both cameras pass those checks.

## Why Android tuning cannot simply be copied

Android tuning works together with the closed CamX/Spectra request graph, exact buffer formats, ISP nodes and control sequence. Factory files and Android measurements are valuable ground truth, but a tuning BIN is not a Windows driver. First the Windows Qualcomm path must produce stable, correctly described buffers; only then can color, exposure and denoise be reproduced meaningfully.

A nominal 1920 × 1080 buffer is also not proof that every output pixel contains independent sensor detail. One front experiment duplicated source columns, so this project measures **unique changing frames and decoded detail**, not only advertised dimensions or sample counts.

No proprietary Android libraries, tuning binaries, Ghidra projects, private logs or unsafe experimental Qualcomm packages are published here.

## ❤️ Help move the research forward

Native camera development on Windows on ARM requires long reverse-engineering sessions, repeated builds, real-device measurements and careful recovery from failed kernel experiments. Support helps dedicate more time to the front BUS issue, rear throughput, Android-grounded image processing and long stability tests.

[![Support via DonationAlerts](https://img.shields.io/badge/Support-DonationAlerts-orange?style=for-the-badge)](https://www.donationalerts.com/r/deskcj) [![Support via PayPal](https://img.shields.io/badge/Support-PayPal-0070BA?style=for-the-badge&logo=paypal&logoColor=white)](https://paypal.me/vladcj)

You can also help without donating: star and share the repository, test the public package, and report reproducible results with the Windows version, application, lighting and selected camera. Donations support continued investigation; they do not guarantee a technical result or delivery date.

## Disclaimer

This is experimental research, **not an installable Qualcomm camera release** and not a promise that Android image quality will be achieved. The project is independent and is not affiliated with or endorsed by Xiaomi, Qualcomm, Microsoft or OmniVision. Their names are used only to identify the hardware and technologies under study.
