$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$b=Split-Path $PSScriptRoot;$repo=Split-Path $b;$journal=Join-Path $b 'research\validation080.json'
if(Test-Path -LiteralPath $journal){throw 'Preserve validation journal'}
$r=[ordered]@{status='RUNNING';pid=$PID;started=(Get-Date -Format o);scope='Both cameras Preview with rear AF pulse and restore; repeat native Video1080p; cached tuning/EEPROM/close';pending=$false;runs=@();errors=@()};$lock=$null
function Save080 {$r|ConvertTo-Json -Depth 8|Set-Content -LiteralPath $journal -Encoding UTF8}
function Crashes080 {return @(Get-WinEvent -FilterHashtable @{LogName='Application';Id=1000;StartTime=[datetime]'2026-10-23T22:55:00'} -ErrorAction SilentlyContinue|Where-Object {$_.Message -match 'svchost.exe_FrameServer'}|ForEach-Object RecordId)}
Save080
try{
 $lock=[IO.File]::Open('C:\ProgramData\CodexQispBuild062\operation.lock',[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
 $cpu=Get-Content -Raw -LiteralPath (Join-Path $b 'research\cpu080.json')|ConvertFrom-Json
 if($cpu.status -ne 'PASS080_BOTH_NATIVE_VIDEO_CLOSED_CPU_TRACE_RECORDED' -or $cpu.pending -or $cpu.wprOwned){throw 'Completed CPU080 capture/recorder required'}
 $r.cpuRun=$cpu.run;$r.crashesBefore=Crashes080
 foreach($item in @(@{mode='Preview';pulse=$true;trace=$true},@{mode='Video';pulse=$false;trace=$false})){
  if((Get-CimInstance Win32_OperatingSystem).FreePhysicalMemory -lt 600*1024){throw 'Low RAM; no new camera'}
  & (Join-Path $PSScriptRoot 'Profile-Both080.ps1') -Mode $item.mode -QueueDepth 2 -InspectTuning -FocusPulse:($item.pulse) -TraceMft:($item.trace)
  $run=(Get-Content -Raw -LiteralPath (Join-Path $b 'results\latest-profile080.txt')).Trim()
  $p=Get-Content -Raw -LiteralPath (Join-Path $run 'result.json')|ConvertFrom-Json
  if([datetimeoffset]$p.started -lt [datetimeoffset]$r.started){throw 'Stale profile pointer'}
  $r.pending=$p.pending;$r.runs+=@([ordered]@{mode=$item.mode;focusPulse=$item.pulse;run=$run;status=$p.status;pending=$p.pending});Save080
  if($p.status -ne 'PASS_BOTH080_PROFILE_INSTALLED080' -or $p.pending){throw 'Capture/native close failed'}
  & 'C:\Users\Xiaomi\AppData\Local\Python\pythoncore-3.15-64\python.exe' (Join-Path $repo 'Build074\tools\Audit-Cached074.py') $run candidate *> (Join-Path $run 'cached-audit074.log');if($LASTEXITCODE){throw 'Actual cached data/EEPROM differs'}
  $rear=Get-Content -Raw -LiteralPath (Join-Path $run 'capture-rear\capture.log')
  if($rear -notmatch 'FOCUS075_SDK_ROUNDTRIP original=0x20100 set_hr=0x00000000 readback=0x20100 normalization=false commit=false'){throw 'Rear SDK focus roundtrip failed'}
  if($item.pulse -and ($rear -notmatch 'FOCUS063_TRIGGER[^\r\n]*commit_hr=0x00000000' -or $rear -notmatch 'CONTROL061 restore_focus=0x00000000[^\r\n]*target=0x20100')){throw 'Rear AF/restore failed'}
 }
 $r.crashesAfter=Crashes080;if(($r.crashesBefore -join ',') -ne ($r.crashesAfter -join ',')){throw 'New FrameServer crash'}
 $state=Get-Content -Raw -LiteralPath 'C:\ProgramData\CodexQispBuild080\active-install.json'|ConvertFrom-Json
 if([datetimeoffset](Get-CimInstance Win32_OperatingSystem).LastBootUpTime -ne [datetimeoffset]$state.boot){throw 'Boot changed'}
 $bug=Get-WinEvent -FilterHashtable @{LogName='System';Id=1001;ProviderName='Microsoft-Windows-WER-SystemErrorReporting'} -MaxEvents 1;if($bug.RecordId -ne 120593){throw 'New bugcheck'}
 $r.status='PASS080_BOTH_PREVIEW_REPEAT_VIDEO_FOCUS_ROUNDTRIP_AF_RESTORE_NATIVE_CLOSE'
 $state.status='PASS_NATIVE080_AVS_ACTIVE';$state|Add-Member -NotePropertyName validation -NotePropertyValue $journal -Force
 $state|ConvertTo-Json -Depth 8|Set-Content -LiteralPath 'C:\ProgramData\CodexQispBuild080\active-install.json' -Encoding UTF8
}catch{
 $r.status='FAILED';$r.errors+=@($_.Exception.Message)
 $latest=Join-Path $b 'results\latest-profile080.txt'
 if(Test-Path -LiteralPath $latest){$p=Get-Content -Raw -LiteralPath ((Get-Content -Raw -LiteralPath $latest).Trim()+'\result.json')|ConvertFrom-Json;if([datetimeoffset]$p.started -ge [datetimeoffset]$r.started){$r.pending=$p.pending;$r.pendingPid=$p.pendingPid}}
}finally{$r.completed=(Get-Date -Format o);Save080;if($lock){$lock.Dispose()}}
if($r.status -notlike 'PASS*'){throw ($r.errors -join '; ')}
