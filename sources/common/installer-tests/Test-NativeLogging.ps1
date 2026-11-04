$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$taskRoot=Split-Path -Parent $PSScriptRoot;$root=$taskRoot
. (Join-Path $root 'NativeLogging.ps1')
. (Join-Path $root 'CameraDma.ps1')
$passed=[Collections.Generic.List[string]]::new()
function Assert([bool]$Condition,[string]$Name){if(-not $Condition){throw ('FAIL '+$Name)};$passed.Add($Name)}
function Reject([scriptblock]$Action,[string]$Name){$caught=$false;try{& $Action|Out-Null}catch{$caught=$true};Assert $caught $Name}
$snapshot=Get-NativeLoggingSnapshot
Assert (@($snapshot.values|Where-Object name -eq IHVSTREAMING).Count -eq $(if($snapshot.currentControlSet -eq 1){1}else{2})) 'Current and literal001 control set paths are separately resolved and deduplicated'
Assert (@($snapshot.values|Where-Object {$_.path -match 'Filter Registry::'}).Count -eq 0) 'Registry paths never contain concatenated array items'
$leaf='Software\CodexNativeLoggingTests\'+[guid]::NewGuid().ToString('N');$path='Registry::HKEY_CURRENT_USER\'+$leaf
$hive=[Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::CurrentUser,[Microsoft.Win32.RegistryView]::Registry64)
$key=$hive.CreateSubKey($leaf);$key.Dispose()
try{
 $absent=Read-NativeLogValue $path 'IHVSTREAMING';$plan=Get-NativeLoggingPlan ([pscustomobject]@{values=@($absent)})
 Assert ($plan.updates.Count -eq 1 -and $plan.updates[0].desired -eq 15) 'Absent private startup filter prepares exactly SDK levels0..3'
 Write-NativeLogValue $plan.updates[0]
 Assert ((Read-NativeLogValue $path 'IHVSTREAMING').value -eq 15) 'Real production logging writer persists DWORD15 under isolated HKCU'
 Restore-NativeLogValue $plan.updates[0]
 Assert (-not (Read-NativeLogValue $path 'IHVSTREAMING').exists) 'Rollback restores original missing value'
 New-ItemProperty -LiteralPath $path -Name IHVSTREAMING -Value 256 -PropertyType DWord|Out-Null
 $plan=Get-NativeLoggingPlan ([pscustomobject]@{values=@(Read-NativeLogValue $path 'IHVSTREAMING')})
 Assert ($plan.updates[0].desired -eq 271) 'Existing unrelated logging bits preserved'
 Write-NativeLogValue $plan.updates[0];Restore-NativeLogValue $plan.updates[0]
 Assert ((Read-NativeLogValue $path 'IHVSTREAMING').value -eq 256) 'Original mask restored exactly'
 New-ItemProperty -LiteralPath $path -Name IHVSTREAMING -Value -1 -PropertyType DWord -Force|Out-Null
 Assert ((Get-NativeLoggingPlan ([pscustomobject]@{values=@(Read-NativeLogValue $path 'IHVSTREAMING')})).ready) 'Signed DWORD all-bits mask is already ready and never reduced'
 New-ItemProperty -LiteralPath $path -Name IHVSTREAMING -Value ([int]::MinValue) -PropertyType DWord -Force|Out-Null
 $plan=Get-NativeLoggingPlan ([pscustomobject]@{values=@(Read-NativeLogValue $path 'IHVSTREAMING')})
 Write-NativeLogValue $plan.updates[0]
 Assert ((Read-NativeLogValue $path 'IHVSTREAMING').value -eq -2147483633) 'High unsigned DWORD bit survives signed registry write'
 Restore-NativeLogValue $plan.updates[0]
 Assert ((Read-NativeLogValue $path 'IHVSTREAMING').value -eq [int]::MinValue) 'High-bit mask restored exactly'
 New-ItemProperty -LiteralPath $path -Name EnableLog -Value 0 -PropertyType DWord|Out-Null
 $plan=Get-NativeLoggingPlan ([pscustomobject]@{values=@(Read-NativeLogValue $path 'EnableLog')})
 Assert ($plan.updates[0].desired -eq 1) 'EnableLog configured as boolean, not mask'
 Write-NativeLogValue $plan.updates[0];Restore-NativeLogValue $plan.updates[0]
 New-ItemProperty -LiteralPath $path -Name EnableLog -Value 2 -PropertyType DWord -Force|Out-Null
 Reject {Write-NativeLogValue $plan.updates[0]} 'Stale registry snapshot rejected before writing'
 New-ItemProperty -LiteralPath $path -Name IHVSTREAMING -Value 'unexpected' -PropertyType String -Force|Out-Null
 Reject {Get-NativeLoggingPlan ([pscustomobject]@{values=@(Read-NativeLogValue $path 'IHVSTREAMING')})} 'Unknown logging value type is not overwritten'
}finally{$hive.DeleteSubKey($leaf,$false);$hive.Dispose()}

