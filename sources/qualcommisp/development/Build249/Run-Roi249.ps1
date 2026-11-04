$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$b='H:\Vlad\Codex\QualcommISP\Build249';$repo=Split-Path -Parent $b
$journal=Join-Path $b 'research\trial249.json';$local='C:\Users\Xiaomi\Documents\Codex\2026-10-23\new-chat\outputs\Roi249\trial249-local.json'
if(Test-Path -LiteralPath $journal){throw 'Single use249: preserve receipt'}
$r=[ordered]@{status='PREPARING249';pid=$PID;started=(Get-Date -Format o);pending=$false;pendingPid=0;pendingKind='';ownerCloseVerified=$true;fullMftDecoded=$true;driverChanges=$false;tuningChanges=$false;cameraControlSetters=$true;aeOnlyRoi=$true;torchSetters=$false;focusSetters=$false;clearOwnRoiRequired=$true;nativePreview1080Configured=$true;dataReference=249;traces=@();runs=@();errors=@();androidParity=$false}
$lock=$null;$active=@();$currentRun=''
function Save249 {
 if($r.Contains('dataCaptureStarted') -and $r.dataCaptureStarted){
  $ds=Get-Content -LiteralPath 'C:\ProgramData\CodexQispBuild249\state249.json' -Raw|ConvertFrom-Json
  $ds.capturePending=($r.pending -or -not $r.ownerCloseVerified -or -not $r.fullMftDecoded)
  $ds.ownerCloseVerified=$r.ownerCloseVerified;$ds.fullMftDecoded=$r.fullMftDecoded
  $ds.captureJournal=$journal;$ds.captureStatus=$r.status
  $ds|ConvertTo-Json -Depth 12|Set-Content -LiteralPath 'C:\ProgramData\CodexQispBuild249\state249.json' -Encoding UTF8
 }
 $r.updated=(Get-Date -Format o);$s=$r|ConvertTo-Json -Depth 10;[IO.File]::WriteAllText($local,$s,[Text.UTF8Encoding]::new($false));[IO.File]::WriteAllText($journal,$s,[Text.UTF8Encoding]::new($false))}
