$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$root=Split-Path $PSScriptRoot
. 'H:\Vlad\Codex\QualcommISP\Build062\tools\CameraState.ps1'
[Reflection.Assembly]::LoadFrom('H:\Vlad\Codex\QualcommISP\Build074\bin\ObserverTypes074.dll')|Out-Null
$cache='C:\ProgramData\CodexQispBuild075'
if(Test-Path -LiteralPath (Join-Path $cache 'active-install.json')){throw 'Preserve existing075 installation journal'}
$script:qispRun=Join-Path $cache ('install-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
$script:qispRecovery=Join-Path $cache ('rollback062-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
$script:qispBusy=$false;$script:qispRebootRequired=$false;$script:qispPendingPid=0
New-Item -ItemType Directory -Path $script:qispRun -Force|Out-Null
$report=[ordered]@{status='FAILED';started=(Get-Date).ToString('o');scope='Shared focus GET producer only; native sensors/ISP/IFE/IPE unchanged';recovery=$script:qispRecovery;run=$script:qispRun;errors=@();pending=$false;rebootCommand=$false}
$lock=$null;$changed=$false
try{
 $lock=[IO.File]::Open('C:\ProgramData\CodexQispBuild062\operation.lock',[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
 $identity=[Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
 if(-not $identity.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){throw 'Administrator token required'}
 if(@(Get-Process -Name 'WindowsCamera','QispCapture*','QispFocus*','CamProbe*','devcon','pnputil','Qoder','GpuCameraPump','GpuIspHost' -ErrorAction SilentlyContinue).Count){throw 'Conflicting device/camera owner'}
 $state=Get-Content -Raw -LiteralPath 'H:\Vlad\Codex\QualcommISP\Build073\research\postboot073-installation-scope.json'|ConvertFrom-Json
 $os=Get-CimInstance Win32_OperatingSystem
 if([datetimeoffset]$os.LastBootUpTime -ne [datetimeoffset]$state.boot){throw 'Changed boot'}
 if($os.FreePhysicalMemory -lt 600*1024 -or [IO.DriveInfo]::new('C:\').AvailableFreeSpace -lt 1GB){throw 'Insufficient free memory/storage'}
 $bug=Get-WinEvent -FilterHashtable @{LogName='System';Id=1001;ProviderName='Microsoft-Windows-WER-SystemErrorReporting'} -MaxEvents 1
 if($bug.RecordId -ne 120593){throw 'New bugcheck'}
 $report.boot=$os.LastBootUpTime.ToString('o')
 foreach($node in $state.nodes){$n=QispNode $node.id;if($n.version -ne $node.version -or $n.service -ne $node.service -or $n.inf -ne $node.inf -or $n.problem -ne 0){throw 'Camera node state changed'}}
 $m=Get-Content -Raw -LiteralPath (Join-Path $root 'research\signed-candidate075b.json')|ConvertFrom-Json
 if($m.status -ne 'PASS_SIGNED_CANDIDATE_NOT_INSTALLED' -or $m.version -ne '1.0.2324.75'){throw 'Signed075b candidate required'}
 foreach($f in $m.files){if((Get-FileHash -LiteralPath (Join-Path $root ('candidate-focus-b\AVStream\'+$f.name))).Hash -ne $f.sha256){throw 'Candidate changed'}}
 $proof=Get-Content -Raw -LiteralPath (Join-Path $root 'research\focus\focus-get-proof075b.json')|ConvertFrom-Json
 if($proof.status -ne 'PASS075B_FOCUS_GET_MODE_RANGE_FIXED_AUTO_LOCK_PRESERVED' -or @($proof.cases|Where-Object {$_.image -eq 'corrected' -and -not $_.valid}).Count){throw 'Focus proof incomplete'}
 $previous=Get-Content -Raw -LiteralPath (Join-Path $root 'results\Video-default-20261024-004928\result.json')|ConvertFrom-Json
 if($previous.pending -or $previous.status -notlike 'PASS*' -or @($previous.sessions).Count -ne 2 -or @($previous.sessions|Where-Object {-not $_.nativeCloseEvidencePassed}).Count){throw 'Both-camera native close evidence required'}
 if((Get-Service CodexNabuGpuIsp).Status -ne 'Stopped'){throw 'GPU owner active'}
 & (Join-Path $PSScriptRoot 'Snapshot-Baseline075.ps1') -OutputDirectory $script:qispRecovery
 $b=Get-Content -Raw -LiteralPath (Join-Path $script:qispRecovery 'baseline.json')|ConvertFrom-Json
 foreach($p in $b.packages){QispCheckSavedPackage $p $script:qispRecovery -Current}
 Copy-Item -LiteralPath 'H:\Vlad\Codex\QualcommISP\Build062\tools\CameraState.ps1' -Destination $script:qispRecovery
 Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Restore-Avs062.ps1') -Destination $script:qispRecovery
 Copy-Item -LiteralPath 'H:\Vlad\Codex\QualcommISP\Build061\release\tools\devcon.exe' -Destination $script:qispRecovery
 $data=@();$expected=@('23046b914a89f34c870662764a79803c6575dc298f45031ca65ad8c1097731ef','9320b6ce3fb4f4263d7cf2b92f01dbf53d5e553101a941f1216d3cf1ee0b3a5b');$di=0
 foreach($side in 'FrontData','RearData'){
  $dir='C:\ProgramData\CodexQispBuild061\active\package\Unified\'+$side
  $out=Join-Path $script:qispRecovery $side;New-Item -ItemType Directory -Path $out|Out-Null
  foreach($f in Get-ChildItem -LiteralPath $dir -File){
   $sha=(Get-FileHash -LiteralPath $f.FullName).Hash
   if($f.Name -like 'com.qti.tuned*' -and $sha -ne $expected[$di]){throw 'Expected current074 tuning'}
   Copy-Item -LiteralPath $f.FullName -Destination $out
   if((Get-FileHash -LiteralPath (Join-Path $out $f.Name)).Hash -ne $sha){throw 'Data backup differs'}
   $data+=@([ordered]@{path=$f.FullName;backup=(Join-Path $out $f.Name);sha256=$sha;bytes=$f.Length})
  };$di++
 }
 $data|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $script:qispRecovery 'data.json') -Encoding UTF8
 $active=Join-Path $cache 'active';if(Test-Path -LiteralPath $active){throw 'Existing075 active copy preserved'}
 New-Item -ItemType Directory -Path (Join-Path $active 'package\AVStream') -Force|Out-Null
 New-Item -ItemType Directory -Path (Join-Path $active 'tools') -Force|Out-Null
 Copy-Item -Path (Join-Path $root 'candidate-focus-b\AVStream\*') -Destination (Join-Path $active 'package\AVStream')
 foreach($name in 'CameraState.ps1','Check-NativeClose.ps1'){Copy-Item -LiteralPath ('H:\Vlad\Codex\QualcommISP\Build062\tools\'+$name) -Destination (Join-Path $active 'tools')}
 foreach($f in $m.files){if((Get-FileHash -LiteralPath (Join-Path $active ('package\AVStream\'+$f.name))).Hash -ne $f.sha256){throw 'Internal package copy differs'}}
 $svc=Get-Service FrameServer;if($svc.Status -ne 'Stopped'){$svc.Stop();$svc.WaitForStatus([ServiceProcess.ServiceControllerStatus]::Stopped,[TimeSpan]::FromSeconds(30))}
 $avs=@($b.packages|Where-Object label -eq 'avs')[0];$front=@($b.packages|Where-Object label -eq 'front-sensor')[0];$rear=@($b.packages|Where-Object label -eq 'rear-hw')[0]
 $changed=$true
 foreach($p in $rear,$front,$avs){QispEnable $p.id $false ('075-disable-'+$p.label)}
 $sys=@($m.files|Where-Object name -eq 'qccamavs8150.sys')[0]
 QispBind '075-bind-avs' $avs.id (Join-Path $active 'package\AVStream\qccamavs8150.inf') $avs.hardware $m.version $sys.sha256 (Join-Path $script:qispRecovery 'devcon.exe')
 foreach($p in $avs,$front,$rear){QispEnable $p.id $true ('075-enable-'+$p.label)}
 $bound=Split-Path (QispServiceImage 'QCCamAvs')
 foreach($f in $m.files){if((Get-FileHash -LiteralPath (Join-Path $bound $f.name)).Hash -ne $f.sha256){throw 'Bound075 file differs'}}
 $com=(Get-Item -LiteralPath 'Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Classes\CLSID\{4C2331F0-66BE-4177-9841-2FCBA8CCF5CA}\InprocServer32').GetValue('')
 if([Environment]::ExpandEnvironmentVariables($com) -ine (Join-Path $bound 'QcDeviceMFT8150.dll')){throw 'Registered MFT path differs'}
 foreach($p in $b.packages|Where-Object label -ne 'avs'){QispCheckSavedPackage $p $script:qispRecovery -Current}
 foreach($r in $b.registry){$now=QispSaveRegistry $r.path $r.name;if($now.value -cne $r.value -or $now.exists -ne $r.exists){throw 'Data path changed'}}
 foreach($d in $data){if((Get-FileHash -LiteralPath $d.path).Hash -ne $d.sha256){throw '074 runtime data changed'}}
 Start-Service FrameServer
 $report.nodes=@($state.nodes|ForEach-Object {QispNode $_.id});$report.active=$active;$report.native061DataRoot=$state.native061DataRoot;$report.registeredMFT=$com;$report.candidateManifest=(Join-Path $root 'research\signed-candidate075b.json');$report.status='PASS_NATIVE075_AVS_ACTIVE_NOT_YET_CAMERA_VALIDATED'
 $report|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $cache 'active-install.json') -Encoding UTF8
}catch{
 $report.errors+=@($_.Exception.Message)
 if($changed -and -not $script:qispBusy -and -not $script:qispRebootRequired){
  try{& (Join-Path $script:qispRecovery 'Restore-Avs062.ps1') -RecoveryDirectory $script:qispRecovery -LockAlreadyHeld;$report.restored062=$true}catch{$report.errors+=@('Rollback: '+$_.Exception.Message)}
 }
}finally{
 $report.pending=$script:qispBusy;$report.pendingPid=$script:qispPendingPid;$report.rebootRequired=$script:qispRebootRequired;$report.completed=(Get-Date).ToString('o')
 $report|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $script:qispRun 'install-result075.json') -Encoding UTF8
 $report|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $root 'research\install-result075.json') -Encoding UTF8
 if($lock){$lock.Dispose()}
}
if($report.status -notlike 'PASS*'){throw ($report.errors -join '; ')}
