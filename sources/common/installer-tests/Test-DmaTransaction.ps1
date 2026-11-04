$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
if($PSVersionTable.PSEdition -eq 'Desktop'){$env:PSModulePath=(Join-Path $PSHOME 'Modules')+';'+[Environment]::GetEnvironmentVariable('PSModulePath',[EnvironmentVariableTarget]::Machine)}
$taskRoot=Split-Path -Parent $PSScriptRoot
. (Join-Path $taskRoot 'CameraDma.ps1')
function Assert-Idle{}
function Get-PSDrive([string]$Name){return [pscustomobject]@{Name=$Name;Free=2GB}}
function Open-CameraDmaWritableKey([string]$Path){$key=[pscustomobject]@{};$key|Add-Member -MemberType ScriptMethod -Name Dispose -Value {};return $key}
function Get-CameraBootIdentity{return [pscustomobject]@{recordId=10;timeUtc='2026-11-04T00:00:00Z'}}
function Write-AtomicJson([string]$Path,[string]$Json){[IO.File]::WriteAllText($Path,$Json,[Text.UTF8Encoding]::new($false))}
$base=Join-Path $PSScriptRoot ('transaction-mock-'+[guid]::NewGuid().ToString('N').Substring(0,8))
$snapshot=[pscustomobject]@{device='test-only';driverKey='Registry::unit-test';service='qcsmmu';sha256='2fa1883f3252d2e50d63911fc9ff435f88e75210f429e86686e395e0827e1299';chipSetVersion=2;problem=0;maps=@([pscustomobject]@{suffix='0\S2CB\0x01\0x04\0x16';hex='ff20800020'},[pscustomobject]@{suffix='0\S2CB\0x01\0x04\0x11';hex='ff20c00000'})}
function Get-NativeCameraDmaSnapshot{return $snapshot}
$writes=0;$injected=$false
function Set-CameraDmaMap([string]$Path,[string]$Expected,[string]$Proposed){
 $row=@($snapshot.maps|Where-Object {($snapshot.driverKey+'\'+$_.suffix) -eq $Path})[0]
 if($row.hex -ne $Expected){throw 'Concurrent edit'}
 $row.hex=$Proposed;$script:writes++
 if($script:writes -eq 2 -and -not $script:injected){$script:injected=$true;throw 'Injected readback failure after second write'}
}
function Get-Item {
 param([string]$LiteralPath)
 if($LiteralPath.StartsWith('Registry::unit-test\')){
  $row=@($snapshot.maps|Where-Object {($snapshot.driverKey+'\'+$_.suffix) -eq $LiteralPath})[0]
  $key=[pscustomobject]@{hex=$row.hex}
  $key|Add-Member -MemberType ScriptMethod -Name GetValue -Value {param($Name)return (Convert-CameraDmaHex $this.hex)}
  return $key
 }
 return Microsoft.PowerShell.Management\Get-Item -LiteralPath $LiteralPath
}
$plan=Get-CameraDmaPlanFromSnapshot $snapshot
$failed=$false;try{Prepare-CameraDma $plan $true|Out-Null}catch{$failed=$true}
if(-not $failed -or $writes -ne 4 -or $snapshot.maps[0].hex -ne 'ff20800020' -or $snapshot.maps[1].hex -ne 'ff20c00000'){throw 'Two-value transaction failed to restore exact original values'}
$pending=Get-Content -LiteralPath (Join-Path $base 'CameraDma\pending.json') -Raw|ConvertFrom-Json
if($pending.status -ne 'FAILED_ORIGINAL_VALUES_RESTORED' -or -not(Test-Path -LiteralPath (Join-Path $pending.backup 'before-MAP.reg'))){throw 'Failure state or original MAP backup missing'}
$prepared=Prepare-CameraDma (Get-CameraDmaPlanFromSnapshot $snapshot) $true
if($prepared.status -ne 'PENDING_RESTART' -or $writes -ne 6){throw 'Successful preparation did not require restart'}
$sameBoot=Prepare-CameraDma (Get-CameraDmaPlanFromSnapshot $snapshot) $true
if($sameBoot.status -ne 'PENDING_RESTART' -or $writes -ne 6){throw 'Same kernel session bypassed restart or repeated MAP writes'}
function Get-CameraBootIdentity{return [pscustomobject]@{recordId=11;timeUtc='2026-11-04T00:10:00Z'}}
$afterBoot=Prepare-CameraDma (Get-CameraDmaPlanFromSnapshot $snapshot) $true
if($afterBoot.status -ne 'READY' -or $writes -ne 6){throw 'New boot failed to release gate idempotently'}
[ordered]@{status='PASS_DMA_TRANSACTION_ROLLBACK_RESTART_IDEMPOTENCE';cameraOpened=$false;nativeRegistryChanged=$false;injectedReadbackFailure=$true;originalMapsRestoredAfterFailure=$true;backupPresent=$true;sameBootBlocked=$true;newBootReady=$true;writesIncludingRollback=$writes}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $PSScriptRoot 'dma-transaction-regression.json') -Encoding UTF8
'PASS_DMA_TRANSACTION_ROLLBACK_RESTART_IDEMPOTENCE'