# Startup gating and injected partial-write rollback use the real orchestrator,
# but fake keys/boot and no actual machine registry changes.
$run=Join-Path $PSScriptRoot ('logging-transaction-'+[guid]::NewGuid().ToString('N').Substring(0,8));New-Item -ItemType Directory -Path $run|Out-Null
$base=$run;$r=[ordered]@{registryActions=$false};$script:currentBoot=[pscustomobject]@{recordId=1;timeUtc='2026-11-04T01:00:00Z'}
function Get-CameraBootIdentity{return $script:currentBoot}
function Save-State{}
function Write-AtomicJson([string]$Path,[string]$Json){[IO.File]::WriteAllText($Path,$Json)}
$script:fakeValues=@([pscustomobject]@{path='fake1';name='EnableLog';keyExists=$true;exists=$true;kind='DWord';value=0},[pscustomobject]@{path='fake2';name='IHVSTREAMING';keyExists=$true;exists=$false;kind='';value=$null})
function Get-NativeLoggingSnapshot{return [pscustomobject]@{values=$script:fakeValues;traceEnable=$null}}
$script:count=0;$script:restored=0
function Write-NativeLogValue([object]$Update){$script:count++;if($script:count -eq 2){throw 'Injected second logging write failure'}}
function Restore-NativeLogValue([object]$Update){$script:restored++}
Reject {Prepare-NativeLogging $true} 'Injected partial registry failure is reported'
Assert ($script:restored -eq 2) 'All attempted writes rollback before leaving failed transaction'
function Write-NativeLogValue([object]$Update){}
Assert ((Prepare-NativeLogging $true) -eq 'PENDING_RESTART') 'Successful startup preparation returns restart requirement'
Assert ((Prepare-NativeLogging $true) -eq 'PENDING_RESTART') 'Same-boot rerun cannot waive startup loading'
$script:currentBoot=[pscustomobject]@{recordId=2;timeUtc='2026-11-04T02:00:00Z'}
$script:fakeValues[0].value=1;$script:fakeValues[1].exists=$true;$script:fakeValues[1].kind='DWord';$script:fakeValues[1].value=15
Assert ((Prepare-NativeLogging $true) -eq 'READY') 'New boot with complete settings permits qualification'
[ordered]@{status='PASS_R23_NATIVE_LOGGING_CONFIG_REGISTRY_ROLLBACK_STARTUP';date='2026-11-04';tests=$passed;realTestHive='HKCU';machineRegistryChanged=$false;liveBootChanged=$false;cameraOpened=$false}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $PSScriptRoot 'native-logging-regression.json') -Encoding UTF8
Write-Output ('PASS_R23_NATIVE_LOGGING_CONFIG_REGISTRY_ROLLBACK_STARTUP count='+$passed.Count)
