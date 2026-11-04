param([switch]$Restore243)
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$cache='C:\ProgramData\CodexQispBuild249';if($PSScriptRoot -ne $cache){throw 'Run staged C249 operator only'}
if(-not ([Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){throw 'Elevated operator required'}
$statePath=Join-Path $cache 'state249.json';$state=Get-Content -LiteralPath $statePath -Raw|ConvertFrom-Json
$r=[ordered]@{};foreach($p in $state.PSObject.Properties){$r[$p.Name]=$p.Value}
$r.operatorPid=$PID;$r.operatorStarted=(Get-Date -Format o);$lock=$null
$refPath='C:\ProgramData\CodexQispBuild243\retained243.json';$reference=Get-Content -LiteralPath $refPath -Raw|ConvertFrom-Json
$assets=Get-Content -LiteralPath (Join-Path $cache 'assets249.json') -Raw|ConvertFrom-Json
. (Join-Path $cache 'CameraState196.ps1')
. (Join-Path $cache 'Query-QispTraces207.ps1') -ImportOnly
function Save249 {$r.updated=(Get-Date -Format o);$r|ConvertTo-Json -Depth 12|Set-Content -LiteralPath $statePath -Encoding UTF8}
function Hold249([string]$why){$r.status='HOLD249_PARTIAL_DATA_REFRESH_NO_AUTO_RECOVERY';$r.errors+=@($why);Save249;for(;;){Start-Sleep -Seconds 2;Save249}}
function Known249 {
 foreach($f in $reference.files){
  if((Get-FileHash -LiteralPath $f.backup).Hash -ne $f.sha256){throw 'Immutable243 backup differs'}
  $hash=(Get-FileHash -LiteralPath $f.live).Hash;$changed=@($assets.data|Where-Object live -eq $f.live)
  if($changed.Count -eq 1){if($hash -notin @($f.sha256,$changed[0].candidateSHA)){throw 'Unknown live data bytes'}}
  elseif($hash -ne $f.sha256){throw 'Unaffected243 file differs'}
 }
}
function Boundary249 {
 if($r.capturePending -or -not $r.ownerCloseVerified -or -not $r.fullMftDecoded){throw 'Capture/source/full-decode unresolved'}
 $null=Assert-NoQispTraces207
 if(@(Get-Process -Name 'QispCapture*','CamProbe*','WindowsCamera','Telegram','WinRoi*','WinFocus*','Qoder','devcon','pnputil','ReadMft*','tracerpt','java','cdb' -ErrorAction SilentlyContinue).Count -or (Get-Service FrameServer).Status -ne 'Stopped'){throw 'Camera/device/decoder owner exists'}
 $os=Get-CimInstance Win32_OperatingSystem;if([datetimeoffset]$os.LastBootUpTime -ne [datetimeoffset]$reference.boot){throw 'Boot changed'}
 if((Get-WinEvent -FilterHashtable @{LogName='System';Id=1001;ProviderName='Microsoft-Windows-WER-SystemErrorReporting'} -MaxEvents 1).RecordId -ne 120593){throw 'New BugCheck'}
 if((Get-FileHash -LiteralPath (QispServiceImage 'QCCamAvs')).Hash -ne $reference.signedKernelSHA256){throw 'SYS080 differs'}
 $mft=(Get-Item -LiteralPath 'Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Classes\CLSID\{4C2331F0-66BE-4177-9841-2FCBA8CCF5CA}\InprocServer32').GetValue('')
 if((Get-FileHash -LiteralPath $mft).Hash -ne $reference.signedMFTSHA256){throw 'MFT075b differs'}
 Known249
}
function Nodes249([switch]$AllowDisabled){
 foreach($n in $reference.nodes){$v=QispNode $n.id;$allowed=@(0);if($AllowDisabled -and $n.id -in @('ACPI\QCOM0529\15','ACPI\QCOM0506\1A','DISPLAY\QCOM_AVSTREAM_8150\3&13F80E4D&0&UID32768')){$allowed=@(0,22)}
  if($v.problem -notin $allowed -or $v.version -ne $n.version -or $v.inf -ne $n.inf -or $v.service -ne $n.service){throw ('Node binding/problem differs: '+$n.id)}
 }
}
function Node249([string]$id,[bool]$enabled,[string]$label){
 if(-not $r.refreshPending -or $id -notin @('ACPI\QCOM0529\15','ACPI\QCOM0506\1A','DISPLAY\QCOM_AVSTREAM_8150\3&13F80E4D&0&UID32768')){Hold249 'Invalid camera-only group step'}
 Boundary249;Nodes249 -AllowDisabled
 $expected=if($enabled){0}else{22};$before=QispNode $id
 if($before.problem -ne $expected){
  $r.deviceStep=$label;$r.deviceId=$id;Save249;$switch=if($enabled){'/enable-device'}else{'/disable-device'}
  $p=Start-Process -FilePath 'C:\Windows\System32\pnputil.exe' -ArgumentList @($switch,('"'+$id+'"')) -WindowStyle Hidden -RedirectStandardOutput (Join-Path $cache ($label+'.out.log')) -RedirectStandardError (Join-Path $cache ($label+'.err.log')) -PassThru
  $null=$p.Handle;$r.pendingPid=$p.Id;Save249;$late=-not $p.WaitForExit(45000);if($late){$r.deadlineExceeded=$true;while(-not $p.WaitForExit(1000)){Save249}}
  $p.WaitForExit();$p.Refresh();$code=$p.ExitCode;$r.pendingPid=0;$r.deviceExit=$code;Save249
  $text=[IO.File]::ReadAllText((Join-Path $cache ($label+'.out.log')))+[IO.File]::ReadAllText((Join-Path $cache ($label+'.err.log')))
  if($null -eq $code -or $code -ne 0 -or $late -or $text -match '(?i)reboot required|reboot is required|must be restarted|restart (the|your) computer'){Hold249 'Camera-only device step rejected/late/reboot requested; no reboot'}
 }
 if((QispNode $id).problem -ne $expected){Hold249 'Camera node actual state differs'}
 $r.deviceStepCompleted=$label;Save249
}
try{
 if($r.refreshPending -or $r.copyStarted -or $r.pendingPid -or $r.capturePending -or -not $r.ownerCloseVerified -or -not $r.fullMftDecoded){throw 'Unresolved previous operation; no automatic recovery'}
 if((Get-FileHash -LiteralPath $refPath).Hash -ne $assets.reference243SHA){throw 'Reference243 identity differs'}
 foreach($f in $assets.files){if((Get-FileHash -LiteralPath (Join-Path $cache $f.name)).Hash -ne $f.sha256){throw ('C stage asset differs: '+$f.name)}}
 $lock=[IO.File]::Open('C:\ProgramData\CodexQispBuild062\operation.lock',[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
 Boundary249;Nodes249
 $crashes=@(Get-WinEvent -FilterHashtable @{LogName='Application';Id=1000;StartTime=([datetimeoffset]$reference.boot).LocalDateTime} -ErrorAction SilentlyContinue|Where-Object {$_.Message -match 'svchost.exe_FrameServer|QcDeviceMFT8150.dll'})
 if($crashes.Count){throw 'FrameServer/MFT crash this boot'}
 if([IO.DriveInfo]::new('C:\').AvailableFreeSpace -lt 1GB -or (-not $Restore243 -and (Get-CimInstance Win32_OperatingSystem).FreePhysicalMemory -lt 650*1024)){throw 'C1GiB/RAM650MiB gate'}
 $phase=if($Restore243){'original'}else{'candidate'}
 if($r.phase -eq $phase){throw 'Requested phase already active; preserve single deployment'}
 foreach($d in $assets.data){$p=if($Restore243){$d.original}else{Join-Path $cache $d.candidateName};$expected=if($Restore243){$d.originalSHA}else{$d.candidateSHA};if((Get-FileHash -LiteralPath $p).Hash -ne $expected){throw 'Data target source differs'}}
 $r.refreshPending=$true;$r.refreshTarget=$phase;$r.status='REFRESHING249_CAMERA_DATA_ONLY';Save249
 Node249 'ACPI\QCOM0529\15' $false ('disable-rear-'+$phase)
 Node249 'ACPI\QCOM0506\1A' $false ('disable-front-'+$phase)
 Boundary249
 $r.copyStarted=$true;Save249
 foreach($d in $assets.data){$p=if($Restore243){$d.original}else{Join-Path $cache $d.candidateName};$expected=if($Restore243){$d.originalSHA}else{$d.candidateSHA};Copy-Item -LiteralPath $p -Destination $d.live -Force;if((Get-FileHash -LiteralPath $d.live).Hash -ne $expected){Hold249 'Data copy SHA readback differs'}}
 $r.phase=$phase;$r.copyStarted=$false;Save249
 Node249 'ACPI\QCOM0506\1A' $true ('enable-front-'+$phase)
 Node249 'ACPI\QCOM0529\15' $true ('enable-rear-'+$phase)
 Node249 'DISPLAY\QCOM_AVSTREAM_8150\3&13F80E4D&0&UID32768' $false ('disable-avs-'+$phase)
 Node249 'DISPLAY\QCOM_AVSTREAM_8150\3&13F80E4D&0&UID32768' $true ('enable-avs-'+$phase)
 $r.refreshPending=$false;$r.refreshTarget='';Boundary249;Nodes249
 foreach($d in $assets.data){$expected=if($Restore243){$d.originalSHA}else{$d.candidateSHA};if((Get-FileHash -LiteralPath $d.live).Hash -ne $expected){throw 'Final target phase differs'}}
 $r.status='PASS249_CAMERA_DATA_PHASE_'+$phase.ToUpper()+'_NO_REBOOT';Save249
}catch{
 if($r.refreshPending -or $r.copyStarted -or $r.pendingPid){Hold249 $_.Exception.Message}
 $r.status='FAILED249_DATA_APPLY_CLOSED_NO_PARTIAL_REFRESH';$r.errors+=@($_.Exception.Message);$r.position=$_.InvocationInfo.PositionMessage;Save249
}finally{if($r.refreshPending -or $r.copyStarted -or $r.pendingPid){Hold249 'Finally refused unresolved refresh'};if($lock){$lock.Dispose()};Save249}
