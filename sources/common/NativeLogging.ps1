Set-StrictMode -Version Latest
function Read-NativeLogValue([string]$Path,[string]$Name,[string]$Anchor='') {
 $key=Get-Item -LiteralPath $Path -ErrorAction SilentlyContinue
 $exists=$key -and $key.GetValueNames() -contains $Name
 [pscustomobject]@{path=$Path;name=$Name;anchor=$Anchor;keyExists=[bool]$key;exists=[bool]$exists;kind=$(if($exists){[string]$key.GetValueKind($Name)}else{''});value=$(if($exists){$key.GetValue($Name)}else{$null})}
}
function Get-NativeLoggingSnapshot {
 $select=Get-Item -LiteralPath 'Registry::HKEY_LOCAL_MACHINE\SYSTEM\Select'
 $current=[int]$select.GetValue('Current',0);if($current -lt 1 -or $current -gt 999){throw 'Cannot resolve active control set for native logging'}
 $paths=@(('Registry::HKEY_LOCAL_MACHINE\SYSTEM\'+('ControlSet{0:D3}' -f $current)+'\Control\Session Manager\Debug Print Filter'),'Registry::HKEY_LOCAL_MACHINE\SYSTEM\ControlSet001\Control\Session Manager\Debug Print Filter')|Sort-Object -Unique
 $values=@(Read-NativeLogValue 'Registry::HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Services\qccamavs\Logging' 'EnableLog' 'Registry::HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Services')
 foreach($path in $paths){$values+=@(Read-NativeLogValue $path 'IHVSTREAMING' (Split-Path -Parent $path))}
 [pscustomobject]@{currentControlSet=$current;values=$values;traceEnable=(Read-NativeLogValue 'Registry::HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Services\qccamavs\Logging' 'TraceEnable')}
}
function Get-NativeLogMissingKeys([object]$Entry) {
 if($Entry.keyExists){return}
 if(-not $Entry.PSObject.Properties['anchor'] -or -not $Entry.anchor){throw 'Native logging creation anchor missing'}
 $anchor=$Entry.anchor.TrimEnd('\')
 if(-not $Entry.path.StartsWith($anchor+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Native logging path outside creation anchor'}
 $suffix=$Entry.path.Substring($anchor.Length+1)
 $expected=if($Entry.name -eq 'EnableLog'){'qccamavs\Logging'}elseif($Entry.name -eq 'IHVSTREAMING'){'Debug Print Filter'}else{throw 'Unknown native logging key'}
 if($suffix -ine $expected -or -not(Test-Path -LiteralPath $anchor)){throw 'Native logging base key missing or unexpected path'}
 $path=$anchor
 foreach($part in $suffix.Split('\')){$path+='\'+$part;if(-not(Test-Path -LiteralPath $path)){Write-Output $path}}
}
function Get-NativeLoggingPlan([object]$Snapshot) {
 $updates=@()
 foreach($entry in $Snapshot.values){
  if($entry.exists -and $entry.kind -ne 'DWord'){throw ('Unexpected native logging value type: '+$entry.path+' '+$entry.name)}
  if($entry.name -eq 'EnableLog'){$desired=[long]1}
  elseif($entry.name -eq 'IHVSTREAMING'){$original=if($entry.exists){([long]$entry.value -band 4294967295L)}else{0L};$desired=$original -bor 15L}
  else{throw 'Unrecognized native logging value'}
  $old=if($entry.exists){([long]$entry.value -band 4294967295L)}else{-1L}
  if(-not $entry.exists -or $old -ne $desired){$updates+=@([pscustomobject]@{before=$entry;desired=$desired;missingKeys=@(Get-NativeLogMissingKeys $entry);createdKeys=@()})}
 }
 [pscustomobject]@{snapshot=$Snapshot;updates=$updates;ready=($updates.Count -eq 0);requiredKernelComponent=81;requiredLevels=@(0,1,2,3);requiredMask=15}
}
function Write-NativeLogValue([object]$Update) {
 $before=$Update.before;$now=Read-NativeLogValue $before.path $before.name
 if($now.keyExists -ne $before.keyExists -or $now.exists -ne $before.exists -or ($now.exists -and ($now.kind -ne $before.kind -or [long]$now.value -ne [long]$before.value))){throw 'Native logging key or value changed concurrently; write refused'}
 if(-not $now.keyExists){
  foreach($path in $Update.missingKeys){
   if(Test-Path -LiteralPath $path){throw 'Native logging key appeared concurrently; creation refused'}
   $parent=Split-Path -Parent $path;if(-not(Test-Path -LiteralPath $parent)){throw 'Native logging creation base disappeared'}
   New-Item -Path $path -ErrorAction Stop|Out-Null
   $Update.createdKeys+=@($path)
  }
  if(-not(Test-Path -LiteralPath $before.path)){throw 'Native logging target key missing after preparation'}
 }
 $signed=if($Update.desired -gt [int]::MaxValue){[int]($Update.desired-4294967296L)}else{[int]$Update.desired}
 New-ItemProperty -LiteralPath $before.path -Name $before.name -Value $signed -PropertyType DWord -Force|Out-Null
 $after=Read-NativeLogValue $before.path $before.name
 if(-not $after.exists -or $after.kind -ne 'DWord' -or ([long]$after.value -band 4294967295L) -ne $Update.desired){throw 'Native logging write readback failed'}
}
function Restore-NativeLogValue([object]$Update) {
 $before=$Update.before;$now=Read-NativeLogValue $before.path $before.name
 $restored=$now.exists -eq $before.exists -and (-not $now.exists -or ($now.kind -eq $before.kind -and [long]$now.value -eq [long]$before.value))
 if(-not $restored){
  if(-not $now.exists -or $now.kind -ne 'DWord' -or ([long]$now.value -band 4294967295L) -ne $Update.desired){throw 'Concurrent logging value change prevents rollback'}
  if($before.exists){New-ItemProperty -LiteralPath $before.path -Name $before.name -Value $before.value -PropertyType DWord -Force|Out-Null}
  else{Remove-ItemProperty -LiteralPath $before.path -Name $before.name}
 }
 $after=Read-NativeLogValue $before.path $before.name
 if($after.exists -ne $before.exists -or ($after.exists -and ($after.kind -ne $before.kind -or [long]$after.value -ne [long]$before.value))){throw 'Native logging rollback readback failed'}
 for($i=$Update.createdKeys.Count-1;$i -ge 0;$i--){
  $path=$Update.createdKeys[$i];$key=Get-Item -LiteralPath $path -ErrorAction SilentlyContinue
  if(-not $key){continue}
  if($key.GetValueNames().Count -ne 0 -or $key.GetSubKeyNames().Count -ne 0){throw 'New logging key contains concurrent data; rollback preserves it'}
  Remove-Item -LiteralPath $path -ErrorAction Stop
  if(Test-Path -LiteralPath $path){throw 'Native logging key rollback readback failed'}
 }
}
function Prepare-NativeLogging([bool]$AllowWrite) {
 $path=Join-Path $base 'CameraDma\logging-pending.json';$boot=Get-CameraBootIdentity
 $plan=Get-NativeLoggingPlan (Get-NativeLoggingSnapshot);$r.nativeLogging=$plan.snapshot;Save-State
 if(Test-Path -LiteralPath $path){
  $record=Get-Content -LiteralPath $path -Raw|ConvertFrom-Json
  if($record.status -eq 'FAILED_ROLLBACK_REQUIRED'){throw 'Native logging transaction requires recovery; retain current device state'}
  if($record.status -in 'APPLIED_PENDING_RESTART','PREPARED_NEEDS_RESTART' -and -not(Test-CameraNewBoot $record.boot $boot)){return 'PENDING_RESTART'}
 }
 if($plan.ready){return 'READY'}
 if(-not $AllowWrite){throw 'Native close logging startup configuration is incomplete. Run Install-QualcommISP.cmd and restart Windows when instructed.'}
 $parent=Split-Path -Parent $path;New-Item -ItemType Directory -Path $parent -Force|Out-Null
 $backup=Join-Path $parent ('logging-backup-'+[guid]::NewGuid().ToString('N')+'.json')
 $record=[ordered]@{schema=2;status='PREPARED_NEEDS_RESTART';boot=$boot;before=$plan.snapshot;updates=$plan.updates;backup=$backup;rebootInvoked=$false}
 Write-AtomicJson $backup ($record|ConvertTo-Json -Depth 10)
 Write-AtomicJson $path ($record|ConvertTo-Json -Depth 10)
 $attempted=@()
 try{
  foreach($update in $plan.updates){$attempted+=@($update);Write-NativeLogValue $update}
  $record.status='APPLIED_PENDING_RESTART';Write-AtomicJson $path ($record|ConvertTo-Json -Depth 10)
  $r.registryActions=$true;Save-State
 }catch{
  $original=$_.Exception.Message;$record.status='FAILED_RESTORED'
  for($i=$attempted.Count-1;$i -ge 0;$i--){try{Restore-NativeLogValue $attempted[$i]}catch{$record.status='FAILED_ROLLBACK_REQUIRED'}}
  Write-AtomicJson $path ($record|ConvertTo-Json -Depth 10)
  throw ($original+'; native logging transaction '+$record.status)
 }
 return 'PENDING_RESTART'
}
