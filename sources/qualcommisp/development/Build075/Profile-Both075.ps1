param([ValidateRange(1,4)][int]$QueueDepth=2,[ValidateSet('Preview','Video')][string]$Mode='Preview',[switch]$LowLatency,[switch]$FocusPulse,[switch]$TraceMft,[switch]$InspectRoi,[switch]$RoiSequence,[switch]$InspectTuning,[string]$InstallationStateFile='C:\ProgramData\CodexQispBuild062\active-install.json')
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$cache=Join-Path $env:ProgramData 'CodexQispBuild062'
$state=Get-Content -LiteralPath $InstallationStateFile -Raw|ConvertFrom-Json
if($state.status -ne 'PASS_NATIVE062_AVS_ACTIVE'){throw 'Completed native061 installation required'}
$active=$state.active
. (Join-Path $active 'tools\CameraState.ps1')
$nativeRecovery=$state.recovery
$script:qispRun='H:\Vlad\Codex\QualcommISP\Build075\results\'+$Mode+'-'+$(if($LowLatency){'lowlat-'}else{'default-'})+(Get-Date -Format 'yyyyMMdd-HHmmss')
New-Item -ItemType Directory -Path $script:qispRun|Out-Null
$script:qispRecovery=Join-Path $script:qispRun 'observer';New-Item -ItemType Directory -Path $script:qispRecovery|Out-Null
$script:qispBusy=$false;$script:qispRebootRequired=$false;$script:qispPendingPid=0
$result=[ordered]@{build='QISP Build074 convergence trial profiling installed062';mode=$Mode;readQueueDepth=$QueueDepth;lowLatency=[bool]$LowLatency;focusPulse=[bool]$FocusPulse;mftTrace=[bool]$TraceMft;status='FAILED';started=(Get-Date).ToString('o');run=$script:qispRun;activeInstallation=$active;processing='native hardware IFE/IPE';signature='sampled up to4096bytes per live frame; saved RAW full FNV';sessions=@();errors=@();pending=$false}
$traces=@();$filter=$null;$mftTrace=$null
try {
 if(@(Get-Process -Name 'QispCapture*','CamProbe*','WindowsCamera','Qoder','devcon','pnputil' -ErrorAction SilentlyContinue).Count){throw 'Conflicting camera/device process'}
 if((Get-CimInstance Win32_OperatingSystem).LastBootUpTime.ToString('o') -ne $state.boot){throw 'Reboot after activation needs new validation scope'}
 $bug=Get-WinEvent -FilterHashtable @{LogName='System';Id=1001;ProviderName='Microsoft-Windows-WER-SystemErrorReporting'} -MaxEvents 1
 if($bug.RecordId -ne 120593){throw 'New crash event detected'}
 foreach($node in $state.nodes){$current=QispNode $node.id;if($current.problem -ne 0 -or $current.version -ne $node.version -or $current.service -ne $node.service){throw 'Native device differs'}}
 $tracelog=Join-Path $script:qispRecovery 'tracelog.exe'
 Copy-Item -LiteralPath 'H:\Vlad\EWDK\Program Files\Windows Kits\10\bin\10.0.26100.0\x64\tracelog.exe' -Destination $tracelog
 [Reflection.Assembly]::LoadFrom('H:\Vlad\Codex\QualcommISP\Build074\bin\ObserverTypes074.dll') | Out-Null
 $filter=[Qisp061DeployFilter]::NtQueryDebugFilterState(0x51,3)
 if($filter -notin 0,1){$filter=$null;throw 'Cannot save native camera trace filter'}
 if([Qisp061DeployFilter]::NtSetDebugFilterState(0x51,3,1) -ne 0){throw 'Cannot enable camera close trace'}
 foreach($side in 'Front','Rear'){
  $lower=$side.ToLowerInvariant();$trace='Codex061Fast-'+[Guid]::NewGuid().ToString('N').Substring(0,8)
  $etl=Join-Path $script:qispRun ('dbgprint-'+$lower+'.etl')
  QispCommand ('trace-'+$lower) $tracelog @('-start',$trace,'-f',('"'+$etl+'"'),'-b','64','-min','2','-max','8','-seq','64','-eflag','DBGPRINT')
  $traces+=@($trace)
  if($TraceMft){
   $mftTrace='Qisp063Mft-'+[Guid]::NewGuid().ToString('N').Substring(0,8)
   QispCommand ('mft-start-'+$lower) (Join-Path $env:windir 'System32\logman.exe') @('start',$mftTrace,'-p','{88c9b48e-2d10-41ce-8433-92b27d4a6a67}','0xffffffffffffffff','0xff','-o',('"'+(Join-Path $script:qispRun ('mft-'+$lower+'.etl'))+'"'),'-f','bin','-max','256','-bs','64','-nb','8','64','-ets')
  }
  $capture=Join-Path $script:qispRun ('capture-'+$lower);New-Item -ItemType Directory -Path $capture|Out-Null
  $name=if($side -eq 'Front'){'QC Front Camera'}else{'QC Back Camera'}
  $argsList=@('--single-opening','--name',('"'+$name+'"'),'--width','1920','--height','1080','--seconds','8','--warmup','8','--repeat','1','--read-timeout-ms','5000','--outstanding',([string]$QueueDepth),'--no-flush','--max-session-ms','15000','--pair-dir',('"'+$capture+'"'),'--pair-count','4','--native-inspect','--measure-stream','--fast-measure')
  if($QueueDepth -gt 2){$argsList+=@('--queue-depth-test')}
  if($Mode -eq 'Video'){$argsList+=@('--video-stream','--native-auto-controls')}else{$argsList+=@('--preview-stream')}
  if($LowLatency){$argsList+=@('--low-latency')}
  if($FocusPulse -and $side -eq 'Rear'){$argsList+=@('--focus-pulse')}
  if($InspectRoi){$argsList+=@('--inspect-roi')}
  $argsList+=@('--inspect-controls')
  if($InspectTuning){$argsList+=@('--inspect-tuning')}
  if($RoiSequence){$argsList+=@('--roi-sequence','--seconds','14','--max-session-ms','25000')}
  $process=Start-Process -FilePath 'H:\Vlad\Codex\QualcommISP\Build075\bin\QispCapture075.exe' -ArgumentList $argsList -WindowStyle Hidden -RedirectStandardOutput (Join-Path $capture 'capture.log') -RedirectStandardError (Join-Path $capture 'capture.err.log') -PassThru
  $null=$process.Handle
  $script:qispBusy=$true;$script:qispPendingPid=$process.Id
  if(-not $process.WaitForExit(45000)){$script:qispBusy=$true;$script:qispPendingPid=$process.Id;throw 'Capture owner pending; do not kill or rebind'}
  $process.WaitForExit();$process.Refresh();$exit=$process.ExitCode
  $script:qispBusy=$true
  $log=Get-Content -LiteralPath (Join-Path $capture 'capture.log') -Raw
  if($log -notmatch 'phase=CAPTURE_CLEANUP_COMPLETE hr=0x00000000' -or $log -notmatch 'phase=SINGLE_TOP_LEVEL_OPEN_END hr=0x00000000' -or $log -match 'QISP006_OWNER_PENDING'){throw 'Source closure not proven'}
  if($mftTrace){& (Join-Path $env:windir 'System32\logman.exe') stop $mftTrace -ets|Out-Null;$mftTrace=$null}
  & $tracelog -stop $trace|Out-Null;if($LASTEXITCODE){throw 'Trace stop failed'};$traces=@()
  $xml=Join-Path $script:qispRun ('dbgprint-'+$lower+'.xml')
  $decode=Start-Process -FilePath (Join-Path $env:windir 'System32\tracerpt.exe') -ArgumentList @(('"'+$etl+'"'),'-o',('"'+$xml+'"'),'-of','XML','-y') -WindowStyle Hidden -RedirectStandardOutput (Join-Path $capture 'decode.log') -RedirectStandardError (Join-Path $capture 'decode.err.log') -PassThru
  $null=$decode.Handle
  if(-not $decode.WaitForExit(20000)){throw 'Decoder pending; no device operation'}
  $decode.WaitForExit();$decode.Refresh();if($decode.ExitCode){throw 'Trace decoder failed'}
  & (Join-Path $active 'tools\Check-NativeClose.ps1') -XmlFile $xml -OutputFile (Join-Path $script:qispRun ('native-close-'+$lower+'.json'))
  $script:qispBusy=$false;$script:qispPendingPid=0
  if($exit){throw ('Capture failed '+$side+' exit='+$exit)}
  $result.sessions+=@([ordered]@{camera=$side;captureDirectory=$capture;exit=$exit;nativeCloseEvidencePassed=$true;resultLine=@($log -split "`r?`n"|Where-Object {$_ -like 'RESULT *'})[0]})
 }
 $result.status='PASS_BOTH063_PROFILE_INSTALLED062'
} catch {$result.errors+=@($_.Exception.Message)}
finally {
 if($mftTrace){& (Join-Path $env:windir 'System32\logman.exe') stop $mftTrace -ets|Out-Null}
 foreach($trace in $traces){& $tracelog -stop $trace|Out-Null}
 if($null -ne $filter){$result.debugFilterRestored=[Qisp061DeployFilter]::NtSetDebugFilterState(0x51,3,[byte]$filter) -eq 0}
 $result.pending=$script:qispBusy;$result.pendingPid=$script:qispPendingPid;$result.completed=(Get-Date).ToString('o');$result|ConvertTo-Json -Depth 7|Set-Content -LiteralPath (Join-Path $script:qispRun 'result.json') -Encoding UTF8
 $script:qispRun|Set-Content -LiteralPath 'H:\Vlad\Codex\QualcommISP\Build075\results\latest-profile075.txt' -Encoding UTF8
}
if($result.status -notlike 'PASS*'){throw ($result.errors -join '; ')}
