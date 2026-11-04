$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$build=Split-Path $PSScriptRoot;$repo=Split-Path $build
$journal=Join-Path $build 'research\validation075.json'
if(Test-Path -LiteralPath $journal){throw 'Preserve existing validation journal'}
$r=[ordered]@{status='RUNNING';pid=$PID;started=(Get-Date -Format o);scope='Both cameras preview/video; SDK unnormalized focus flags; rear AF pulse and restore';pending=$false;runs=@();errors=@()}
$lock=$null
function Save075 {$r|ConvertTo-Json -Depth 8|Set-Content -LiteralPath $journal -Encoding UTF8}
function Crashes075 {return @(Get-WinEvent -FilterHashtable @{LogName='Application';Id=1000;StartTime=[datetime]'2026-10-23T22:55:00'} -ErrorAction SilentlyContinue|Where-Object {$_.Message -match 'svchost.exe_FrameServer'}|ForEach-Object RecordId)}
Save075
try{
 $lock=[IO.File]::Open('C:\ProgramData\CodexQispBuild062\operation.lock',[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
 $state=Get-Content -Raw -LiteralPath 'C:\ProgramData\CodexQispBuild075\active-install.json'|ConvertFrom-Json
 if($state.status -ne 'PASS_NATIVE075_AVS_ACTIVE_NOT_YET_CAMERA_VALIDATED' -or $state.pending){throw 'Completed075b installation required'}
 $r.crashesBefore=Crashes075
 foreach($item in @(@{mode='Preview';pulse=$false;trace=$true},@{mode='Video';pulse=$false;trace=$false},@{mode='Preview';pulse=$true;trace=$false})){
  for($i=0;(Get-CimInstance Win32_OperatingSystem).FreePhysicalMemory -lt 600*1024;$i++){if($i -ge 15){throw 'Low RAM before camera open'};Start-Sleep -Seconds 2}
  & (Join-Path $PSScriptRoot 'Profile-Focus075.ps1') -Mode $item.mode -QueueDepth 2 -InspectTuning -FocusPulse:($item.pulse) -TraceMft:($item.trace)
  $run=(Get-Content -Raw -LiteralPath (Join-Path $build 'results\latest-focus-profile075.txt')).Trim()
  $p=Get-Content -Raw -LiteralPath (Join-Path $run 'result.json')|ConvertFrom-Json
  if([datetimeoffset]$p.started -lt [datetimeoffset]$r.started){throw 'Stale profile pointer'}
  $r.pending=$p.pending;$r.runs+=@([ordered]@{mode=$item.mode;focusPulse=$item.pulse;run=$run;status=$p.status;pending=$p.pending});Save075
  if($p.status -ne 'PASS_BOTH075_PROFILE_INSTALLED075' -or $p.pending){throw 'Capture/close failed'}
  & 'C:\Users\Xiaomi\AppData\Local\Python\pythoncore-3.15-64\python.exe' (Join-Path $repo 'Build074\tools\Audit-Cached074.py') $run candidate *> (Join-Path $run 'cached-audit074.log')
  if($LASTEXITCODE){throw 'Cached data/EEPROM differs'}
  $rear=Get-Content -Raw -LiteralPath (Join-Path $run 'capture-rear\capture.log')
  if($rear -notmatch 'FOCUS075_SDK_ROUNDTRIP original=0x20100 set_hr=0x00000000 readback=0x20100 normalization=false commit=false'){throw 'Rear SDK exact focus flags roundtrip failed'}
  if($item.pulse){
   if($rear -notmatch 'FOCUS063_TRIGGER[^\r\n]*commit_hr=0x00000000' -or $rear -notmatch 'CONTROL061 restore_focus=0x00000000[^\r\n]*target=0x20100'){throw 'Rear AF pulse/restore failed'}
  }
 }
 $r.crashesAfter=Crashes075
 if(($r.crashesBefore -join ',') -ne ($r.crashesAfter -join ',')){throw 'New FrameServer crash'}
 $r.status='PASS075_BOTH_PREVIEW_VIDEO_SDK_FOCUS_ROUNDTRIP_REAR_AF_RESTORE_NATIVE_CLOSE'
}catch{
 $r.status='FAILED';$r.errors+=@($_.Exception.Message)
 $latest=Join-Path $build 'results\latest-focus-profile075.txt'
 if(Test-Path -LiteralPath $latest){$p=Get-Content -Raw -LiteralPath ((Get-Content -Raw -LiteralPath $latest).Trim()+'\result.json')|ConvertFrom-Json;if([datetimeoffset]$p.started -ge [datetimeoffset]$r.started){$r.pending=$p.pending;$r.pendingPid=$p.pendingPid}}
}finally{$r.completed=(Get-Date -Format o);Save075;if($lock){$lock.Dispose()}}
if($r.status -notlike 'PASS*'){throw ($r.errors -join '; ')}
