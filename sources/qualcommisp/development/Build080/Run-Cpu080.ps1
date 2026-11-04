$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$b=Split-Path $PSScriptRoot;$repo=Split-Path $b;$journal=Join-Path $b 'research\cpu080.json'
if(Test-Path -LiteralPath $journal){throw 'Single use profiler'}
$r=[ordered]@{status='RUNNING';pid=$PID;started=(Get-Date -Format o);driver=80;driverChanges=$false;scope='One bounded front/rear VIDEO with CPU samples/CSwitch and no tuning changes';pending=$false;nativePending=$false;wprCommandPending=$false;wprOwned=$false;errors=@()};$lock=$null
$scope=Join-Path $b ('results\cpu-080-'+(Get-Date -Format yyyyMMdd-HHmmss));New-Item -ItemType Directory -Path $scope|Out-Null
$r.scopeDirectory=$scope
function Save079 {$r|ConvertTo-Json -Depth 8|Set-Content -LiteralPath $journal -Encoding UTF8}
function Wpr079([string]$Label,[string[]]$Arguments){
 $p=Start-Process -FilePath 'C:\Windows\System32\wpr.exe' -ArgumentList $Arguments -WindowStyle Hidden -RedirectStandardOutput (Join-Path $scope ($Label+'.log')) -RedirectStandardError (Join-Path $scope ($Label+'.err')) -PassThru
 $null=$p.Handle;$r.wprCommandPid=$p.Id;$r.wprCommandPending=$true;Save079
 if(-not $p.WaitForExit(45000)){throw 'WPR command pending; no overlapping recorder action'}
 $p.WaitForExit();$p.Refresh();$r.wprCommandPending=$false;Save079
 if($p.ExitCode){throw ('WPR '+$Label+' failed exit='+$p.ExitCode)}
 return [IO.File]::ReadAllText((Join-Path $scope ($Label+'.log')))
}
Save079
try{
 $lock=[IO.File]::Open('C:\ProgramData\CodexQispBuild062\operation.lock',[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
 if(@(Get-Process -Name 'WindowsCamera','QispCapture*','QispFocus*','TorchPulse*','DbgEngWitness*','cdb','tracerpt','devcon','pnputil','Qoder','wpr' -ErrorAction SilentlyContinue).Count){throw 'Another owner/recorder command'}
 $state=Get-Content -Raw -LiteralPath 'C:\ProgramData\CodexQispBuild080\active-install.json'|ConvertFrom-Json
 if($state.status -notin @('PASS_NATIVE080_AVS_ACTIVE_NOT_YET_CAMERA_VALIDATED','PASS_NATIVE080_AVS_ACTIVE') -or $state.pending){throw 'Installed080 required'}
 $ret=Get-Content -Raw -LiteralPath 'C:\ProgramData\CodexQispBuild075\retained075.json'|ConvertFrom-Json
 foreach($f in $ret.files){if((Get-FileHash -LiteralPath $f.live).Hash -ne $f.sha256){throw 'Current tuning changed'}}
 if([datetimeoffset](Get-CimInstance Win32_OperatingSystem).LastBootUpTime -ne [datetimeoffset]$state.boot){throw 'Boot changed'}
 for($i=0;(Get-CimInstance Win32_OperatingSystem).FreePhysicalMemory -lt 650*1024;$i++){if($i -ge 15){throw 'Low memory; no recorder/camera'};Start-Sleep -Seconds 2}
 $status=Wpr079 'status-before' @('-status')
 if($status -notmatch 'WPR is not recording'){throw 'Existing recorder; leave it untouched'}
 $temp=Join-Path $scope 'temp';New-Item -ItemType Directory -Path $temp|Out-Null
 $profile=(Join-Path $b 'research\QispCpu080.wprp')+'!QispCpu080.Light'
 $null=Wpr079 'start' @('-start',$profile,'-filemode','-recordtempto',$temp)
 $r.wprOwned=$true;Save079
 $status=Wpr079 'status-owned' @('-status','profiles')
 if($status -notmatch 'QispCpu080'){throw 'New recorder ownership not confirmed'}
 & (Join-Path $PSScriptRoot 'Profile-Both080.ps1') -Mode Video -QueueDepth 2 -InspectTuning
 $run=(Get-Content -Raw -LiteralPath (Join-Path $b 'results\latest-profile080.txt')).Trim()
 $p=Get-Content -Raw -LiteralPath (Join-Path $run 'result.json')|ConvertFrom-Json
 $r.run=$run;$r.nativePending=$p.pending
 if($p.status -ne 'PASS_BOTH080_PROFILE_INSTALLED080' -or $p.pending){throw 'Native capture/close failed'}
 & 'C:\Users\Xiaomi\AppData\Local\Python\pythoncore-3.15-64\python.exe' (Join-Path $repo 'Build074\tools\Audit-Cached074.py') $run candidate *> (Join-Path $run 'cached-audit074.log')
 if($LASTEXITCODE){throw 'Actual cached tuning/EEPROM differs'}
 $r.nativeCapturePassed=$true
}catch{
 $r.status='FAILED';$r.errors+=@($_.Exception.Message)
 $latest=Join-Path $b 'results\latest-profile080.txt'
 if(Test-Path -LiteralPath $latest){$p=Get-Content -Raw -LiteralPath ((Get-Content -Raw -LiteralPath $latest).Trim()+'\result.json')|ConvertFrom-Json;if([datetimeoffset]$p.started -ge [datetimeoffset]$r.started){$r.nativePending=$p.pending;$r.nativePendingPid=$p.pendingPid}}
}finally{
 if($r.wprOwned -and -not $r.wprCommandPending){
  try{
   $etl=Join-Path $scope 'cpu080.etl';$null=Wpr079 'stop' @('-stop',$etl,'QISP080','-skipPdbGen')
   $r.wprOwned=$false;$r.etl=$etl
   $r.recorderAfter=Wpr079 'status-after' @('-status')
  }catch{$r.errors+=@('Recorder finalization: '+$_.Exception.Message)}
 }
 $r.pending=$r.nativePending -or $r.wprCommandPending -or $r.wprOwned
 if(-not $r.pending -and $r.errors.Count -eq 0 -and $r.nativeCapturePassed){$r.status='PASS080_BOTH_NATIVE_VIDEO_CLOSED_CPU_TRACE_RECORDED'}elseif($r.status -eq 'RUNNING'){$r.status='FAILED'}
 $r.completed=(Get-Date -Format o);Save079;if($lock){$lock.Dispose()}
}
if($r.status -notlike 'PASS*'){throw ($r.errors -join '; ')}