function Hold249([string]$why){$r.status='HOLD249_OWNER_OR_CLOSE_OR_DECODE_UNPROVEN';$r.errors+=@($why);$r.pending=$true;for(;;){Save249;Start-Sleep -Seconds 2}}
function Child249([string]$exe,[string[]]$arguments,[string]$kind,[int]$deadline=60000){
 $p=Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -RedirectStandardOutput (Join-Path $currentRun ($kind+'.log')) -RedirectStandardError (Join-Path $currentRun ($kind+'.err.log')) -PassThru
 $null=$p.Handle;$r.pending=$true;$r.pendingPid=$p.Id;$r.pendingKind=$kind;Save249
 $late=-not $p.WaitForExit($deadline);if($late){$r.deadlineExceeded=$true;while(-not $p.WaitForExit(1000)){Save249}}
 $p.WaitForExit();$p.Refresh();$code=$p.ExitCode;if($null -eq $code){throw ('Exit code not observed: '+$kind)}
 $r.pending=$false;$r.pendingPid=0;$r.pendingKind='';Save249;return [pscustomobject]@{pid=$p.Id;exit=[int]$code;late=$late}
}
function Helper249([string]$exe,[string[]]$arguments,[string]$kind,[int]$deadline=60000){$v=Child249 $exe $arguments $kind $deadline;if($v.exit -ne 0 -or $v.late){throw ('Helper failed/late: '+$kind)}}
function Idle249 {
 for($i=0;(Get-Service FrameServer).Status.ToString() -ne 'Stopped';++$i){if($i -ge 30){throw 'FrameServer did not naturally stop'};Start-Sleep -Seconds 1}
 if(@(Get-CimInstance Win32_Process|Where-Object {$_.Name -match '^(QispCapture|CamProbe|WindowsCamera|WinRoi|WinFocus|ReadMft|tracerpt|java|devcon|pnputil|Qoder)'}).Count){throw 'Other capture/decoder/device owner'}
}
try{
 [IO.Directory]::CreateDirectory((Split-Path -Parent $local))|Out-Null
 if(-not ([Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){throw 'Elevated operator required'}
 & (Join-Path $b 'tools\Preflight249.ps1')
 $pre=Get-Content -LiteralPath (Join-Path $b 'research\preflight249.json') -Raw|ConvertFrom-Json
 if($pre.status -notlike 'PASS249*' -or -not $pre.captureMemoryGateCurrentlyPass){throw 'Current249/memory preflight rejected'}
 $seal=Get-Content -LiteralPath (Join-Path $b 'research\seal249.json') -Raw|ConvertFrom-Json
 foreach($f in $seal.files){if((Get-FileHash -LiteralPath $f.path).Hash -ne $f.sha256){throw ('Frozen asset differs: '+$f.path)}}
 $tracelog='H:\Vlad\EWDK\Program Files\Windows Kits\10\bin\10.0.26100.0\x64\tracelog.exe';$reader=Join-Path $repo 'Build100\bin\ReadMft100.exe';$capture=Join-Path $repo 'Build247\bin\QispCaptureRoi247b.exe'
 $lock=[IO.File]::Open('C:\ProgramData\CodexQispBuild062\operation.lock',[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
 . (Join-Path $repo 'Build207\tools\Query-QispTraces207.ps1') -ImportOnly
 [Reflection.Assembly]::LoadFrom((Join-Path $repo 'Build074\bin\ObserverTypes074.dll'))|Out-Null
 if([Qisp061DeployFilter]::NtQueryDebugFilterState(0x51,3) -ne 1){throw 'Native close debug filter must already be1'}
 $ds=Get-Content -LiteralPath 'C:\ProgramData\CodexQispBuild249\state249.json' -Raw|ConvertFrom-Json
 if($ds.refreshPending -or $ds.copyStarted -or $ds.pendingPid -or $ds.capturePending -or -not $ds.ownerCloseVerified -or -not $ds.fullMftDecoded -or $ds.errors.Count){throw 'Active241 data journal pending; no second owner'}
 $r.boot=$pre.boot;$r.dataCaptureStarted=$true;Save249
 foreach($side in @('rear','front')){
  Idle249;$null=Assert-NoQispTraces207
  $os=Get-CimInstance Win32_OperatingSystem;if([datetimeoffset]$os.LastBootUpTime -ne [datetimeoffset]$pre.boot){throw 'Boot changed'}
  if($os.FreePhysicalMemory -lt 650*1024 -or [IO.DriveInfo]::new('C:\').AvailableFreeSpace -lt 1GB){throw 'RAM650MiB/C1GiB gate'}
  $script:currentRun=Join-Path $repo ('Build247\results\roi-winrt-'+$side+'-249');[IO.Directory]::CreateDirectory($currentRun)|Out-Null;$r.currentCamera=$side
  $dbg='Qisp249Dbg-'+[guid]::NewGuid().ToString('N').Substring(0,8);$mft='Qisp249Mft-'+[guid]::NewGuid().ToString('N').Substring(0,8)
  $dbgPath=Join-Path $currentRun 'dbgprint.etl';$mftPath=Join-Path $currentRun 'mft.etl'
  Helper249 $tracelog @('-start',$dbg,'-f',('"'+$dbgPath+'"'),'-b','64','-min','8','-max','128','-seq','256','-eflag','DBGPRINT') 'dbg-start'
  $active+=@($dbg);$r.traces=$active;Save249
  Helper249 'C:\Windows\System32\logman.exe' @('start',$mft,'-p','{88c9b48e-2d10-41ce-8433-92b27d4a6a67}','0xffffffffffffffff','0xff','-o',('"'+$mftPath+'"'),'-f','bin','-max','512','-bs','64','-nb','8','128','-ets') 'mft-start'
  $active+=@($mft);$r.traces=$active;$r.ownerCloseVerified=$false;$r.fullMftDecoded=$false;$r.status='RUNNING249_BOTH_WINRT_PREVIEW_AE_ONLY_BRIGHT_DARK';Save249
  $name=if($side -eq 'rear'){'QC Back Camera'}else{'QC Front Camera'}
  $v=Child249 $capture @(('"'+$name+'"'),('"'+$currentRun+'"')) 'capture' 90000
  $text=Get-Content -LiteralPath (Join-Path $currentRun 'capture.log') -Raw
  $clearFailed=($text -match 'ROI247B_CAPS' -and $text -notmatch 'CLOSE247B roi_clear hr=00000000')
  if($text -match 'STEP247B initialize_complete' -and $text -notmatch 'CLOSE247B capture_close hr=00000000'){Hold249 'WinRT capture close not proven'}
  if($text -match 'STEP247B reader_started' -and ($text -notmatch 'CLOSE247B reader_stop hr=00000000' -or $text -notmatch 'CLOSE247B reader_close hr=00000000')){Hold249 'WinRT reader stop/close not proven'}
  Idle249
  foreach($trace in @($active)){Helper249 $tracelog @('-stop',$trace) ('stop-'+$trace);$active=@($active|Where-Object {$_ -ne $trace});$r.traces=$active;Save249}
  Helper249 'C:\Windows\System32\tracerpt.exe' @(('"'+$dbgPath+'"'),'-o',('"'+(Join-Path $currentRun 'dbgprint.xml')+'"'),'-of','XML','-y') 'dbg-decode'
  $closeError=$null
  try{& (Join-Path $repo 'Build196\tools\Check-NativeClose196.ps1') -XmlFile (Join-Path $currentRun 'dbgprint.xml') -OutputFile (Join-Path $currentRun 'native-close.json')}catch{$closeError=$_.Exception.Message}
  $close=Get-Content -LiteralPath (Join-Path $currentRun 'native-close.json') -Raw|ConvertFrom-Json
  $noStart=($text -notmatch 'STEP247B reader_started' -and $v.exit -ne 0 -and $close.initialized -eq 0 -and $close.eventsLost -eq 0 -and @($close.errors).Count -eq 0)
  if(($close.status -ne 'PASS' -or $close.eventsLost -ne 0) -and -not $noStart){Hold249 ('Native hardware close/loss0 missing: '+$closeError)}
  $r.ownerCloseVerified=$true;Save249
  Helper249 $reader @(('"'+$mftPath+'"'),('"'+(Join-Path $currentRun 'mft249.jsonl')+'"'),('"'+(Join-Path $currentRun 'mft249-decode.json')+'"')) 'mft-decode' 120000
  $decode=Get-Content -LiteralPath (Join-Path $currentRun 'mft249-decode.json') -Raw|ConvertFrom-Json;if($decode.status -ne 'PASS100_ETL_SEQUENTIAL_DECODE_NO_EVENTS_LOST' -or $decode.eventsLost -ne 0){Hold249 'Full MFT/loss0 missing'}
  $r.fullMftDecoded=$true;Save249
  if($clearFailed){throw 'Own AE ROI clear failed after verified native/source close'};if($v.exit -ne 0 -or $v.late){throw 'WinRT capture failed after verified native/source close'}
  if($text -notmatch 'RESULT247B hr=00000000 frames=\d+ raws=4 roiCleared=true'){throw 'WinRT result not complete'}
  foreach($phase in @(0,1,3,4)){if((Get-Item -LiteralPath (Join-Path $currentRun ('phase-'+$phase+'.nv12'))).Length -ne 3110400){throw 'Raw NV12 shape/extent rejected'}}
  $r.runs+=@([ordered]@{camera=$side;pid=$v.pid;nativeClose='PASS';winrtCaptureReaderClosed=$true;fullMftEvents=$decode.events;eventsLost=0;aeOnlyRoi=$true;ownRoiCleared=$true;sameSensorFrame=$false;run=$currentRun});Save249;Idle249
 }
 $lock.Dispose();$lock=$null;& (Join-Path $b 'tools\Final-State249.ps1')
 $post=Get-Content -LiteralPath (Join-Path $b 'research\final-state249.json') -Raw|ConvertFrom-Json;if($post.status -notlike 'PASS249*'){throw 'Final current249 audit failed'}
 $r.status='PASS249_BOTH_AE_ONLY_ROI_CLEARED_NATIVE_CLOSE_FULL_MFT_LOSS0';Save249
}catch{
 $r.errors+=@($_.Exception.Message);$r.position=$_.InvocationInfo.PositionMessage
 if($r.pending -or -not $r.ownerCloseVerified -or -not $r.fullMftDecoded){Hold249 $_.Exception.Message}
 $r.status='FAILED249_CLOSED_NO_DRIVER_CHANGE';Save249
}finally{if($r.pending -or -not $r.ownerCloseVerified -or -not $r.fullMftDecoded){Hold249 'Finally refused while pending'};foreach($trace in @($active)){& $tracelog -stop $trace|Out-Null};if($lock){$lock.Dispose()};Save249}
