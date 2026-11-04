$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
if($PSVersionTable.PSEdition -eq 'Desktop'){$env:PSModulePath=(Join-Path $PSHOME 'Modules')+';'+[Environment]::GetEnvironmentVariable('PSModulePath',[EnvironmentVariableTarget]::Machine)}
$taskRoot=Split-Path -Parent $PSScriptRoot
. (Join-Path $taskRoot 'CameraDma.ps1')
$scratchRelative='Software\CodexCameraInstallerTests\'+[guid]::NewGuid().ToString('N')
if($scratchRelative -notmatch '^Software\\CodexCameraInstallerTests\\[a-f0-9]{32}$'){throw 'Invalid isolated registry test path'}
$scratchPath='Registry::HKEY_CURRENT_USER\'+$scratchRelative
$hive=[Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::CurrentUser,[Microsoft.Win32.RegistryView]::Registry64)
$providerKey=$null;$passed=@()
function Assert([bool]$Condition,[string]$Name){if(-not $Condition){throw ('FAIL '+$Name)};$script:passed+=@($Name)}
try{
 $key=$hive.CreateSubKey($scratchRelative)
 try{$key.SetValue('MAP',(Convert-CameraDmaHex 'ff20800020'),[Microsoft.Win32.RegistryValueKind]::Binary)}finally{$key.Dispose()}
 $providerKey=Get-Item -LiteralPath $scratchPath
 $readOnlyFailure=$false
 try{$providerKey.SetValue('MAP',(Convert-CameraDmaHex 'ff20800020ff21000020'),[Microsoft.Win32.RegistryValueKind]::Binary)}catch{$readOnlyFailure=$_.Exception.Message -match 'Cannot write to the registry key|UnauthorizedAccess'}
 Assert $readOnlyFailure 'Reproduced original R2 provider read-only write failure'
 Set-CameraDmaMap $scratchPath 'ff20800020' 'ff20800020ff21000020'
 $actual=[BitConverter]::ToString([byte[]]$providerKey.GetValue('MAP')).Replace('-','').ToLowerInvariant()
 Assert ($actual -eq 'ff20800020ff21000020') 'Real production setter opens writable key and persists exact binary bytes'
 $staleRejected=$false
 try{Set-CameraDmaMap $scratchPath 'ff20800020' 'ff20c00000'}catch{$staleRejected=$_.Exception.Message -match 'changed concurrently'}
 Assert $staleRejected 'Stale expected value refused before write'
 Assert ([BitConverter]::ToString([byte[]]$providerKey.GetValue('MAP')).Replace('-','').ToLowerInvariant() -eq $actual) 'Rejected operation leaves bytes unchanged'
 Set-CameraDmaMap $scratchPath 'ff20800020ff21000020' 'ff20800020'
 Assert ([BitConverter]::ToString([byte[]]$providerKey.GetValue('MAP')).Replace('-','').ToLowerInvariant() -eq 'ff20800020') 'Same writable setter restores original bytes'
 $key=Open-CameraDmaWritableKey $scratchPath
 try{$key.SetValue('MAP','different-type',[Microsoft.Win32.RegistryValueKind]::String)}finally{$key.Dispose()}
 $typeRejected=$false;try{Set-CameraDmaMap $scratchPath 'ff20800020' 'ff20c00000'}catch{$typeRejected=$_.Exception.Message -match 'type changed'}
 Assert $typeRejected 'Non-binary MAP rejected'
 $missingRejected=$false;try{$key=Open-CameraDmaWritableKey ($scratchPath+'\missing');$key.Dispose()}catch{$missingRejected=$_.Exception.Message -match 'key missing'}
 Assert $missingRejected 'Missing key is not automatically created'
 Assert (-not(Test-Path -LiteralPath ($scratchPath+'\missing'))) 'No unintended registry child created'
}finally{
 if($providerKey){$providerKey.Dispose()}
 # Only this GUID leaf, under this fixed HKCU test root, has been created.
 $hive.DeleteSubKey($scratchRelative,$false);$hive.Dispose()
}
Assert (-not(Test-Path -LiteralPath $scratchPath)) 'Isolated test key removed'
[ordered]@{status='PASS_R22_REAL_REGISTRY_WRITE_READBACK_RESTORE';powershell=[string]$PSVersionTable.PSVersion;tests=$passed;testHive='HKEY_CURRENT_USER';machineRegistryChanged=$false;cameraOpened=$false;driverChanged=$false;scratchRemoved=$true}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $PSScriptRoot 'registry-write-regression.json') -Encoding UTF8
Write-Output ('PASS_R22_REAL_REGISTRY_WRITE_READBACK_RESTORE count='+$passed.Count)
