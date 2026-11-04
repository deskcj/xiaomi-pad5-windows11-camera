param([ValidateSet('ru-RU','en-US')][string]$Locale='en-US',[string]$Test)
$ErrorActionPreference='Stop'
[Threading.Thread]::CurrentThread.CurrentCulture=[Globalization.CultureInfo]::GetCultureInfo($Locale)
[Threading.Thread]::CurrentThread.CurrentUICulture=[Globalization.CultureInfo]::GetCultureInfo($Locale)
if($Test -eq 'Test-WarmStart'){& (Join-Path $PSScriptRoot ($Test+'.ps1')) -Locale $Locale}
else {& (Join-Path $PSScriptRoot ($Test+'.ps1'))}
