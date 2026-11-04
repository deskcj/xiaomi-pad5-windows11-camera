$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$taskRoot=Split-Path -Parent $PSScriptRoot;$root=$taskRoot
. (Join-Path $root 'NativeLogging.ps1');. (Join-Path $root 'CameraDma.ps1')
$passed=[Collections.Generic.List[string]]::new()
function Assert([bool]$Condition,[string]$Name){if(-not $Condition){throw ('FAIL '+$Name)};$passed.Add($Name)}
function Reject([scriptblock]$Action,[string]$Name){$caught=$false;try{& $Action|Out-Null}catch{$caught=$true};Assert $caught $Name}
$leaf='Software\CodexFreshLoggingTests\'+[guid]::NewGuid().ToString('N')
$hive=[Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::CurrentUser,[Microsoft.Win32.RegistryView]::Registry64)
$hive.CreateSubKey($leaf+'\Services').Dispose();$hive.CreateSubKey($leaf+'\Session Manager').Dispose()
$anchor='Registry::HKEY_CURRENT_USER\'+$leaf+'\Services';$session='Registry::HKEY_CURRENT_USER\'+$leaf+'\Session Manager'
$service=$anchor+'\qccamavs';$logging=$service+'\Logging';$filter=$session+'\Debug Print Filter'
function Read-TestSnapshot{[pscustomobject]@{values=@((Read-NativeLogValue $logging 'EnableLog' $anchor),(Read-NativeLogValue $filter 'IHVSTREAMING' $session));traceEnable=(Read-NativeLogValue $logging 'TraceEnable')}}
try{
 $fixture=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'fixtures\missing-logging.json') -Raw|ConvertFrom-Json
 Assert (-not $fixture.driverActions -and $fixture.cameraDma.status -eq 'PENDING_RESTART' -and -not $fixture.nativeLogging.values[0].keyExists) 'Recorded fresh friend failure happened before binding with missing service logging'
 $plan=Get-NativeLoggingPlan (Read-TestSnapshot)
 Assert ($plan.updates.Count -eq 2 -and $plan.updates[0].missingKeys.Count -eq 2 -and $plan.updates[1].missingKeys.Count -eq 1) 'Missing service/logging/filter keys included in pre-write plan'
 foreach($update in $plan.updates){Write-NativeLogValue $update}
 Assert ((Read-NativeLogValue $logging 'EnableLog').value -eq 1 -and (Read-NativeLogValue $filter 'IHVSTREAMING').value -eq 15) 'Real HKCU fresh install creates all exact keys and values'
 Assert ((Get-NativeLoggingPlan (Read-TestSnapshot)).ready) 'Fresh key preparation readback is ready'
 for($i=$plan.updates.Count-1;$i -ge 0;$i--){Restore-NativeLogValue $plan.updates[$i]}
 Assert (-not(Test-Path -LiteralPath $service) -and -not(Test-Path -LiteralPath $filter)) 'Rollback removes only newly created empty descendants'
 Assert ((Test-Path -LiteralPath $anchor) -and (Test-Path -LiteralPath $session)) 'Existing Services and Session Manager anchors preserved'
 New-Item -Path $service|Out-Null;New-ItemProperty -LiteralPath $service -Name Start -Value 3 -PropertyType DWord|Out-Null
 $plan=Get-NativeLoggingPlan (Read-TestSnapshot);Write-NativeLogValue $plan.updates[0];Restore-NativeLogValue $plan.updates[0]
 Assert ((Get-Item -LiteralPath $service).GetValue('Start') -eq 3 -and -not(Test-Path -LiteralPath $logging)) 'Existing service configuration untouched by logging create/rollback'
 Remove-ItemProperty -LiteralPath $service -Name Start;Remove-Item -LiteralPath $service
 $bad=Read-NativeLogValue ($anchor+'\unrelated\Logging') 'EnableLog' $anchor
 Reject {Get-NativeLoggingPlan ([pscustomobject]@{values=@($bad)})} 'Unknown descendant rejected before creating anything'
 $bad=Read-NativeLogValue ($anchor+'\missing\qccamavs\Logging') 'EnableLog' ($anchor+'\missing')
 Reject {Get-NativeLoggingPlan ([pscustomobject]@{values=@($bad)})} 'Missing system-level anchor never fabricated'
 $plan=Get-NativeLoggingPlan (Read-TestSnapshot);New-Item -Path $service|Out-Null
 Reject {Write-NativeLogValue $plan.updates[0]} 'Concurrent service key creation is not claimed by installer'
 Assert (-not(Test-Path -LiteralPath $logging)) 'Concurrent-key rejection creates no further child'
 Remove-Item -LiteralPath $service
 $plan=Get-NativeLoggingPlan (Read-TestSnapshot);New-Item -Path $service|Out-Null;New-Item -Path $logging|Out-Null
 Reject {Write-NativeLogValue $plan.updates[0]} 'Concurrent target creation cannot silently receive our value'
 Remove-Item -LiteralPath $logging;Remove-Item -LiteralPath $service
 $plan=Get-NativeLoggingPlan (Read-TestSnapshot);Write-NativeLogValue $plan.updates[0]
 New-ItemProperty -LiteralPath $logging -Name ConcurrentValue -Value 42 -PropertyType DWord|Out-Null
 Reject {Restore-NativeLogValue $plan.updates[0]} 'Concurrent contents prevent deleting new keys'
 Assert ((Get-Item -LiteralPath $logging).GetValue('ConcurrentValue') -eq 42 -and -not(Read-NativeLogValue $logging 'EnableLog').exists) 'Rollback preserves other writer data while restoring our value'
 Remove-ItemProperty -LiteralPath $logging -Name ConcurrentValue;Remove-Item -LiteralPath $logging;Remove-Item -LiteralPath $service
 # Execute production transaction against real isolated HKCU keys and fake boot.
 $run=Join-Path $PSScriptRoot ('fresh-transaction-'+[guid]::NewGuid().ToString('N').Substring(0,8));New-Item -ItemType Directory -Path $run|Out-Null
 $base=$run;$r=[ordered]@{registryActions=$false};$script:testBoot=[pscustomobject]@{recordId=1;timeUtc='2026-11-04T01:00:00Z'}
 function Save-State{}
 function Write-AtomicJson([string]$Path,[string]$Json){[IO.File]::WriteAllText($Path,$Json)}
 function Get-CameraBootIdentity{$script:testBoot}
 function Get-NativeLoggingSnapshot{Read-TestSnapshot}
 $script:productionWriter=${function:Write-NativeLogValue};$script:writeIndex=0
 function Write-NativeLogValue([object]$Update){$script:writeIndex++;if($script:writeIndex -eq 2){throw 'Injected second-write failure'};& $script:productionWriter $Update}
 Reject {Prepare-NativeLogging $true} 'Injected failure after creating service keys rejects transaction'
 Assert (-not(Test-Path -LiteralPath $service) -and -not(Test-Path -LiteralPath $filter)) 'Partial production transaction restores original missing key tree'
 $journal=Get-Content -LiteralPath (Join-Path $run 'CameraDma\logging-pending.json') -Raw|ConvertFrom-Json
 Assert ($journal.status -eq 'FAILED_RESTORED') 'Failed transaction accurately records restored state'
 function Write-NativeLogValue([object]$Update){& $script:productionWriter $Update}
 Assert ((Prepare-NativeLogging $true) -eq 'PENDING_RESTART') 'Retry creates missing friend keys and requires restart'
 Assert ((Prepare-NativeLogging $true) -eq 'PENDING_RESTART') 'Same-boot retry cannot bypass startup requirement'
 $script:testBoot=[pscustomobject]@{recordId=2;timeUtc='2026-11-04T02:00:00Z'}
 Assert ((Prepare-NativeLogging $true) -eq 'READY') 'New boot admits prepared fresh keys'
 # Real Setup bookkeeping fragment: pending DMA remains visible if logging fails.
 $setup=[IO.File]::ReadAllText((Join-Path $root 'Setup.ps1'))
 $start=$setup.IndexOf(' $dma=Prepare-CameraDma ');$end=$setup.IndexOf(' $logging=Prepare-NativeLogging ',$start)
 Assert ($start -ge 0 -and $end -gt $start) 'Exact public DMA bookkeeping fragment found'
 $Mode='Install';$dmaPlan=$null;$r=[ordered]@{registryActions=$false;rebootRequired=$false}
 function Prepare-CameraDma{param($Plan,$AllowWrite)$fixture.cameraDma}
 Invoke-Expression $setup.Substring($start,$end-$start)
 Assert ($r.registryActions -and $r.rebootRequired) 'Pending DMA change remains marked even before later logging failure'
}finally{
 if(-not $leaf.StartsWith('Software\CodexFreshLoggingTests\',[StringComparison]::Ordinal)){throw 'Unexpected test cleanup root'}
 $hive.DeleteSubKeyTree($leaf,$false);$hive.Dispose()
}
[ordered]@{status='PASS_R24_FRESH_FRIEND_KEYS_TRANSACTION_ROLLBACK_RESTART';tests=$passed;realTestHive='HKCU';liveCameraOpened=$false;machineRegistryChanged=$false}|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $PSScriptRoot 'fresh-logging-regression.json') -Encoding UTF8
'PASS_R24_FRESH_FRIEND_KEYS_TRANSACTION_ROLLBACK_RESTART count='+$passed.Count
