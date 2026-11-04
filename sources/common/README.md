# Shared installer and probe sources / Общие исходники

Setup.cmd / Setup.ps1 and helper scripts match the public driver packages.
The driver ZIP also supplies Profile.json, signed Payload, runtime binaries,
public certificates, Tools and MANIFEST-SHA256.json. This source tree alone
is not an installation package. Use the branch driver ZIP for installation.

The eight retained regression suites use synthetic or reduced numeric fixtures.
They do not install drivers or open cameras. Registry tests create isolated HKCU
test keys and restore them. Run each in a separate Windows PowerShell 5.1 process:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File installer-tests/Run-Locale.ps1 -Locale ru-RU -Test Test-WarmStart
```

Replace Test-WarmStart with Test-InstallerR2, Test-DmaTransaction,
Test-CameraSelection, Test-NativeLogging, Test-FreshLoggingKeys,
Test-RegistryWrite or Test-RestartRecovery. Repeat with en-US.
Reports and isolated mock files are written beside the tests. Private traces,
camera identifiers, photographs and signing keys are not included.

Русский: установка выполняется из ZIP драйвера, где есть Profile/Payload/Tools
и манифест. Здесь исходники и восемь регрессионных наборов без открытия камер
и установки драйвера. Тесты реестра создают собственные HKCU-ключи и восстанавливают
их; запускайте каждый набор отдельно в Windows PowerShell 5.1 по примеру выше.
