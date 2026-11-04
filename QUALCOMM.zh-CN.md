# Xiaomi Pad 5 的 Qualcomm ISP 相机

[![English](https://img.shields.io/badge/English-57606a?style=for-the-badge)](QUALCOMM.md) [![Русский](https://img.shields.io/badge/%D0%A0%D1%83%D1%81%D1%81%D0%BA%D0%B8%D0%B9-57606a?style=for-the-badge)](QUALCOMM.ru.md) [![简体中文](https://img.shields.io/badge/%E7%AE%80%E4%BD%93%E4%B8%AD%E6%96%87-2f81f7?style=for-the-badge)](QUALCOMM.zh-CN.md) [![项目主页](https://img.shields.io/badge/%E9%A1%B9%E7%9B%AE%E4%B8%BB%E9%A1%B5-57606a?style=for-the-badge&logo=github&logoColor=white)](README.zh-CN.md)

![状态](https://img.shields.io/badge/status-both%20cameras%20working-success?style=for-the-badge) ![后置](https://img.shields.io/badge/rear-working-success?style=for-the-badge) ![前置](https://img.shields.io/badge/front-working-success?style=for-the-badge)

**在运行 Windows 11 ARM64 的 Xiaomi Pad 5（nabu）上，前后摄像头均已通过 Qualcomm 硬件 ISP 工作。** 已测试的配置使用 IFE/IPE 图像处理流程，提供实时预览和照片，并实现较好的画质、改善的色彩、后置自动对焦及自动亮度调节。

## 下载

**[下载 Qualcomm ISP 253 驱动 ZIP](https://github.com/deskcj/xiaomi-pad5-windows11-camera/raw/refs/heads/main/downloads/Xiaomi-Pad-5-Camera-QualcommISP253-20261104.zip)**

选定的参考组合为 **253 调校、SYS080、DeviceMFT075b**，附带基于已验证 R2.4 修复的通用防护式安装程序。已接受的图像处理载荷未变。原始 Qualcomm C/C++ 厂商栈及固件源码不可获取，因此无法仅凭源码使用 EWDK 重新构建整个版本。

**[下载项目代码、调校数据和 IFE/IPE 说明](https://github.com/deskcj/xiaomi-pad5-windows11-camera/raw/refs/heads/main/downloads/Xiaomi-Pad-5-Camera-QualcommISP253-Development-20261104.zip)** · [浏览开发资料](sources/qualcommisp/README.md)。

## 安装与诊断

按照[指南（英文）](https://github.com/erdilS/Port-Windows-11-Xiaomi-Pad-5/blob/main/guide/English/disable-secureboot-en.md)关闭 Secure Boot 并启用 **测试模式（Test Mode）**。关闭所有相机应用，完整解压 ZIP，然后**以管理员身份运行 `Setup.cmd`**。若要求重启，正常重启后再次运行；等待 SUCCESS / 退出代码 0，再在 Windows「相机」中测试两颗摄像头。

若需重新检查实际画面，关闭相机应用，**以管理员身份运行 `Diagnose.cmd`**。分享 `C:\ProgramData\XiaomiPad5Camera\Logs` 中最新的完整文件夹，包括 `result.json` 和 `setup.log`；分享前请先查看保存的诊断帧。`Check.cmd` 只检查文件和平台。

## 🎬 视频演示：前后摄像头通过 Qualcomm ISP 工作

观看运行 Windows 11 ARM64 的 **Xiaomi Pad 5（nabu）前后摄像头**使用 Qualcomm ISP 的演示。视频展示的是开发里程碑；画质和稳定性仍在继续改进。

[![观看 Xiaomi Pad 5（nabu）前后摄像头通过 Qualcomm ISP 工作的演示](https://img.youtube.com/vi/ue8ANiYQOg0/hqdefault.jpg)](https://www.youtube.com/watch?v=ue8ANiYQOg0)

▶️ **[在 YouTube 观看演示](https://www.youtube.com/watch?v=ue8ANiYQOg0)**

## ✅ 已实现功能

| 功能 | 后置摄像头 | 前置摄像头 |
| --- | --- | --- |
| Windows「相机」实时预览 | ✅ 可用 | ✅ 可用 |
| 拍照 | ✅ 可用 | ✅ 可用 |
| 已测试的全分辨率照片 | ✅ 4160 × 3120，约 13 MP | ✅ 3264 × 2448，约 8 MP |
| 自动亮度和白平衡 | ✅ 可用，仍在继续调校 | ✅ 可用，仍在继续调校 |
| 对焦 | ✅ 自动对焦 | ✅ 固定对焦 |
| 照片与预览的色彩一致性 | ✅ 已改善并检查 | ✅ 已改善并检查 |

可用的照片分辨率与相机控制选项取决于应用。前置固定对焦是硬件特性。

## 📷 更好的照片与预览

- ✅ 在已测试的配置中，两颗摄像头均可生成完整且细节丰富的图像。
- ✅ 在已测试的室内和日光场景中，照片与预览画质较好。
- ✅ 色彩再现和亮度得到改善；后续优化以 Android 测量结果为参考。
- ✅ 在已检查的对比中，此前的绿色偏色和保存照片发灰问题已得到修正。
- ✅ 在已检查的场景中，保存照片的亮度和色彩现已与预览较为接近。
- ✅ 两颗摄像头均已测试全分辨率拍照。

## 🎯 对焦和自动调节

- ✅ 后置自动对焦可用于近处物体和较远场景。
- ✅ 前后摄像头均可自动调节亮度和白平衡。
- ✅ 此前反馈的后置相机在光照变化时闪烁的问题已处理，并在用户测试中检查。
- ✅ 已在拍照测试中确认后置闪光灯工作。

亮度过渡、复杂光照及对焦响应仍在持续改进。此平板的前置相机在硬件上没有自动对焦或前置 LED 闪光灯。

## 🔄 后续改进

下一目标是在日常使用中让两颗摄像头更接近 Android：

- 🔄 在明亮、昏暗和混合光照下获得更一致的色彩与曝光。
- 🔄 更好地保留高光、阴影及细节。
- 🔄 加快启动、使亮度过渡更平滑，并提高后置对焦响应速度。
- 🔄 继续测试帧率、低光及长时间运行的稳定性。
- 🔄 在不同相机驱动之间切换后更可靠地恢复工作。

**两颗摄像头正常工作是已确认的里程碑；在所有光照条件下完全达到 Android 画质以及稳定 30 FPS，仍是正在努力的目标。**

## 📌 发布范围

这是公开预览版。全新安装检查与历史光学/FPS 测量覆盖的条件不同。反馈的后置启动咔嗒声尚未通过声学方法定位。尚未确认 HDR 及所有应用中的点按对焦表现。

## ❤️ 支持持续开发

开发需要大量时间、真实平板测试及与 Android 对比。支持有助于继续改善两颗摄像头的画质、对焦、速度和可靠性。

[![通过 DonationAlerts 支持](https://img.shields.io/badge/Support-DonationAlerts-orange?style=for-the-badge)](https://www.donationalerts.com/r/deskcj)

您也可以给仓库点星、分享项目，并反馈使用体验，说明 Windows 版本、应用、光照条件和所用摄像头。捐款用于支持持续开发，不保证交付日期或特定结果。

## 免责声明

这是独立的社区项目，**与 Xiaomi、Qualcomm、Microsoft 或 OmniVision 无关联，也未获得其认可**。公司和产品名称仅用于说明兼容硬件和技术；相关权利属于各自的所有者。

这些驱动是采用测试签名的实验性内核软件，按**“现状”**提供，不保证达到 Android 的画质、稳定性或支持水平。使用中可能发生崩溃、设备故障或数据丢失。安装前请备份系统，并保留恢复设备的方法。是否测试及相关风险由您自行承担。本声明并不授权违反第三方工具许可协议进行再分发。
