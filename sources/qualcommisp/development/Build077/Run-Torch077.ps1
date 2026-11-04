$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$b=Split-Path $PSScriptRoot;$journal=Join-Path $b 'research\torch077.json'
if(Test-Path -LiteralPath $journal){throw 'Preserve single-use077 journal'}
$r=[ordered]@{status='RUNNING';pid=$PID;started=(Get-Date -Format o);driver=75;driverChanges=$false;scope='Short rear public WinRT TorchControl pulse; ON hold250ms; OFF/restore; native close';pending=$false;crashesBefore=@();errors=@()};$lock=$null
function Save077 {$r|ConvertTo-Json -Depth 9|Set-Content -LiteralPath $journal -Encoding UTF8}
function Crashes077 {return @(Get-WinEvent -FilterHashtable @{LogName='Application';Id=1000;StartTime=[datetime]'2026-10-23T22:55:00'} -ErrorAction SilentlyContinue|Where-Object {$_.Message -match 'svchost.exe_FrameServer'}|ForEach-Object RecordId)}
Save077
try{
 $lock=[IO.File]::Open('C:\ProgramData\CodexQispBuild062\operation.lock',[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
 if(@(Get-Process -Name 'WindowsCamera','QispCapture*','QispFocus*','TorchPulse*','DbgEngWitness*','EvStep*','WinRoi*','cdb','tracerpt','devcon','pnputil','Qoder' -ErrorAction SilentlyContinue).Count){throw 'Another owner/decoder present'}
 $state=Get-Content -Raw -LiteralPath 'C:\ProgramData\CodexQispBuild075\active-install.json'|ConvertFrom-Json
 if($state.status -ne 'PASS_NATIVE075_AVS_ACTIVE' -or $state.pending){throw 'Validated075 required'}
 if([datetimeoffset](Get-CimInstance Win32_OperatingSystem).LastBootUpTime -ne [datetimeoffset]$state.boot){throw 'Boot changed'}
 [Reflection.Assembly]::LoadFrom('H:\Vlad\Codex\QualcommISP\Build074\bin\ObserverTypes074.dll')|Out-Null
 . 'H:\Vlad\Codex\QualcommISP\Build075\tools\CameraState.ps1'
 $ret=Get-Content -Raw -LiteralPath 'C:\ProgramData\CodexQispBuild075\retained075.json'|ConvertFrom-Json
 $snapshot=Get-Content -Raw -LiteralPath $ret.packages|ConvertFrom-Json
 foreach($p in $snapshot.packages){QispCheckSavedPackage $p (Split-Path $ret.packages) -Current}
 foreach($f in $ret.files){if((Get-FileHash -LiteralPath $f.live).Hash -ne $f.sha256){throw 'Active tuning changed'}}
 $r.crashesBefore=Crashes077
 for($i=0;(Get-CimInstance Win32_OperatingSystem).FreePhysicalMemory -lt 600*1024;$i++){if($i -ge 15){throw 'Low RAM; no camera open'};Start-Sleep -Seconds 2}
 & (Join-Path $PSScriptRoot 'Profile-Torch077.ps1') -ParentPid $PID
 $run=(Get-Content -Raw -LiteralPath (Join-Path $b 'results\latest-torch077.txt')).Trim()
 $p=Get-Content -Raw -LiteralPath (Join-Path $run 'result.json')|ConvertFrom-Json
 $r.run=$run;$r.pending=$p.pending
 if($p.status -notlike 'PASS077*' -or $p.pending){throw 'Torch API or native close failed'}
 $r.status='PASS077_PUBLIC_TORCH_API_PULSE_OFF_RESTORE_NATIVE_CLOSE_OPTICAL_REVIEW_REQUIRED'
}catch{
 $r.status='FAILED';$r.errors+=@($_.Exception.Message)
 $latest=Join-Path $b 'results\latest-torch077.txt'
 if(Test-Path -LiteralPath $latest){$run=(Get-Content -Raw -LiteralPath $latest).Trim();$p=Get-Content -Raw -LiteralPath (Join-Path $run 'result.json')|ConvertFrom-Json;if([datetimeoffset]$p.started -ge [datetimeoffset]$r.started){$r.run=$run;$r.pending=$p.pending;$r.pendingPid=$p.pendingPid}}
}finally{
 $r.crashesAfter=Crashes077;if(($r.crashesBefore -join ',') -ne ($r.crashesAfter -join ',')){$r.status='FAILED';$r.errors+=@('New FrameServer crash')}
 $r.completed=(Get-Date -Format o);Save077;if($lock){$lock.Dispose()}
}
if($r.status -notlike 'PASS*'){throw ($r.errors -join '; ')}
