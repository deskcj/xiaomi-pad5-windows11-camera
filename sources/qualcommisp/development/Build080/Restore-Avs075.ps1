param([Parameter(Mandatory=$true)][string]$RecoveryDirectory,[switch]$LockAlreadyHeld)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'CameraState.ps1')
[Reflection.Assembly]::LoadFrom((Join-Path $PSScriptRoot 'ObserverTypes074.dll'))|Out-Null
$script:qispRecovery=[IO.Path]::GetFullPath($RecoveryDirectory)
if(-not $script:qispRecovery.StartsWith('C:\ProgramData\CodexQispBuild080\',[StringComparison]::OrdinalIgnoreCase)){throw 'Wrong recovery directory'}
$script:qispRun=$script:qispRecovery;$script:qispBusy=$false;$script:qispRebootRequired=$false;$script:qispPendingPid=0
$lock=$null;$report=[ordered]@{status='FAILED';started=(Get-Date).ToString('o');scope='Restore AVStream075; retain the exact pre080 data files';errors=@();pending=$false}
try{
 if(-not $LockAlreadyHeld){$lock=[IO.File]::Open('C:\ProgramData\CodexQispBuild062\operation.lock',[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)}
 if(@(Get-Process -Name 'WindowsCamera','QispCapture*','QispFocus*','CamProbe*','devcon','pnputil','Qoder' -ErrorAction SilentlyContinue).Count){throw 'Owner or PnP process still present'}
 $b=Get-Content -Raw -LiteralPath (Join-Path $script:qispRecovery 'baseline.json')|ConvertFrom-Json
 if([datetimeoffset](Get-CimInstance Win32_OperatingSystem).LastBootUpTime -ne [datetimeoffset]$b.lastBoot){throw 'Changed boot; revalidate before using recovery'}
 foreach($p in $b.packages){QispCheckSavedPackage $p $script:qispRecovery}
 $data=Get-Content -Raw -LiteralPath (Join-Path $script:qispRecovery 'data.json')|ConvertFrom-Json
 foreach($d in $data){if((Get-FileHash -LiteralPath $d.path).Hash -ne $d.sha256){throw 'Data changed after snapshot; preserve it for review'}}
 $svc=Get-Service FrameServer;if($svc.Status -ne 'Stopped'){$svc.Stop();$svc.WaitForStatus([ServiceProcess.ServiceControllerStatus]::Stopped,[TimeSpan]::FromSeconds(30))}
 if((Get-Service CodexNabuGpuIsp).Status -ne 'Stopped'){throw 'GPU owner active'}
 $avs=@($b.packages|Where-Object label -eq 'avs')[0]
 $front=@($b.packages|Where-Object label -eq 'front-sensor')[0];$rear=@($b.packages|Where-Object label -eq 'rear-hw')[0]
 foreach($p in $rear,$front,$avs){QispEnable $p.id $false ('rollback-disable-'+$p.label)}
 $sys=@($avs.files|Where-Object name -eq $avs.sys)[0]
 QispBind 'rollback-bind-avs075' $avs.id (Join-Path $script:qispRecovery ('avs\'+$avs.inf)) $avs.hardware $avs.version $sys.sha256 (Join-Path $script:qispRecovery 'devcon.exe')
 foreach($r in $b.registry){QispRestoreRegistry $r}
 foreach($p in $avs,$front,$rear){QispEnable $p.id ($p.problem -eq 0) ('rollback-enable-'+$p.label)}
 foreach($p in $b.packages){QispCheckSavedPackage $p $script:qispRecovery -Current}
 if($b.frameServer -eq 'Running'){Start-Service FrameServer}
 $report.status='PASS_AVSTREAM075_EXACT_PRE080_STATE_RESTORED'
}catch{$report.errors+=@($_.Exception.Message)}finally{
 $report.pending=$script:qispBusy;$report.pendingPid=$script:qispPendingPid;$report.rebootRequired=$script:qispRebootRequired;$report.completed=(Get-Date).ToString('o')
 $report|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $script:qispRecovery 'restore075.json') -Encoding UTF8
 if($lock){$lock.Dispose()}
}
if($report.status -notlike 'PASS*'){throw ($report.errors -join '; ')}
