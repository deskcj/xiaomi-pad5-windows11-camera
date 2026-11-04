param([ValidateSet('preview')][string]$Phase='preview',[int]$ParentPid)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$repo='H:\Vlad\Codex\QualcommISP';$build=Join-Path $repo 'Build077'
if($PID -ne $ParentPid){throw 'Must run within the locked parent operator'}
$manifest=Get-Content -LiteralPath (Join-Path $build 'research\torch-observer-manifest077.json') -Raw | ConvertFrom-Json
foreach($f in $manifest.files){if((Get-FileHash -LiteralPath $f.path).Hash -ne $f.sha256){throw 'Observer source identity changed'}}
$run=Join-Path $build ('results\torch-'+$Phase+'-'+(Get-Date -Format yyyyMMdd-HHmmss))
New-Item -ItemType Directory -Path $run | Out-Null
$journal=Join-Path $run 'result.json'
$r=[ordered]@{status='RUNNING';pid=$PID;phase=$Phase;run=$run;started=(Get-Date -Format o);pending=$false;pendingPid=0;errors=@();sessions=@();tuningChangedByThisHelper=$false;AFTriggered=$false;shortTorchRequested=$true;photoFlashTriggered=$false;roiSet=$false}
$filter=$null;$trace=$null;$mft=$null
function SaveEV074 {$r | ConvertTo-Json -Depth 9 | Set-Content -LiteralPath $journal -Encoding UTF8}
function CheckIdleEV074 {
 if($r.pending){throw 'Native owner or decoder pending'}
 if(@(Get-Process -Name 'WindowsCamera','QispCapture*','QispFocus*','TorchPulse*','DbgEngWitness*','EvStep*','WinRoi*','CamProbe*','Qoder','devcon','pnputil','tracerpt' -ErrorAction SilentlyContinue).Count){throw 'Conflicting camera owner'}
 if([datetimeoffset](Get-CimInstance Win32_OperatingSystem).LastBootUpTime -ne [datetimeoffset]$manifest.boot){throw 'Boot changed'}
 if((Get-WinEvent -FilterHashtable @{LogName='System';Id=1001;ProviderName='Microsoft-Windows-WER-SystemErrorReporting'} -MaxEvents 1).RecordId -ne 120593){throw 'New BugCheck'}
 if((Get-CimInstance Win32_OperatingSystem).FreePhysicalMemory -lt 600*1024){throw 'Low RAM; no camera action'}
}
$filterExe=Join-Path $repo 'Build074\bin\TraceFilter074.exe'
function ReadFilter074 {
 $value=& $filterExe get
 if($LASTEXITCODE){throw 'Native filter query failed'}
 return [int]$value
}
function WriteFilter074([int]$Value) {
 $status=& $filterExe set $Value
 if($LASTEXITCODE){throw 'Native filter set failed'}
 return [int]$status
}
$tracelog='H:\Vlad\EWDK\Program Files\Windows Kits\10\bin\10.0.26100.0\x64\tracelog.exe'
try {
 CheckIdleEV074
 $filter=(ReadFilter074)
 if($filter -notin 0,1){$filter=$null;throw 'Cannot preserve debug filter'}
 if((WriteFilter074 1) -ne 0){throw 'Cannot enable native close trace'}
 foreach($side in 'rear') {
  CheckIdleEV074
  $folder=Join-Path $run $side;New-Item -ItemType Directory -Path $folder | Out-Null
  $s=[ordered]@{camera=$side;run=$folder;status='RUNNING';pending=$false};$r.sessions+=@($s);SaveEV074
  $trace='Qisp074EV-'+[Guid]::NewGuid().ToString('N').Substring(0,8);$mft=$trace+'Mft'
  & $tracelog -start $trace -f (Join-Path $folder 'dbgprint.etl') -b 64 -min 2 -max 8 -seq 64 -eflag DBGPRINT *> (Join-Path $folder 'trace-start.log')
  if($LASTEXITCODE){throw 'Native trace start failed'}
  & 'C:\Windows\System32\logman.exe' start $mft -p '{88c9b48e-2d10-41ce-8433-92b27d4a6a67}' '0xffffffffffffffff' '0xff' -o (Join-Path $folder 'mft.etl') -f bin -max 256 -bs 64 -nb 8 64 -ets *> (Join-Path $folder 'mft-start.log')
  if($LASTEXITCODE){throw 'MFT trace start failed'}
  $suffix=if($side -eq 'front'){'{4FAEAFD4-041B-4E46-85FD-400473891182}'}else{'{5584F823-3830-4CFD-947F-78DE17A8B14C}'}
  $device='\\?\DISPLAY#QCOM_AVStream_8150#3&13f80e4d&0&UID32768#{e5323777-f976-4f5b-9b55-b94699c46e44}\'+$suffix
  $p=Start-Process -FilePath (Join-Path $build 'src\TorchPulse077.exe') -ArgumentList @(('"'+$device+'"'),('"'+(Join-Path $folder 'frames.csv')+'"'),'--own-session') -WindowStyle Hidden -RedirectStandardOutput (Join-Path $folder 'torch.log') -RedirectStandardError (Join-Path $folder 'torch.err') -PassThru
  $null=$p.Handle;$r.pending=$true;$r.pendingPid=$p.Id;$s.clientPid=$p.Id;SaveEV074
  if(-not $p.WaitForExit(45000)){throw 'WinRT owner pending; do not kill or rebind'}
  $p.WaitForExit();$p.Refresh();$s.clientExit=$p.ExitCode
  $log=Get-Content -LiteralPath (Join-Path $folder 'torch.log') -Raw
  if($log -notmatch 'CLIENT_CLOSED clean=1' -or $log -match 'OWNER_PENDING'){throw 'Native owner closure unproven'}
  & 'C:\Windows\System32\logman.exe' stop $mft -ets *> (Join-Path $folder 'mft-stop.log');if($LASTEXITCODE){throw 'MFT trace stop failed'};$mft=$null
  & $tracelog -stop $trace *> (Join-Path $folder 'trace-stop.log');if($LASTEXITCODE){throw 'Native trace stop failed'};$trace=$null
  $xml=Join-Path $folder 'dbgprint.xml'
  $d=Start-Process -FilePath 'C:\Windows\System32\tracerpt.exe' -ArgumentList @(('"'+(Join-Path $folder 'dbgprint.etl')+'"'),'-o',('"'+$xml+'"'),'-of','XML','-y') -WindowStyle Hidden -RedirectStandardOutput (Join-Path $folder 'decode.log') -RedirectStandardError (Join-Path $folder 'decode.err') -PassThru
  $null=$d.Handle;$r.pendingPid=$d.Id;SaveEV074
  if(-not $d.WaitForExit(20000)){throw 'Native decoder pending; no new capture'}
  $d.WaitForExit();$d.Refresh();if($d.ExitCode){throw 'Native decode failed'}
  & 'C:\ProgramData\CodexQispBuild075\active\tools\Check-NativeClose.ps1' -XmlFile $xml -OutputFile (Join-Path $folder 'native-close.json')
  $r.pending=$false;$r.pendingPid=0;$s.pending=$false;SaveEV074
  if($s.clientExit -ne 0 -or $log -notmatch 'TORCH077_RESTORED enabled=0 expected=0' -or $log -notmatch 'TORCH077_OFF_DONE enabled=0' -or $log -notmatch 'CLIENT_CLOSED clean=1 acquired=\d+ errors=0'){throw 'Torch interface test failed but native closure completed'}
  $s.status='PASS077_TORCH_API_PULSE_OFF_RESTORED_NATIVE_CLOSE';$s.completed=(Get-Date -Format o);SaveEV074
 }
 $r.status='PASS077_REAR_TORCH_API_PULSE_OFF_RESTORED_NATIVE_CLOSE'
} catch {$r.status='FAILED';$r.errors+=@($_.Exception.Message)}
finally {
 if($mft){& 'C:\Windows\System32\logman.exe' stop $mft -ets *> (Join-Path $run 'mft-stop-finally.log')}
 if($trace){& $tracelog -stop $trace *> (Join-Path $run 'trace-stop-finally.log')}
 if($null -ne $filter){$r.debugFilterRestored=(WriteFilter074 ([int]$filter)) -eq 0}
 $r.completed=(Get-Date -Format o);SaveEV074
 $run | Set-Content -LiteralPath (Join-Path $build 'results\latest-torch077.txt') -Encoding UTF8
}
if($r.status -notlike 'PASS*'){throw ($r.errors -join '; ')}
