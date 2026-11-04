$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$taskRoot=Split-Path -Parent $PSScriptRoot;$root=$taskRoot
. (Join-Path $root 'CameraSelection.ps1')
$passed=[Collections.Generic.List[string]]::new()
function Assert([bool]$Condition,[string]$Name){if(-not $Condition){throw ('FAIL '+$Name)};$passed.Add($Name)}
function Reject([scriptblock]$Action,[string]$Name){$caught=$false;try{& $Action|Out-Null}catch{$caught=$true};Assert $caught $Name}
$absent="ERROR256 hr=8000ffff message=Exactly one selected camera required`nSTATE256 captureCreated=0 initializeAttempted=0 readerCreated=0 startAttempted=0 readerStarted=0`nRETIRE256 neverOpened=1 readerStopped=1 readerClosed=1 captureClosed=1`nRESULT256 hr=8000ffff frames=0 signal=false"
$proof=Get-ProbeRetirement $absent
Assert ($proof.closed -and $proof.neverOpened) 'Explicit no objects and no initialization proves never opened'
Assert (-not (Get-ProbeRetirement 'ERROR256 hr=8000ffff message=Exactly one named camera required').closed) 'Old error message alone is not retirement evidence'
Assert (-not (Get-ProbeRetirement ($absent+"`nSTEP256 initialize_begin qpc=1")).closed) 'Contradictory initialization marker rejects never-opened claim'
Assert (-not (Get-ProbeRetirement ($absent+"`nCLOSE256 reader_close hr=8000ffff")).closed) 'Close failure rejects even successful retirement flags'
Assert (-not (Get-ProbeRetirement ($absent+"`n"+$absent)).closed) 'Duplicate lifecycle records rejected'
$partial="STATE256 captureCreated=1 initializeAttempted=1 readerCreated=0 startAttempted=0 readerStarted=0`nCLOSE256 capture_close hr=00000000`nRETIRE256 neverOpened=0 readerStopped=1 readerClosed=1 captureClosed=1"
Assert ((Get-ProbeRetirement $partial).closed -and -not (Get-ProbeRetirement $partial).neverOpened) 'Created capture requires its close even without reader creation'
Assert (-not (Get-ProbeRetirement ($partial.Replace('CLOSE256 capture_close hr=00000000',''))).closed) 'Missing capture close rejects partial initialization'
$full="STATE256 captureCreated=1 initializeAttempted=1 readerCreated=1 startAttempted=1 readerStarted=1`nCLOSE256 reader_stop hr=00000000`nCLOSE256 reader_close hr=00000000`nCLOSE256 capture_close hr=00000000`nRETIRE256 neverOpened=0 readerStopped=1 readerClosed=1 captureClosed=1"
Assert ((Get-ProbeRetirement $full).closed) 'Successful reader stop and both closes accepted'
Assert (-not (Get-ProbeRetirement ($full.Replace('CLOSE256 reader_stop hr=00000000',''))).closed) 'Start attempt requires successful stop'
Assert (-not (Get-ProbeRetirement ($full.Replace('captureClosed=1','captureClosed=0'))).closed) 'Unclosed capture rejects apparently successful close text'
$avs='DISPLAY\QCOM_AVStream_8150\test'
$rear=[pscustomobject]@{id='\\?\DISPLAY#QCOM_AVStream_8150#test#{e5323777-f976-4f5b-9b55-b94699c46e44}\{5584F823-3830-4CFD-947F-78DE17A8B14C}';instanceId=$avs;enabled=$true;panel=2;name='Camera'}
$front=[pscustomobject]@{id='\\?\DISPLAY#QCOM_AVStream_8150#test#{e5323777-f976-4f5b-9b55-b94699c46e44}\{4FAEAFD4-041B-4E46-85FD-400473891182}';instanceId=$avs;enabled=$true;panel=1;name='Camera'}
$inventory=[pscustomobject]@{schema=1;revision='R22';enumerationComplete=$true;cameraOpened=$false;devices=@($rear,$front)}
Assert ((Select-NativeCamera $inventory $avs rear).id -eq $rear.id) 'Duplicate friendly names do not confuse rear identity'
Assert ((Select-NativeCamera $inventory $avs front).id -eq $front.id) 'Front identity selected independently of friendly name'
$rear.name='Localized rear camera';Assert ((Select-NativeCamera $inventory $avs rear).id -eq $rear.id) 'Renamed native camera selected by physical identity'
$rear.panel=0;Assert ((Select-NativeCamera $inventory $avs rear).id -eq $rear.id) 'Known sensor GUID identifies camera when panel is unspecified'
$rear.panel=1;Reject {Select-NativeCamera $inventory $avs rear} 'Contradictory panel and sensor GUID rejected';$rear.panel=2
$inventory.devices=@($rear,$rear,$front);Reject {Select-NativeCamera $inventory $avs rear} 'Multiple rear interfaces rejected rather than choosing first'
$inventory.devices=@();Assert ($null -eq (Select-NativeCamera $inventory $avs rear -AllowAbsent)) 'Explicit complete inventory without a native interface permits pre-install absence'
Reject {Select-NativeCamera $inventory $avs rear} 'Post-install absence remains failure'
$virtual=[pscustomobject]@{id='\\?\ROOT#CAMERA#test';instanceId='ROOT\CAMERA\test';enabled=$true;panel=2;name='QC Back Camera'}
$inventory.devices=@($virtual);Assert ($null -eq (Select-NativeCamera $inventory $avs rear -AllowAbsent)) 'Misleading virtual-camera name cannot qualify as native hardware'
$inventory.devices=@($rear,$front)
$path=Join-Path $PSScriptRoot ('inventory-'+[guid]::NewGuid().ToString('N')+'.json')
try{
 $inventory|ConvertTo-Json -Depth 5|Set-Content -LiteralPath $path -Encoding UTF8
 Assert (@((Read-CameraInventory $path).devices).Count -eq 2) 'Complete read-only inventory accepted'
 $inventory.enumerationComplete=$false;$inventory|ConvertTo-Json -Depth 5|Set-Content -LiteralPath $path -Encoding UTF8
 Reject {Read-CameraInventory $path} 'Incomplete enumeration never proves absence'
 $inventory.enumerationComplete=$true;$inventory.devices=@($rear,$rear);$inventory|ConvertTo-Json -Depth 5|Set-Content -LiteralPath $path -Encoding UTF8
 Reject {Read-CameraInventory $path} 'Duplicate identities in enumeration rejected'
}finally{Remove-Item -LiteralPath $path -ErrorAction SilentlyContinue}
[ordered]@{status='PASS_CAMERA_SELECTION_LIFECYCLE';powershell=[string]$PSVersionTable.PSVersion;tests=$passed;cameraOpened=$false;driverChanged=$false}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $PSScriptRoot 'camera-selection-regression.json') -Encoding UTF8
Write-Output ('PASS_CAMERA_SELECTION_LIFECYCLE count='+$passed.Count)
