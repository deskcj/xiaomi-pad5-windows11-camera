# New read-only baseline after the user returned from Android; no historical guards edited.
$ErrorActionPreference='Stop'; Set-StrictMode -Version Latest
$b='H:\Vlad\Codex\QualcommISP\Build243'; $repo=Split-Path -Parent $b
$out=Join-Path $b 'research\pre-adopt-state243.json'
if(Test-Path -LiteralPath $out){throw 'Single-use216 audit: preserve existing receipt'}
$r=[ordered]@{status='FAILED226_READONLY_STATE';started=(Get-Date -Format o);pid=$PID;errors=@();deviceActions=$false;processActions=$false;tuningChanges=$false;historicalReferenceModified=$false;androidParity=$false};$lock=$null
try{
 if(-not ([Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){throw 'Elevated read-only visibility required'}
 $os=Get-CimInstance Win32_OperatingSystem
 $r.boot=([datetimeoffset]$os.LastBootUpTime).ToString('o');$r.freeMemoryMiB=[math]::Round($os.FreePhysicalMemory/1024,1)
 $r.owners=@(Get-CimInstance Win32_Process|Where-Object {$_.Name -match '^(QispCapture|CamProbe|WindowsCamera|WinRoi|WinFocus|ReadMft|tracerpt|java|devcon|pnputil|Qoder)'}|Select-Object ProcessId,CreationDate,Name,CommandLine)
 $r.frameServer=(Get-Service FrameServer).Status.ToString()
 if($r.owners.Count -or $r.frameServer -ne 'Stopped'){throw 'Camera/device/decoder owner or FrameServer active'}
 $lock=[IO.File]::Open('C:\ProgramData\CodexQispBuild062\operation.lock',[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
 $r.lockAvailable=$true
 . (Join-Path $repo 'Build196\tools\CameraState196.ps1')
 . (Join-Path $repo 'Build207\tools\Query-QispTraces207.ps1') -ImportOnly
 $r.etw=Assert-NoQispTraces207
 $refpath='C:\ProgramData\CodexQispBuild241\test-reference241.json'
 $ref=Get-Content -LiteralPath $refpath -Raw|ConvertFrom-Json
 if([datetimeoffset]$os.LastBootUpTime -ne [datetimeoffset]$ref.boot){throw 'Accepted223 boot changed'}
 $r.referencePath=$refpath;$r.referenceSHA=(Get-FileHash -LiteralPath $refpath).Hash;$r.referenceHistoricalBoot=$ref.boot
 if($ref.pending -or $ref.dataReference -ne 241 -or $ref.installedDriver -ne 80 -or $ref.deviceMFT -ne 75){throw 'Accepted167 identity differs'}
 $prior=Get-Content -LiteralPath (Join-Path $repo 'Build213\research\operation213.json') -Raw|ConvertFrom-Json
 if($prior.pending -or $prior.groupActive -or -not $prior.restored080 -or $prior.errors.Count){throw 'Historical213 not closed/restored'}
 if(Test-Path -LiteralPath (Join-Path $repo 'Build214\research\trial214.json')){throw 'Unexpected214 runtime: review independently first'}
 $r.dataFiles=@();foreach($f in $ref.files){foreach($p in @($f.live,$f.backup)){if((Get-FileHash -LiteralPath $p).Hash -ne $f.sha256 -or (Get-Item -LiteralPath $p).Length -ne $f.bytes){throw ('167 file differs: '+$p)}};$r.dataFiles+=@($f)}
 $r.nodes=@();foreach($n in $ref.nodes){$a=QispNode $n.id;if($a.problem -ne 0 -or $a.version -ne $n.version -or $a.inf -ne $n.inf -or $a.service -ne $n.service){throw ('167 binding differs: '+$n.id)};$r.nodes+=@($a)}
 $kernel=QispServiceImage 'QCCamAvs';$r.kernelPath=$kernel;$r.kernelSHA=(Get-FileHash -LiteralPath $kernel).Hash
 if($r.kernelSHA -ne $ref.signedKernelSHA256){throw 'SYS080 differs'}
 $mft=(Get-Item -LiteralPath 'Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Classes\CLSID\{4C2331F0-66BE-4177-9841-2FCBA8CCF5CA}\InprocServer32').GetValue('')
 $r.mftPath=$mft;$r.mftSHA=(Get-FileHash -LiteralPath $mft).Hash
 if($r.mftSHA -ne $ref.signedMFTSHA256){throw 'MFT075b differs'}
 $old=Get-Content -LiteralPath (Join-Path $repo 'Build080\research\signed080.json') -Raw|ConvertFrom-Json
 $r.packageFiles=@();foreach($f in $old.files){$p=Join-Path (Split-Path -Parent $kernel) $f.name;$hash=(Get-FileHash -LiteralPath $p).Hash;if($hash -ne $f.sha256){throw ('080 package differs: '+$p)};$r.packageFiles+=@([ordered]@{path=$p;sha256=$hash})}
 $bug=Get-WinEvent -FilterHashtable @{LogName='System';Id=1001;ProviderName='Microsoft-Windows-WER-SystemErrorReporting'} -MaxEvents 1
 $r.bugCheckRecord=$bug.RecordId;$r.bugCheckTime=$bug.TimeCreated.ToString('o')
 if($bug.RecordId -ne 120593){throw 'New BugCheck since last accepted baseline; inspect before capture'}
 $r.crashes=@(Get-WinEvent -FilterHashtable @{LogName='Application';Id=1000;StartTime=$os.LastBootUpTime} -ErrorAction SilentlyContinue|Where-Object {$_.Message -match 'svchost.exe_FrameServer|QcDeviceMFT8150.dll'}|Select-Object RecordId,TimeCreated,Message)
 if($r.crashes.Count){throw 'FrameServer/MFT crash this boot'}
 [Reflection.Assembly]::LoadFrom((Join-Path $repo 'Build074\bin\ObserverTypes074.dll'))|Out-Null
 $r.debugFilter=[Qisp061DeployFilter]::NtQueryDebugFilterState(0x51,3)
 $r.battery=@(Get-CimInstance Win32_Battery|Select-Object Name,BatteryStatus,EstimatedChargeRemaining)
 $r.volumes=@(Get-Volume -DriveLetter C,H|Select-Object DriveLetter,SizeRemaining,Size)
 $r.smmu=Get-ItemProperty -LiteralPath 'Registry::HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Services\qcsmmu\Parameters' -ErrorAction SilentlyContinue|Select-Object * -ExcludeProperty PSPath,PSParentPath,PSChildName,PSDrive,PSProvider
 $r.status='PASS226_CURRENT080_075B_DATA241_TEST_VARIANT_NO_OWNERS_NO_NEW_CRASH_READONLY'
 $r.dataReference=223;$r.installedDriver=80;$r.deviceMFT=75
 $r.captureMemoryGateMiB=650;$r.captureMemoryGateCurrentlyPass=($r.freeMemoryMiB -ge 650)
}catch{$r.errors+=@($_.Exception.Message);$r.position=$_.InvocationInfo.PositionMessage}
finally{if($lock){$lock.Dispose()};$r.completed=(Get-Date -Format o);$r|ConvertTo-Json -Depth 10|Set-Content -LiteralPath $out -Encoding UTF8}
if($r.status -notlike 'PASS*'){throw ($r.errors -join '; ')}
