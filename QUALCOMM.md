# Qualcomm ISP research — separate from the downloadable camera drivers

[Русский](QUALCOMM.ru.md) · [Working camera download and installation](README.md)

The goal of this research is to use the Xiaomi Pad 5's Qualcomm imaging hardware more like the Android camera stack, then improve color, exposure, noise reduction, detail, and frame rate under Windows. **This is ongoing research, not an installable release.** The downloadable ZIP on the main page uses a different, software-processing path.

## Current evidence

| Path | Observed result | Remaining problem |
| --- | --- | --- |
| Rear OV13B10 through Qualcomm | Recognizable real scene frames; one saved short run delivered 79/79 unique frames at about 15.6 FPS | Slow first frame (about 3.5 s), inconsistent starts, orientation/color defects, below Android speed and image quality |
| Front OV8856 through Qualcomm | Some short tests produced changing, partly recognizable frames | Strong fixed image pattern and intermittent stalls; the most recent bounded open returned **zero frames** and timed out after 25 s |
| Daily downloadable drivers | Both front and rear publish usable Windows camera streams | Software image processing remains visibly below Android quality |

The Qualcomm results above are **not combined into one stable front/rear driver** and are **not in the public ZIP**. A nominal 1920 × 1080 buffer is not proof of full independent 1920 × 1080 image detail; one experimental front decoder duplicated source columns. We will not label that as native full-resolution capture.

## What the latest front diagnostic established

The first recorded front-camera overflow showed top ISP interrupt status `0x1e00` and BUS status `0x4000`. At that instant `BUS DEBUG_STATUS_CFG` was `0`, `DEBUG_STATUS_0` was `0`, and the sampled WM2 configuration/status were also `0`. Therefore the zero debug value does **not** identify the faulty output, and the earlier idea that an active WM2 alone explains the failure is unsupported. The test delivered no front frames, then returned to the working software drivers without a reboot or observed crash.

Xiaomi's published [VFE170 register map](https://raw.githubusercontent.com/MiCode/Xiaomi_Kernel_OpenSource/nabu-r-oss/drivers/media/platform/msm/camera/cam_isp/isp_hw_mgr/isp_hw/vfe_hw/vfe17x/cam_vfe170.h) places the BUS debug configuration/status at `0x226c`/`0x2270`. Its [Android BUS initialization](https://raw.githubusercontent.com/MiCode/Xiaomi_Kernel_OpenSource/nabu-r-oss/drivers/media/platform/msm/camera/cam_isp/isp_hw_mgr/isp_hw/vfe_hw/vfe_bus/cam_vfe_bus_ver2.c) enables the debug register with `0x82` and treats `0x4000` as part of the BUS error mask. A separate diagnostic package implementing only that debug-register setup has been built and signed **offline**; it has **not** been installed or validated on hardware. This is instrumentation, not an image-quality fix.

## Why Android parameters cannot simply be pasted into Windows

Android uses sensor-specific tuning together with a closed CamX/Spectra processing stack. Tuning data depends on the exact ISP nodes, buffer formats, request sequencing, and controls supplied by that stack. The Windows camera stack and the current custom drivers do not implement the same interface. Android captures and factory files are useful ground truth, but copying a tuning binary or color table into an unrelated Windows path will not automatically create Android-quality photos.

The required work is sequential: identify and stop the front BUS error; establish continuous, correctly decoded frames from **both** sensors; fix orientation and buffer geometry; then validate real unique FPS, exposure/white balance, color matrices, noise reduction, tone mapping, and application behavior. The rear path also needs reliable startup and more throughput. There is no honest date or build count for completion.

No Android proprietary libraries, tuning binaries, Ghidra output, private test logs, or experimental Qualcomm driver packages are published in this repository.

## Support

If you want to support independent research: [DonationAlerts](https://www.donationalerts.com/r/deskcj) · [PayPal](https://paypal.me/vladcj). Donations do not guarantee a particular technical outcome or timeline.

## Disclaimer

These are experimental research notes, **not an installable Qualcomm camera release** or a promise that Android image quality will be achieved. The project is independent and is not affiliated with or endorsed by Xiaomi, Qualcomm, Microsoft, or OmniVision. Their names are used only to identify the hardware and technologies under study.
