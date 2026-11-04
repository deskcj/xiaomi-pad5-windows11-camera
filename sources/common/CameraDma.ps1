# Import defines functions only. No registry writes or device operations on import.
function Convert-CameraDmaHex([string]$Hex) {
 if($Hex -notmatch '^(?:[0-9a-fA-F]{2})+$'){throw 'Invalid camera DMA binary value'}
 $bytes=New-Object byte[] ($Hex.Length/2)
 for($i=0;$i -lt $bytes.Length;$i++){$bytes[$i]=[Convert]::ToByte($Hex.Substring($i*2,2),16)}
 return ,$bytes
}
function Get-CameraDmaPlanFromSnapshot($Snapshot) {
 $expectedHash='2fa1883f3252d2e50d63911fc9ff435f88e75210f429e86686e395e0827e1299'
 if($Snapshot.service -ine 'qcsmmu' -or $Snapshot.sha256 -ine $expectedHash -or $Snapshot.chipSetVersion -ne 2 -or $Snapshot.problem -ne 0){throw 'Unsupported native SMMU configuration. No SMMU driver or mapping has been changed.'}
 $specs=@(
  [pscustomobject]@{suffix='0\S2CB\0x01\0x04\0x16';before='ff20800020';after='ff20800020ff21000020'},
  [pscustomobject]@{suffix='0\S2CB\0x01\0x04\0x11';before='ff20c00000';after='ff20c00000ff21400000'}
 )
 $rows=@();$updates=@()
 foreach($spec in $specs){
  $match=@($Snapshot.maps|Where-Object suffix -eq $spec.suffix)
  if($match.Count -ne 1 -or $match[0].hex -notin $spec.before,$spec.after){throw ('Unrecognized camera MAP baseline: '+$spec.suffix+'. No changes made.')}
  $row=[ordered]@{suffix=$spec.suffix;path=($Snapshot.driverKey+'\'+$spec.suffix);beforeHex=$match[0].hex;afterHex=$spec.after;changed=($match[0].hex -ne $spec.after)}
  $rows+=@($row);if($row.changed){$updates+=@($row)}
 }
 # Check the added three StreamIDs against ALL native S2CB groups.
 # Only identical routing to the intended context is acceptable.
 foreach($map in $Snapshot.maps){
  $bytes=Convert-CameraDmaHex $map.hex
  if($bytes.Length%5 -ne 0){throw ('Malformed native S2CB MAP: '+$map.suffix)}
  for($i=0;$i -lt $bytes.Length;$i+=5){
   $sid=([int]$bytes[$i+1] -shl 8) -bor [int]$bytes[$i+2]
   $mask=([int]$bytes[$i+3] -shl 8) -bor [int]$bytes[$i+4]
   foreach($target in @(@(0x2100,'0\S2CB\0x01\0x04\0x16'),@(0x2120,'0\S2CB\0x01\0x04\0x16'),@(0x2140,'0\S2CB\0x01\0x04\0x11'))){
    if((($sid -bxor $target[0]) -band (0x7fff -bxor ($mask -band 0x7fff))) -eq 0 -and $map.suffix -ine $target[1]){throw ('Camera StreamID conflicts with another SMMU group: '+$map.suffix+'. No changes made.')}
   }
  }
 }
 [pscustomobject]@{status=$(if($updates.Count){'CAMERA_DMA_STARTUP_UPDATE_REQUIRED'}else{'CAMERA_DMA_REGISTRY_READY'});device=$Snapshot.device;driverKey=$Snapshot.driverKey;sha256=$Snapshot.sha256;rows=$rows;updates=$updates;loadedHardwareStateRead=$false}
}
function Get-NativeCameraDmaSnapshot {
 $device='ACPI\VEN_QCOM&DEV_0509&SUBSYS_MTPA8150&REV_0002\0'
 $node=Get-Item -LiteralPath ('Registry::HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Enum\'+$device)
 $driver=[string]$node.GetValue('Driver','')
 if($driver -notmatch '^\{4d36e97d-e325-11ce-bfc1-08002be10318\}\\[0-9]+$'){throw 'Unsupported native SMMU software key'}
 $driverKey='Registry::HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\Class\'+$driver
 $key=Get-Item -LiteralPath $driverKey
 $service=[string]$node.GetValue('Service','')
 $image=Service-Image $service
 $maps=@()
 $mapRoot=Join-Path $driverKey '0\S2CB'
 foreach($mapKey in @(Get-ChildItem -LiteralPath $mapRoot -Recurse)){
  if($mapKey.GetValueNames() -notcontains 'MAP'){continue}
  if($mapKey.GetValueKind('MAP') -ne [Microsoft.Win32.RegistryValueKind]::Binary){throw 'Unexpected SMMU MAP registry type'}
  $suffix=$mapKey.Name.Substring(('HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\Class\'+$driver+'\').Length)
  $value=[byte[]]$mapKey.GetValue('MAP')
  $maps+=@([pscustomobject]@{suffix=$suffix;hex=[BitConverter]::ToString($value).Replace('-','').ToLowerInvariant()})
 }
 [pscustomobject]@{device=$device;driverKey=$driverKey;service=$service;sha256=(Get-FileHash -LiteralPath $image).Hash.ToLowerInvariant();chipSetVersion=$key.GetValue('ChipSetVersion',0);problem=(Problem $device);maps=$maps}
}
function Get-CameraBootIdentity {
 $event=Get-WinEvent -FilterHashtable @{LogName='System';ProviderName='Microsoft-Windows-Kernel-General';Id=12} -MaxEvents 1 -ErrorAction Stop
 [pscustomobject]@{recordId=[long]$event.RecordId;timeUtc=$event.TimeCreated.ToUniversalTime().ToString('o')}
}
function Test-CameraNewBoot($Before,$Now) {
 return ($Before.recordId -ne $Now.recordId -or $Before.timeUtc -ne $Now.timeUtc)
}
function Open-CameraDmaWritableKey([string]$Path) {
 # Registry provider Get-Item returns a read-only RegistryKey. Open explicitly
 # writable instead. This does not change ownership or the key's access rules.
 $machine='Registry::HKEY_LOCAL_MACHINE\';$user='Registry::HKEY_CURRENT_USER\'
 if($Path.StartsWith($machine,[StringComparison]::OrdinalIgnoreCase)){$hiveName=[Microsoft.Win32.RegistryHive]::LocalMachine;$subkey=$Path.Substring($machine.Length)}
 elseif($Path.StartsWith($user,[StringComparison]::OrdinalIgnoreCase)){$hiveName=[Microsoft.Win32.RegistryHive]::CurrentUser;$subkey=$Path.Substring($user.Length)}
 else{throw 'Unsupported registry path for camera MAP update'}
 if(-not $subkey){throw 'A concrete existing registry subkey is required'}
 $hive=[Microsoft.Win32.RegistryKey]::OpenBaseKey($hiveName,[Microsoft.Win32.RegistryView]::Registry64)
 try{
  $rights=[Security.AccessControl.RegistryRights]::QueryValues -bor [Security.AccessControl.RegistryRights]::SetValue
  $key=$hive.OpenSubKey($subkey,[Microsoft.Win32.RegistryKeyPermissionCheck]::ReadWriteSubTree,$rights)
  if($null -eq $key){throw ('Registry key missing; no key created: '+$Path)}
  return $key
 }finally{$hive.Dispose()}
}
function Set-CameraDmaMap([string]$Path,[string]$Expected,[string]$Proposed) {
 $key=Open-CameraDmaWritableKey $Path
 try{
  if($key.GetValueKind('MAP') -ne [Microsoft.Win32.RegistryValueKind]::Binary){throw 'MAP type changed concurrently'}
  $current=[BitConverter]::ToString([byte[]]$key.GetValue('MAP')).Replace('-','').ToLowerInvariant()
  if($current -ine $Expected){throw 'MAP changed concurrently; refusing to overwrite'}
  $key.SetValue('MAP',(Convert-CameraDmaHex $Proposed),[Microsoft.Win32.RegistryValueKind]::Binary)
  $key.Flush()
  $actual=[BitConverter]::ToString([byte[]]$key.GetValue('MAP')).Replace('-','').ToLowerInvariant()
  if($actual -ine $Proposed){throw 'Camera MAP readback mismatch'}
 }finally{$key.Dispose()}
}
function Prepare-CameraDma($Plan,[bool]$AllowWrite) {
 $pendingPath=Join-Path $base 'CameraDma\pending.json'
 $boot=Get-CameraBootIdentity
 if(Test-Path -LiteralPath $pendingPath){
  $pending=Get-Content -LiteralPath $pendingPath -Raw|ConvertFrom-Json
  if($pending.status -eq 'APPLIED_PENDING_RESTART'){
   if($pending.device -ine $Plan.device -or $pending.driverKey -ine $Plan.driverKey -or $pending.sha256 -ine $Plan.sha256){throw 'Pending camera DMA journal belongs to another configuration'}
   if($Plan.updates.Count){throw 'Pending DMA registry values changed. Preserve journal for recovery.'}
   if(-not(Test-CameraNewBoot $pending.boot $boot)){return [pscustomobject]@{status='PENDING_RESTART';journal=$pendingPath;registryChanged=$false}}
   # A genuine subsequent kernel startup and exact readback satisfy the startup gate.
   # This is not a direct MMIO readback of hardware SMR/S2CR.
   if($AllowWrite){$pending.status='REGISTRY_READY_AFTER_NEW_BOOT';$pending|Add-Member -NotePropertyName validatedBoot -NotePropertyValue $boot -Force;Write-AtomicJson $pendingPath ($pending|ConvertTo-Json -Depth 10)}
  }elseif($pending.status -eq 'APPLY_INTERRUPTED_REVIEW_REQUIRED'){throw 'Previous DMA transaction was interrupted; preserve its backup for recovery.'}
 }
 if(-not $Plan.updates.Count){return [pscustomobject]@{status='READY';registryChanged=$false;journal=$pendingPath}}
 if(-not $AllowWrite){throw 'Missing camera DMA startup routing. Run Setup.cmd; then restart Windows when instructed.'}
 Assert-Idle
 # Confirm write access before creating a pending transaction journal.
 foreach($row in $Plan.updates){$writeKey=Open-CameraDmaWritableKey $row.path;$writeKey.Dispose()}
 if((Get-PSDrive C).Free -lt 50MB){throw 'Free at least 50 MiB on C: to preserve the DMA recovery journal'}
 $backup=Join-Path $base ('CameraDma\backup-'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[guid]::NewGuid().ToString('N').Substring(0,8))
 New-Item -ItemType Directory -Path $backup|Out-Null
 $journal=[ordered]@{schema=1;status='APPLY_INTERRUPTED_REVIEW_REQUIRED';device=$Plan.device;driverKey=$Plan.driverKey;sha256=$Plan.sha256;boot=$boot;rows=$Plan.rows;created=(Get-Date -Format o);backup=$backup;driverRebind=$false;liveSmmuReset=$false;rebootInvoked=$false}
 Write-AtomicJson (Join-Path $backup 'before.json') ($journal|ConvertTo-Json -Depth 10)
 $regLines=@('Windows Registry Editor Version 5.00','')
 foreach($row in $Plan.rows){
  $nativePath=$row.path.Replace('Registry::HKEY_LOCAL_MACHINE\','HKEY_LOCAL_MACHINE\')
  $hexBytes=[regex]::Matches($row.beforeHex,'..')|ForEach-Object Value
  $regLines+=@(('['+$nativePath+']'),('"MAP"=hex:'+($hexBytes -join ',')),'')
 }
 $regLines|Set-Content -LiteralPath (Join-Path $backup 'before-MAP.reg') -Encoding Unicode
 Write-AtomicJson $pendingPath ($journal|ConvertTo-Json -Depth 10)
 try{
  foreach($row in $Plan.updates){Set-CameraDmaMap $row.path $row.beforeHex $row.afterHex}
  $after=Get-CameraDmaPlanFromSnapshot (Get-NativeCameraDmaSnapshot)
  if($after.updates.Count){throw 'Incomplete camera DMA transaction'}
  $journal.status='APPLIED_PENDING_RESTART'
  Write-AtomicJson $pendingPath ($journal|ConvertTo-Json -Depth 10)
 }catch{
  $rollbackErrors=@()
  foreach($row in @($Plan.updates)){
   try{
    $key=Get-Item -LiteralPath $row.path
    $hex=[BitConverter]::ToString([byte[]]$key.GetValue('MAP')).Replace('-','').ToLowerInvariant()
    if($hex -eq $row.afterHex){Set-CameraDmaMap $row.path $row.afterHex $row.beforeHex}elseif($hex -ne $row.beforeHex){throw 'Concurrent edit prevents rollback'}
   }catch{$rollbackErrors+=@($_.Exception.Message)}
  }
  $journal.status=$(if($rollbackErrors.Count){'APPLY_INTERRUPTED_REVIEW_REQUIRED'}else{'FAILED_ORIGINAL_VALUES_RESTORED'});$journal.rollbackErrors=$rollbackErrors
  Write-AtomicJson $pendingPath ($journal|ConvertTo-Json -Depth 10)
  throw
 }
 [pscustomobject]@{status='PENDING_RESTART';registryChanged=$true;journal=$pendingPath;backup=$backup}
}
