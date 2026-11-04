$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$repo='H:\Vlad\Codex\QualcommISP';$build=Join-Path $repo 'Build075'
$journal=Join-Path $build 'research\depth4075.json'
if(Test-Path -LiteralPath $journal){throw 'Single-use observer'}
if(-not ([Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){throw 'Trace token required'}
[Reflection.Assembly]::LoadFrom((Join-Path $build 'bin\ObserverTypes074.dll')) | Out-Null
$r=[ordered]@{status='RUNNING';pid=$PID;started=(Get-Date -Format o);deviceChanges=$false;readQueueDepth=4;MFTTrace=$false;nativeCloseTrace=$true;pending=$false;runs=@();errors=@()};$lock=$null
function Save074 {$r | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $journal -Encoding UTF8}
function Crash074 {return @(Get-WinEvent -FilterHashtable @{LogName='Application';Id=1000;StartTime=[datetime]'2026-10-23T22:55:00'} -ErrorAction SilentlyContinue | Where-Object {$_.Message -match 'svchost.exe_FrameServer'} | ForEach-Object RecordId)}
Save074
try {
 $lock=[IO.File]::Open('C:\ProgramData\CodexQispBuild062\operation.lock',[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
 $trial=Get-Content -LiteralPath (Join-Path $repo 'Build074\research\trial074b.json') -Raw | ConvertFrom-Json
 if($trial.status -notlike 'PASS074_CONVERGENCE*' -or $trial.pending -or $trial.lastApplied -ne 'candidate'){throw 'Completed trial074b candidate required'}
 $r.crashesBefore=Crash074
 foreach($mode in 'Video') {
  foreach($f in $trial.files){if((Get-FileHash -LiteralPath $f.live).Hash -ne $f.candidateSHA){throw 'Active candidate changed'}}
  for($i=0;(Get-CimInstance Win32_OperatingSystem).FreePhysicalMemory -lt 600*1024;$i++){if($i -ge 15){throw 'Low RAM; no camera open'};Start-Sleep -Seconds 2}
  & (Join-Path $build 'tools\Profile-Both075.ps1') -Mode $mode -QueueDepth 4 -InspectTuning -InstallationStateFile (Join-Path $repo 'Build073\research\postboot073-installation-scope.json')
  $run=(Get-Content -LiteralPath (Join-Path $build 'results\latest-profile074.txt') -Raw).Trim()
  $p=Get-Content -LiteralPath (Join-Path $run 'result.json') -Raw | ConvertFrom-Json
  $r.pending=$p.pending;$r.runs+=@([ordered]@{mode=$mode;run=$run;status=$p.status;pending=$p.pending});Save074
  if($p.status -ne 'PASS_BOTH063_PROFILE_INSTALLED062' -or $p.pending){throw 'Capture/closure failed'}
  & 'C:\Users\Xiaomi\AppData\Local\Python\pythoncore-3.15-64\python.exe' (Join-Path $repo 'Build074\tools\Audit-Cached074.py') $run candidate *> (Join-Path $run 'cached-audit074.log')
  if($LASTEXITCODE){throw 'Actual cached candidate/EEPROM differs'}
 }
 $r.crashesAfter=Crash074
 if(($r.crashesBefore -join ',') -ne ($r.crashesAfter -join ',')){throw 'New FrameServer crash'}
 $r.status='PASS075_BOTH_VIDEO_DEPTH4_NATIVE_CLOSE'
} catch {
 $r.status='FAILED';$r.errors+=@($_.Exception.Message)
 $latest=Join-Path $build 'results\latest-profile074.txt'
 if(Test-Path -LiteralPath $latest){$run=(Get-Content -LiteralPath $latest -Raw).Trim();$p=Get-Content -LiteralPath (Join-Path $run 'result.json') -Raw|ConvertFrom-Json;$r.pending=$p.pending}
} finally {$r.completed=(Get-Date -Format o);Save074;if($lock){$lock.Dispose()}}
