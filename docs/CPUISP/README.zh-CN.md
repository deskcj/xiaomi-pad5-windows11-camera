# Xiaomi Pad 5（nabu）的 CPU ISP 相机

[![English](https://img.shields.io/badge/English-57606a?style=for-the-badge)](README.md) [![Русский](https://img.shields.io/badge/%D0%A0%D1%83%D1%81%D1%81%D0%BA%D0%B8%D0%B9-57606a?style=for-the-badge)](README.ru.md) [![简体中文](https://img.shields.io/badge/%E7%AE%80%E4%BD%93%E4%B8%AD%E6%96%87-2f81f7?style=for-the-badge)](README.zh-CN.md) [![项目主页](https://img.shields.io/badge/%E9%A1%B9%E7%9B%AE%E4%B8%BB%E9%A1%B5-57606a?style=for-the-badge&logo=github&logoColor=white)](../README.zh-CN.md)

![状态：公开预览版](https://img.shields.io/badge/status-public%20preview-orange?style=for-the-badge) ![设备：Xiaomi Pad 5](https://img.shields.io/badge/device-Xiaomi%20Pad%205-blue?style=for-the-badge) ![平台：Windows 11 ARM64](https://img.shields.io/badge/platform-Windows%2011%20ARM64-0078D4?style=for-the-badge)

**[下载已验证的 CPUISP4 驱动 ZIP](https://github.com/deskcj/xiaomi-pad5-windows11-camera/raw/refs/heads/main/downloads/Xiaomi-Pad-5-Camera-CPUISP4-20261104.zip)**

选用的 CPUISP4 驱动版本：前置硬件 **226.0.0.3**、后置硬件 **226.2.0.10**、虚拟前置 **226.1.0.3**、虚拟后置 **226.3.0.4**。ZIP 保留原有已签名 CPUISP4 SYS 代码，更新了 INF 日期和签名目录，并使用通用的 `Setup.cmd`/`Check.cmd`/`Diagnose.cmd` 安装及诊断命令。

**[下载 CPUISP4 C/H 源码](https://github.com/deskcj/xiaomi-pad5-windows11-camera/raw/refs/heads/main/downloads/Xiaomi-Pad-5-Camera-CPUISP4-Sources-20261104.zip)** · [浏览源码](../../sources/cpuisp/README.md)。

选择 CPUISP4，是因为它的 C/H 源码快照和 WDK 项目仍有保留。其四个 ARM64 驱动已使用 EWDK 和历史编译选项重新构建；已发布的 C/H 文件均与保留的快照一致。重新构建的文件与原有已签名二进制文件并非逐字节相同。

在项目平板上，从包含西里尔字母且带空格的路径运行公开的 `Setup.cmd` 和 `Diagnose.cmd`，两颗相机的 1280 × 960 图像检查及正常关闭均通过，无需重启。更新后的 INF/目录文件身份避免了此前 CPUISP3 前置驱动包的冲突。

## 工作原理

硬件后端配置前置 OV8856 和后置 OV13B10 传感器，采集 Bayer RAW 数据。大部分图像处理由 CPU 上的软件完成，然后 AVStream 虚拟相机将结果交给 Windows 应用。此版本不使用 GPU ISP 服务，也不使用 Qualcomm IPE 的最终成像路径。

## 已实现功能与限制

- ✅ 前后摄像头分别提供真实传感器画面。
- ✅ Windows「相机」中的预览、拍照、录像及前后切换均曾正常工作。
- ✅ CPU 相机路线曾用于 Chrome 和 Telegram；其他应用需单独测试。
- ✅ CPUISP4 后置 DMA 页列表回退方案不再依赖与前置相机争用的单块大型连续内存。
- ❌ 画质明显低于 Android：深色织物可能出现彩色噪声；前置可能偏软，后置则更软且噪声更多。曝光、色彩、细节和降噪仍需改进。
- ❌ 尚未确认持续稳定的真实 30 帧/秒及 Android 水平的对焦、闪光灯和 HDR。
- 🔄 历史 CPUISP4 对比中，后置输出由约 9.9 FPS 提升到 22 FPS；恢复前置细节处理后，该次对比中的边缘能量提高约 9%，噪声增加约 3%。后置保留了此前较轻的处理预算，因为较重的处理使输出从 15.6 FPS 降至 12.9 FPS。这些是特定场景下的开发测量值，不是性能保证。

CPUISP4 恢复了前置细节重建和自适应噪声的亮度细化处理，改进了后置帧调度，并将后置曝光限制在传感器帧周期以下 64 行。源码 ZIP 在 C/H 文件中保留了这些时序和图像处理常量。CPUISP3 的精确源码快照没有保留，因此以具有完整保留源码树的 CPUISP4 替换其下载版本，便于继续开发。

输出分辨率为 1280 × 960，并不自动证明具有传感器原生的完整细节。两颗相机能否使用以及启动过程也取决于硬件状态和应用对相机的占用。

## 安装与诊断

按照[指南（英文）](https://github.com/erdilS/Port-Windows-11-Xiaomi-Pad-5/blob/main/guide/English/disable-secureboot-en.md)关闭 Secure Boot 并启用 **测试模式（Test Mode）**。关闭所有相机应用，完整解压 ZIP，然后**以管理员身份运行 `Setup.cmd`**。若要求重启，正常重启后再次运行；等待 SUCCESS / 退出代码 0，再在 Windows「相机」中测试两颗摄像头。

若需重新检查实际画面，关闭相机应用，**以管理员身份运行 `Diagnose.cmd`**。分享 `C:\ProgramData\XiaomiPad5Camera\Logs` 中最新的完整文件夹，包括 `result.json` 和 `setup.log`；分享前请先查看保存的诊断帧。`Check.cmd` 只检查文件和平台。

## ❤️ 帮助 nabu 的 Windows 相机变得更好

这是在真实平板上进行的独立工作：研究相机系统、构建 ARM64 驱动、测试两颗传感器，并排查可能导致整台设备崩溃的问题。进展需要谨慎实验和反复验证。如果本项目对您有帮助，您的支持能让更清晰、更可靠的前后摄像头继续向前发展。

[![通过 DonationAlerts 支持](https://img.shields.io/badge/Support-DonationAlerts-orange?style=for-the-badge)](https://www.donationalerts.com/r/deskcj)

即使**不捐款**也可以帮忙：给仓库点星、分享给其他 nabu 用户，并反馈可复现的结果，包括 Windows 版本、相机应用、光照条件和使用的前置或后置摄像头。这些反馈有助于确定下一步修复的优先级。捐款支持研究，但不保证特定结果或交付日期。感谢您支持独立的 Windows on ARM 项目！

## 免责声明

这是独立的社区项目，**与 Xiaomi、Qualcomm、Microsoft 或 OmniVision 无关联，也未获得其认可**。公司和产品名称仅用于说明兼容硬件和技术；相关权利属于各自的所有者。

这些驱动是采用测试签名的实验性内核软件，按**“现状”**提供，不保证达到 Android 的画质、稳定性或支持水平。使用中可能发生崩溃、设备故障或数据丢失。安装前请备份系统，并保留恢复设备的方法。是否测试及相关风险由您自行承担。本声明并不授权违反第三方工具许可协议进行再分发。
