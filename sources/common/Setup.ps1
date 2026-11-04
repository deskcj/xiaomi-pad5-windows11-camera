[CmdletBinding()]
param([ValidateSet('Install','Check','Diagnose')][string]$Mode='Install',[string]$ResultRoot='',[switch]$Elevated)
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
if($PSVersionTable.PSEdition -eq 'Desktop'){$env:PSModulePath=(Join-Path $PSHOME 'Modules')+';'+[Environment]::GetEnvironmentVariable('PSModulePath',[EnvironmentVariableTarget]::Machine)}
$root=$PSScriptRoot;$profile=Get-Content -LiteralPath (Join-Path $root 'Profile.json') -Raw|ConvertFrom-Json
$admin=([Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if(-not $admin -and $Mode -ne 'Check'){
 if($Elevated){throw 'Administrator elevation did not succeed. Run Install-QualcommISP.cmd as administrator.'}
 $args=@('-NoProfile','-ExecutionPolicy','Bypass','-File',('"'+$PSCommandPath+'"'),'-Mode',$Mode,'-Elevated');if($ResultRoot){$args+=@('-ResultRoot',('"'+$ResultRoot+'"'))}
 Write-Host 'Running elevated camera setup. Please wait; logs are saved to C:\ProgramData\XiaomiPad5Camera\Logs.'
 $p=Start-Process -FilePath "$env:windir\System32\WindowsPowerShell\v1.0\powershell.exe" -ArgumentList $args -Verb RunAs -WindowStyle Hidden -Wait -PassThru
 $statePath='C:\ProgramData\XiaomiPad5Camera\state.json'
 if(Test-Path -LiteralPath $statePath){$childState=Get-Content -LiteralPath $statePath -Raw|ConvertFrom-Json;if($childState.pid -eq $p.Id){Write-Host ('Status: '+$childState.status);if($childState.PSObject.Properties['resultDirectory']){Write-Host ('Log: '+$childState.resultDirectory)};foreach($message in $childState.errors){Write-Host $message}}}
 exit $p.ExitCode
}
$base='C:\ProgramData\XiaomiPad5Camera';$locks=@();$sessions=@();$cache='';$run='';$exitCode=1
$r=[ordered]@{schema=1;profile=$profile.id;dataReference=$profile.dataReference;mode=$Mode;started=(Get-Date -Format o);pid=$PID;status='PREPARING';pendingPid=0;pendingCreation='';pendingKind='';unsafeToContinue=$false;captureClosed=$true;fullMftDecoded=$true;driverActions=$false;registryActions=$false;certificatesImported=$false;rebootInvoked=$false;rebootRequired=$false;captures=@();steps=@();errors=@()}
Add-Type -TypeDefinition @"
using System;using System.Runtime.InteropServices;
public static class PadCameraNative {
 public static void WriteAtomic(string path,string json){string tmp=path+"."+System.Diagnostics.Process.GetCurrentProcess().Id+".tmp";System.IO.File.WriteAllText(tmp,json,new System.Text.UTF8Encoding(false));if(System.IO.File.Exists(path))System.IO.File.Replace(tmp,path,null);else System.IO.File.Move(tmp,path);}
 [DllImport("cfgmgr32.dll",CharSet=CharSet.Unicode)] public static extern uint CM_Locate_DevNodeW(out uint devinst,string id,uint flags);
 [DllImport("cfgmgr32.dll")] public static extern uint CM_Get_DevNode_Status(out uint status,out uint problem,uint devinst,uint flags);
 [DllImport("ntdll.dll")] public static extern int NtQueryDebugFilterState(uint component,uint level);
 [DllImport("ntdll.dll")] public static extern int NtSetDebugFilterState(uint component,uint level,byte enabled);
}
"@
function Write-AtomicJson([string]$path,[string]$json){[PadCameraNative]::WriteAtomic($path,$json)}
function Save-State {
 if(-not $run){return};$r.updated=(Get-Date -Format o);$json=$r|ConvertTo-Json -Depth 16
 Write-AtomicJson (Join-Path $run 'result.json') $json
 if($Mode -ne 'Check'){Write-AtomicJson (Join-Path $base 'state.json') $json}
}
function Append-SetupLog([string]$text){
 $path=Join-Path $run 'setup.log'
 for($attempt=0;;++$attempt){
  $stream=$null;$writer=$null
  try{
   $stream=[IO.File]::Open($path,[IO.FileMode]::Append,[IO.FileAccess]::Write,[IO.FileShare]::ReadWrite)
   $writer=[IO.StreamWriter]::new($stream,[Text.UTF8Encoding]::new($false))
   $writer.WriteLine($text);$writer.Flush();return
  }catch [IO.IOException]{
   $code=$_.Exception.HResult -band 0xffff
   if($code -notin 32,33 -or $attempt -ge 20){throw}
   Start-Sleep -Milliseconds 100
  }finally{if($writer){$writer.Dispose()}elseif($stream){$stream.Dispose()}}
 }
}
function Note([string]$text){$line=(Get-Date -Format o)+' '+$text;Write-Host $line;if($run){Append-SetupLog $line};Save-State}
function Problem([string]$id){$dev=[uint32]0;$status=[uint32]0;$problem=[uint32]0;if([PadCameraNative]::CM_Locate_DevNodeW([ref]$dev,$id,0) -ne 0 -or [PadCameraNative]::CM_Get_DevNode_Status([ref]$status,[ref]$problem,$dev,0) -ne 0){return $null};return [int]$problem}
function Node([string]$id){
 $key=Get-Item -LiteralPath ('Registry::HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Enum\'+$id) -ErrorAction Stop;$driver=$key.GetValue('Driver','');$version='';$inf=''
 if($driver){$c=Get-Item -LiteralPath ('Registry::HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\Class\'+$driver);$version=$c.GetValue('DriverVersion','');$inf=$c.GetValue('InfPath','')}
 [pscustomobject]@{id=$id;problem=(Problem $id);service=$key.GetValue('Service','');version=$version;inf=$inf;hardware=@($key.GetValue('HardwareID',@()))}
}
function Find-Physical([string]$slot){
 $parents=@{isp='ACPI\QCOM0528';mipi='ACPI\QCOM05A4';front='ACPI\QCOM0506';rear='ACPI\QCOM0529';avs='DISPLAY\QCOM_AVStream_8150'};$parent=$parents[$slot];$found=@()
 foreach($k in @(Get-ChildItem -LiteralPath ('Registry::HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Enum\'+$parent) -ErrorAction SilentlyContinue)){$id=$parent+'\'+$k.PSChildName;if($null -ne (Problem $id)){$found+=@(Node $id)}}
 if($found.Count -ne 1){throw ('Expected one NABU '+$slot+' device, found '+$found.Count)};return $found[0]
}
function Virtual-Nodes {
 $found=@();foreach($k in @(Get-ChildItem -LiteralPath 'Registry::HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Enum\ROOT\CAMERA' -ErrorAction SilentlyContinue)){
  $id='ROOT\CAMERA\'+$k.PSChildName;if($null -eq (Problem $id)){continue};$n=Node $id;if(@($n.hardware|Where-Object {$_ -in 'ROOT\NABUCAMVIRTUALFRONT','ROOT\NABUCAMVIRTUALREAR'}).Count){$found+=@($n)}
 };return $found
}
function Service-Image([string]$service){$p=(Get-Item -LiteralPath ('Registry::HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Services\'+$service)).GetValue('ImagePath','');$p=[Environment]::ExpandEnvironmentVariables($p).Replace('\SystemRoot',$env:windir);if($p.StartsWith('\??\')){$p=$p.Substring(4)};return $p.Trim('"')}
function Assert-Idle {
 for($i=0;(Get-Service FrameServer).Status -ne 'Stopped';++$i){if($i -ge 120){throw 'Close camera preview/recording in Camera, Telegram or other apps, then run Setup again.'};Start-Sleep -Seconds 1}
 $owners=@(Get-CimInstance Win32_Process|Where-Object {$_.Name -match '^(WindowsCamera|CameraProbe|QispCapture|CamProbe|LiveRawCapture|FrontRawGrab|ReadMft|tracerpt|devcon|pnputil|java|LiveRender|GpuIspBench|Qoder)(\.exe|\d|$)'})
 if($owners.Count){throw ('Camera/device/decoder owner is active: '+(($owners.Name|Sort-Object -Unique)-join ', '))}
 foreach($n in @((Find-Physical 'mipi'),(Find-Physical 'rear'))+@(Virtual-Nodes)){
  if($n.service -notlike 'NabuCam*'){continue};$k=Get-Item -LiteralPath ('Registry::HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Enum\'+$n.id+'\Device Parameters') -ErrorAction SilentlyContinue;if(-not $k){continue}
  foreach($field in 'NabuOwn_PersistentSessionState','NabuRearHw_PersistentSessionState','NabuAv_ActivePinCount','NabuAvRear_ActivePinCount','NabuAv_RawConversionBusy','NabuAvRear_RawConversionBusy'){if($k.GetValueNames() -contains $field -and [long]$k.GetValue($field) -ne 0){throw ('Camera still owns buffers: '+$n.id+' '+$field)}}
 }
}
function Run-Child([string]$label,[string]$exe,[string[]]$arguments,[int]$timeout=60000){
 $safe=$label -replace '[^A-Za-z0-9_-]','_';$output=Join-Path $run ($safe+'.out.log');$errorFile=Join-Path $run ($safe+'.err.log');if(Test-Path -LiteralPath $output){throw 'Repeated step would overwrite evidence'}
 Note ('BEGIN '+$label);$p=Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -RedirectStandardOutput $output -RedirectStandardError $errorFile -PassThru;$null=$p.Handle
 $r.pendingPid=$p.Id;$r.pendingCreation=$p.StartTime.ToString('o');$r.pendingKind=$label;Save-State
 if(-not $p.WaitForExit($timeout)){$r.unsafeToContinue=$true;Save-State;throw ('Operation is still running; do not start another installer. PID='+$p.Id+' '+$label)}
 $p.WaitForExit();$p.Refresh();$code=$p.ExitCode;if($null -eq $code){$r.unsafeToContinue=$true;throw 'Missing process completion status'}
 $r.pendingPid=0;$r.pendingCreation='';$r.pendingKind='';Save-State;$text=[IO.File]::ReadAllText($output)+[IO.File]::ReadAllText($errorFile)
 if($code -in 3010,3011 -or ((Split-Path -Leaf $exe) -eq 'devcon.exe' -and $code -eq 1)){$r.rebootRequired=$true;throw ('Windows requested a restart for '+$label+'. Setup will not restart this tablet.')}
 if($code -ne 0){throw ($label+' failed with exit '+$code+'. See '+$output)};Note ('OK '+$label);return $text
}
function Enable-Node([string]$id,[bool]$enabled,[string]$label){
 Assert-Idle;$p=Problem $id;if($null -eq $p){throw ('Device disappeared: '+$id)};$expected=if($enabled){0}else{22}
 if($p -ne $expected){
  $r.driverActions=$true
  if($enabled -and $id -like 'ROOT\CAMERA\*'){
   # PnPUtil returned ERROR_DEVICE_NOT_CONNECTED for present disabled root nodes.
   # Exact-instance DevCon enable was verified on both existing nodes, without reboot.
   $toolRoot=if($cache){$cache}else{$root}
   $null=Run-Child $label (Join-Path $toolRoot 'Tools\devcon.exe') @('enable',('"@'+$id+'"'))
  }else{
   $switch=if($enabled){'/enable-device'}else{'/disable-device'}
   $null=Run-Child $label (Join-Path $env:windir 'System32\pnputil.exe') @($switch,('"'+$id+'"'))
  }
 }
 if((Problem $id) -ne $expected){throw ('Unexpected device state after '+$label)}
}
function Verify-Package {
 $m=Get-Content -LiteralPath (Join-Path $root 'MANIFEST-SHA256.json') -Raw|ConvertFrom-Json
 foreach($f in $m.files){$path=[IO.Path]::GetFullPath((Join-Path $root $f.path));if(-not $path.StartsWith($root.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Invalid manifest path'}
  if((Get-Item -LiteralPath $path).Length -ne $f.bytes -or (Get-FileHash -LiteralPath $path).Hash -ne $f.sha256){throw ('Archive is incomplete or modified: '+$f.path)}
 };return $m
}
function Stop-Gpu {
 $svc=Get-Service CodexNabuGpuIsp -ErrorAction SilentlyContinue;if($svc -and $svc.Status -ne 'Stopped'){Note 'Stopping GPU workers normally';Stop-Service CodexNabuGpuIsp -NoWait;$svc.WaitForStatus('Stopped',[TimeSpan]::FromSeconds(45))}
 $until=[datetime]::UtcNow.AddSeconds(15);while(@(Get-Process GpuCameraPump -ErrorAction SilentlyContinue).Count -and [datetime]::UtcNow -lt $until){Start-Sleep -Milliseconds 100}
 if(@(Get-Process GpuCameraPump -ErrorAction SilentlyContinue).Count){$r.unsafeToContinue=$true;throw 'GPU workers are not closed; no device changes permitted'}
 if($svc){$null=Run-Child 'gpu-demand-start' (Join-Path $env:windir 'System32\sc.exe') @('config','CodexNabuGpuIsp','start=','demand')};Assert-Idle
}
function Memory-Guard {
 $free=[math]::Floor((Get-CimInstance Win32_OperatingSystem).FreePhysicalMemory/1024);$minimum=if($profile.id -eq 'GPUISP'){900}else{650}
 if($free -lt $minimum){throw ("Available RAM is "+$free+" MiB; close unused apps to free at least "+$minimum+" MiB and retry.")}
 if($Mode -eq 'Install' -and (Get-PSDrive C).Free -lt 1GB){throw 'Free at least 1 GiB on C: before installing.'}
 if($Mode -eq 'Diagnose'){$drive=[IO.DriveInfo]::new([IO.Path]::GetPathRoot($run));if($drive.AvailableFreeSpace -lt 1GB){throw 'Free at least 1 GiB on the diagnostic output drive.'}}
}
function Capture-One([string]$side,[string]$label,[bool]$native,[bool]$ReleaseOnly=$false){
 Assert-Idle;Memory-Guard
 $device=$null
 if($native){$inventory=Get-CameraInventory $label;$device=Select-NativeCamera $inventory (Find-Physical 'avs').id $side}
 $d=Join-Path $run $label;New-Item -ItemType Directory -Path $d|Out-Null
 $toolRoot=if($cache){$cache}else{$root};$trace=Join-Path $toolRoot 'Tools\tracelog.exe'
 $dbg='Qisp256Release-D-'+[guid]::NewGuid().ToString('N');$mft='Qisp256Release-M-'+[guid]::NewGuid().ToString('N');$active=@();$filters=@();$probeError='';$text='';$closeError='';$retirement=$null
 try{
  if($native){
   # dpfilter.h: IHVSTREAMING=81 (0x51). Preserve and enable all four levels.
   # The native driver's EnableLog switch must also be enabled at driver start.
   foreach($level in 0,1,2,3){
    $filter=[PadCameraNative]::NtQueryDebugFilterState(0x51,$level);if($filter -notin 0,1){throw 'Cannot preserve native debug filter'}
    $filters+=@([pscustomobject]@{level=$level;original=$filter})
    if($filter -eq 0 -and [PadCameraNative]::NtSetDebugFilterState(0x51,$level,1) -ne 0){throw 'Cannot enable native close tracing'}
   }
   $null=Run-Child ($label+'-dbg-start') $trace @('-start',$dbg,'-f',('"'+(Join-Path $d 'dbgprint.etl')+'"'),'-b','64','-min','8','-max','128','-seq','256','-eflag','DBGPRINT')
   $active+=@($dbg);$script:sessions+=@($dbg)
   $null=Run-Child ($label+'-mft-start') "$env:windir\System32\logman.exe" @('start',$mft,'-p','{88c9b48e-2d10-41ce-8433-92b27d4a6a67}','0xffffffffffffffff','0xff','-o',('"'+(Join-Path $d 'mft.etl')+'"'),'-f','bin','-max','512','-bs','64','-nb','8','128','-ets')
   $active+=@($mft);$script:sessions+=@($mft);$r.fullMftDecoded=$false
  }
  $name=if($native){if($side -eq 'rear'){'QC Back Camera'}else{'QC Front Camera'}}else{if($side -eq 'rear'){'Xiaomi Pad 5 Rear Camera'}else{'Xiaomi Pad 5 Front Camera'}}
  $seconds='8'
  $r.captureClosed=$false;Save-State
  $selector=if($native){@('--device-id',('"'+$device.id+'"'))}else{@('--name',('"'+$name+'"'))}
  try{$text=Run-Child ($label+'-probe') (Join-Path $toolRoot 'Tools\CameraProbe256.exe') ($selector+@('--seconds',$seconds,'--save-dir',('"'+$d+'"'))) 45000}catch{$probeError=$_.Exception.Message}
  if($r.pendingPid){$r.unsafeToContinue=$true;throw 'Capture is still pending; retain current drivers'}
  $log=Join-Path $run ($label+'-probe.out.log');if(Test-Path -LiteralPath $log){$text=Get-Content -LiteralPath $log -Raw -Encoding UTF8}
  $stderr=Join-Path $run ($label+'-probe.err.log');if(Test-Path -LiteralPath $stderr){$text+="`n"+(Get-Content -LiteralPath $stderr -Raw -Encoding UTF8)}
  $original=@([regex]::Matches($text,'(?m)^ERROR256[^\r\n]*')|ForEach-Object Value)
  if($original.Count){$r.errors+=$original;$probeError=($original -join '; ')}
  $retirement=Get-ProbeRetirement $text
  if(-not $retirement.closed){$r.unsafeToContinue=$true;$closeError='Capture close not proven; retain current drivers'}
  else{Assert-Idle;$r.captureClosed=$true;Save-State}
 }finally{
  if(-not $r.pendingPid){
   foreach($s in @($active)){$null=Run-Child ($label+'-stop-'+$s) $trace @('-stop',$s);$script:sessions=@($script:sessions|Where-Object {$_ -ne $s})}
   foreach($saved in $filters){if($saved.original -eq 0){if([PadCameraNative]::NtSetDebugFilterState(0x51,$saved.level,0) -ne 0){$r.unsafeToContinue=$true;throw 'Native debug filter restoration failed'}}}
  }
 }
 if($native){
  $null=Run-Child ($label+'-dbg-decode') "$env:windir\System32\tracerpt.exe" @(('"'+(Join-Path $d 'dbgprint.etl')+'"'),'-o',('"'+(Join-Path $d 'dbgprint.xml')+'"'),'-of','XML','-y') 90000
  if(-not $retirement.neverOpened){
   try{$null=Run-Child ($label+'-native-close') "$env:windir\System32\WindowsPowerShell\v1.0\powershell.exe" @('-NoProfile','-ExecutionPolicy','Bypass','-File',('"'+(Join-Path $toolRoot 'Tools\Check-NativeClose.ps1')+'"'),'-XmlFile',('"'+(Join-Path $d 'dbgprint.xml')+'"'),'-OutputFile',('"'+(Join-Path $d 'native-close.json')+'"')) 60000}catch{$closeError=($closeError+'; '+$_.Exception.Message).Trim('; ')}
  }else{
   Write-AtomicJson (Join-Path $d 'native-close.json') ('{"status":"NOT_OPENED","cameraOpened":false,"reason":"Explicit probe lifecycle proves no capture object or initialization attempt"}')
  }
  # Decode the full MFT even if close qualification rejected, preserving the cause.
  if($r.pendingPid){$r.unsafeToContinue=$true;throw 'Native close decoder is still running'}
  $null=Run-Child ($label+'-mft-decode') (Join-Path $toolRoot 'Tools\ReadMft\ReadMft100.exe') @(('"'+(Join-Path $d 'mft.etl')+'"'),('"'+(Join-Path $d 'mft.jsonl')+'"'),('"'+(Join-Path $d 'mft-decode.json')+'"')) 90000
  $decode=Get-Content -LiteralPath (Join-Path $d 'mft-decode.json') -Raw|ConvertFrom-Json
  if($decode.eventsLost -ne 0 -or $decode.status -notlike 'PASS*'){$r.unsafeToContinue=$true;throw 'Full MFT trace decode failed'}
  $r.fullMftDecoded=$true;Save-State
 }
 if($text -match 'RESULT256 hr=00000000 frames=[1-9]\d* .*signal=false'){
  $r.errors+=@('Camera '+$side+' returned empty pixels (zero Y/UV may appear green). Driver binding alone is not a successful image test.')
 }
 if($closeError){
  $evidence=Join-Path $d 'native-close.json'
  if(Test-Path -LiteralPath $evidence){$closeReport=Get-Content -LiteralPath $evidence -Raw|ConvertFrom-Json;$closeError+='; '+($closeReport.reasons -join '; ')}
  $r.unsafeToContinue=$true;throw (($probeError+'; '+$closeError+'; full MFT decoded; see result.json for findings').Trim('; '))
 }
 if($ReleaseOnly){
  if($retirement.neverOpened){throw ('Native camera disappeared before pre-install release; '+$probeError)}
  Note ('RELEASE VERIFIED '+$side+'. Previous image findings remain in logs; new images must pass after installation.')
  return
 }
 if($probeError){throw $probeError}
 if($text -notmatch 'RESULT256 hr=00000000 frames=[1-9]\d* .*signal=true saved=true controlsChanged=false'){throw ('Camera '+$side+' image check failed. Empty/green output is not accepted as success.')}
 $r.captures+=@([ordered]@{side=$side;native=$native;directory=$d;closeVerified=$true;signal=$true;controlsChanged=$false})
 Note ('PASS camera '+$side+' '+$label)
}

function Can-RetryNativeEmptyStart([string]$Text,[object]$Close,[object]$Decode,[object]$State){
 if($State.pendingPid -or $State.unsafeToContinue -or -not $State.captureClosed -or -not $State.fullMftDecoded){return $false}
 $retirement=Get-ProbeRetirement $Text
 if(-not $retirement.closed -or $retirement.neverOpened){return $false}
 if($Text -match '(?m)^ERROR256' -or $Text -notmatch '(?m)^RESULT256 hr=00000000 frames=0 duplicatePolls=0 width=[1-9]\d* height=[1-9]\d* .*signal=false saved=false controlsChanged=false\r?$'){return $false}
 return $Close.status -eq 'PASS' -and $Close.eventsLost -eq 0 -and $Decode.status -like 'PASS*' -and $Decode.eventsLost -eq 0
}
function Capture-WithWarmStartRetry([string]$side,[string]$label,[bool]$native){
 try{Capture-One $side $label $native;return}catch{
  $firstError=$_.Exception.Message
  if(-not $native -or $Mode -ne 'Install'){throw}
  $directory=Join-Path $run $label
  $closeFile=Join-Path $directory 'native-close.json';$decodeFile=Join-Path $directory 'mft-decode.json'
  $probeFile=Join-Path $run ($label+'-probe.out.log')
  if(-not(Test-Path -LiteralPath $closeFile) -or -not(Test-Path -LiteralPath $decodeFile) -or -not(Test-Path -LiteralPath $probeFile)){throw}
  $text=Get-Content -LiteralPath $probeFile -Raw
  $close=Get-Content -LiteralPath $closeFile -Raw|ConvertFrom-Json
  $decode=Get-Content -LiteralPath $decodeFile -Raw|ConvertFrom-Json
  if(-not(Can-RetryNativeEmptyStart $text $close $decode $r)){throw}
  if(-not $r.Contains('verificationRetries')){$r.verificationRetries=@()}
  $r.verificationRetries+=@([ordered]@{side=$side;firstLabel=$label;firstError=$firstError;firstFrames=0;firstCloseVerified=$true;firstFullTraceLoss=0;retryLabel=($label+'-retry1')})
  Note ('Native '+$side+' first start returned no frames and closed safely; full trace complete. Retrying image check once; original evidence retained.')
 }
 # A second failure is terminal; no recursion, driver rebinding or forced reset.
 Capture-One $side ($label+'-retry1') $native
}

function Install-Certificates {
 foreach($c in @(Get-ChildItem -LiteralPath (Join-Path $cache 'Certificates') -Filter *.cer)){
  $cert=[Security.Cryptography.X509Certificates.X509Certificate2]::new($c.FullName)
  foreach($store in 'Root','TrustedPublisher'){if(-not(Test-Path -LiteralPath ("Cert:\LocalMachine\"+$store+'\'+$cert.Thumbprint))){Import-Certificate -FilePath $c.FullName -CertStoreLocation ("Cert:\LocalMachine\"+$store)|Out-Null}}
 }
 foreach($f in @(Get-ChildItem -LiteralPath (Join-Path $cache 'Payload') -Recurse -File|Where-Object {$_.Extension -in '.sys','.cat'})){
  if((Get-AuthenticodeSignature -LiteralPath $f.FullName).Status -ne 'Valid'){throw ('Invalid driver/catalog signature: '+$f.Name)}
 };$r.certificatesImported=$true;Save-State
}
function Bind([object]$spec){
 Assert-Idle;$id=if($spec.slot -eq 'virtualfront' -or $spec.slot -eq 'virtualrear'){
  $nodes=@(Virtual-Nodes|Where-Object {$_.hardware -contains $spec.hardware});if($nodes.Count){$nodes[0].id}else{''}
 }else{(Find-Physical $spec.slot).id}
 $inf=Join-Path $cache $spec.inf
 $same=$false
 if($id){$n=Node $id;if($n.service -eq $spec.service -and $n.version -eq $spec.version){$image=Service-Image $n.service;$same=(Test-Path -LiteralPath $image) -and (Get-FileHash -LiteralPath $image).Hash -eq $spec.sha256}}
 if(-not $same -or $profile.id -eq 'QISP253'){
  $r.driverActions=$true;Save-State
  $verb=if($id){'update'}else{'install'}
  $null=Run-Child ('bind-'+$spec.slot) (Join-Path $cache 'Tools\devcon.exe') @($verb,('"'+$inf+'"'),$spec.hardware) 90000
 }
 if(-not $id){$nodes=@(Virtual-Nodes|Where-Object {$_.hardware -contains $spec.hardware});if($nodes.Count -ne 1){throw 'Virtual camera creation did not produce one device'};$id=$nodes[0].id}
 $n=Node $id
 if($n.service -ne $spec.service -or $n.version -ne $spec.version -or (Get-FileHash -LiteralPath (Service-Image $n.service)).Hash -ne $spec.sha256){throw ('Driver binding verification failed: '+$spec.slot)}
 return $id
}
function Set-QispData {
 foreach($side in 'front','rear'){
  $n=Find-Physical $side;$key='Registry::HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Enum\'+$n.id+'\Device Parameters'
  New-Item -Path $key -Force|Out-Null
  $folder=if($side -eq 'front'){'FrontData'}else{'RearData'}
  New-ItemProperty -LiteralPath $key -Name CameraImagesPath -Value (Join-Path $cache ('Payload\Unified\'+$folder)) -PropertyType String -Force|Out-Null
 }
 $key='HKLM:\SYSTEM\CurrentControlSet\Control\Qualcomm\Camera';New-Item -Path $key -Force|Out-Null
 foreach($name in 'disableDualIfe','UBWCEnabledPreview','UBWCEnabledVideo','UBWCEnabledSnapshot','DumpFramesMask','EnableRawSnapshot','EnableContinuousRawDump'){
  $value=if($name -eq 'disableDualIfe'){1}else{0};New-ItemProperty -LiteralPath $key -Name $name -Value $value -PropertyType DWord -Force|Out-Null
 };$r.registryActions=$true;Save-State
}
function Set-NativeLogging {
 # Native success messages are gated by this switch in addition to DbgPrint filters.
 $key='Registry::HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Services\qccamavs\Logging'
 New-Item -Path $key -Force|Out-Null
 New-ItemProperty -LiteralPath $key -Name EnableLog -Value 1 -PropertyType DWord -Force|Out-Null
 $r.registryActions=$true;Save-State
}
function Start-Gpu {
 $exe=Join-Path $cache 'Payload\Runtime\GpuIspHost.exe';$sc="$env:windir\System32\sc.exe"
 $verb=if(Get-Service CodexNabuGpuIsp -ErrorAction SilentlyContinue){'config'}else{'create'}
 $null=Run-Child 'gpu-service-config' $sc @($verb,'CodexNabuGpuIsp','binPath=',('\"'+$exe+'\"'),'start=','auto','DisplayName=','"Xiaomi Pad 5 GPU ISP"')
 Start-Service CodexNabuGpuIsp;(Get-Service CodexNabuGpuIsp).WaitForStatus('Running',[TimeSpan]::FromSeconds(15))
 $until=[datetime]::UtcNow.AddSeconds(30)
 do{
  $workers=@(Get-CimInstance Win32_Process|Where-Object {$_.Name -eq 'GpuCameraPump.exe' -and $_.ExecutablePath -eq (Join-Path $cache 'Payload\Runtime\GpuCameraPump.exe')})
  $ready=(Test-Path -LiteralPath (Join-Path $cache 'Payload\Runtime\runtime\front\READY.txt')) -and (Test-Path -LiteralPath (Join-Path $cache 'Payload\Runtime\runtime\rear\READY.txt'))
  if($workers.Count -eq 2 -and $ready){break};Start-Sleep -Milliseconds 250
 }while([datetime]::UtcNow -lt $until)
 if($workers.Count -ne 2 -or -not $ready){throw 'GPU service did not produce both READY workers'}
}
function Verify-Active {
 $nodes=@();foreach($spec in $profile.drivers){
  $n=@(if($spec.slot -like 'virtual*'){Virtual-Nodes|Where-Object {$_.hardware -contains $spec.hardware -and $_.problem -eq 0}}else{Find-Physical $spec.slot})
  if($n.Count -ne 1 -or $n[0].problem -ne 0 -or $n[0].service -ne $spec.service -or $n[0].version -ne $spec.version){throw ('Active binding differs: '+$spec.slot)}
  $image=Service-Image $spec.service
  if((Get-FileHash -LiteralPath $image).Hash -ne $spec.sha256){throw ('Active SYS differs: '+$spec.slot)}
  $nodes+=@($n[0])
 }
 $r.nodes=$nodes;$r.cache=$cache
 if($profile.id -eq 'QISP253'){
  foreach($n in @(Virtual-Nodes)){if($n.problem -ne 22){throw 'Alternative virtual camera remains enabled'}}
  foreach($side in 'front','rear'){
   $n=Find-Physical $side;$actual=(Get-Item -LiteralPath ('Registry::HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Enum\'+$n.id+'\Device Parameters')).GetValue('CameraImagesPath','')
   $folder=if($side -eq 'front'){'FrontData'}else{'RearData'}
   if($actual -ne (Join-Path $cache ('Payload\Unified\'+$folder))){throw 'Native camera data path differs'}
  }
  $mft=Service-Image 'QCCamAvs';$mft=Join-Path (Split-Path -Parent $mft) 'QcDeviceMFT8150.dll'
  if((Get-FileHash -LiteralPath $mft).Hash -ne $profile.mftSha256){throw 'DeviceMFT075b differs'}
  $registration=(Get-Item -LiteralPath 'Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Classes\CLSID\{4C2331F0-66BE-4177-9841-2FCBA8CCF5CA}\InprocServer32').GetValue('','')
  $registration=[Environment]::ExpandEnvironmentVariables([string]$registration).Trim('"')
  if($registration -ine $mft -or (Get-FileHash -LiteralPath $registration).Hash -ne $profile.mftSha256){throw 'Registered DeviceMFT is not the active packaged MFT'}
  $firmware=Join-Path (Split-Path -Parent (Service-Image 'qcISP')) 'CAMERA_ICP_AAAAAA.elf'
  if((Get-FileHash -LiteralPath $firmware).Hash -ne (Get-FileHash -LiteralPath (Join-Path $cache 'Payload\Unified\ISP\CAMERA_ICP_AAAAAA.elf')).Hash){throw 'Installed ICP firmware differs from the package'}
 }else{if((Find-Physical 'avs').problem -ne 22 -or (Find-Physical 'front').problem -ne 22){throw 'Native alternative remains enabled'}}
 $svc=Get-Service CodexNabuGpuIsp -ErrorAction SilentlyContinue
 if($profile.id -eq 'GPUISP'){
  if(-not $svc -or $svc.Status -ne 'Running' -or (Service-Image 'CodexNabuGpuIsp') -ne (Join-Path $cache 'Payload\Runtime\GpuIspHost.exe')){throw 'GPU service configuration differs'}
 }elseif($svc -and $svc.Status -ne 'Stopped'){throw 'GPU service should be stopped'}
 $r.activeVerificationPassed=$true;Save-State
}
. (Join-Path $root 'Tools\CameraDma.ps1')
. (Join-Path $root 'Tools\CameraSelection.ps1')
. (Join-Path $root 'Tools\NativeLogging.ps1')
try{
 $manifest=Verify-Package
 if(@(Get-CimInstance Win32_Processor|Where-Object Architecture -eq 12).Count -eq 0){throw 'This package requires Windows ARM64 on Xiaomi Pad 5 NABU'}
 foreach($slot in 'isp','mipi','front','rear','avs'){$null=Find-Physical $slot}
 $dmaPlan=Get-CameraDmaPlanFromSnapshot (Get-NativeCameraDmaSnapshot)
 if($Mode -eq 'Check'){
  $logPlan=Get-NativeLoggingPlan (Get-NativeLoggingSnapshot)
  Write-Host ('DMA253R2 '+$dmaPlan.status)
  Write-Host ('NATIVE_LOGGING '+$(if($logPlan.ready){'READY'}else{'PREPARATION_REQUIRED'}))
  Write-Host ('PASS256 archive integrity/platform '+$profile.id);exit 0
 }
 New-Item -ItemType Directory -Path $base -Force|Out-Null
 foreach($lockPath in @((Join-Path $base 'operation.lock'),'C:\ProgramData\CodexQispBuild062\operation.lock')){
  if($lockPath -like '*CodexQispBuild062*' -and -not(Test-Path -LiteralPath $lockPath)){continue}
  $locks+=@([IO.File]::Open($lockPath,[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None))
 }
 $state=Join-Path $base 'state.json';$previous=$null
 if(Test-Path -LiteralPath $state){$previous=Get-Content -LiteralPath $state -Raw|ConvertFrom-Json}
 $runParent=if($ResultRoot){[IO.Path]::GetFullPath($ResultRoot)}else{Join-Path $base 'Logs'}
 $run=Join-Path $runParent ($profile.id+'-'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[guid]::NewGuid().ToString('N').Substring(0,6))
 New-Item -ItemType Directory -Path $run -Force|Out-Null;$r.resultDirectory=$run;$r.boot=([datetimeoffset](Get-CimInstance Win32_OperatingSystem).LastBootUpTime).ToString('o');$r.bootIdentity=Get-CameraBootIdentity
 if($previous){Write-AtomicJson (Join-Path $run 'previous-state.json') ($previous|ConvertTo-Json -Depth 16)}
 Assert-Idle
 if($previous -and $previous.pendingPid){
  $owner=Get-Process -Id $previous.pendingPid -ErrorAction SilentlyContinue
  if($owner -and $owner.StartTime.ToString('o') -eq $previous.pendingCreation){$r.pendingPid=$previous.pendingPid;$r.pendingCreation=$previous.pendingCreation;$r.pendingKind=$previous.pendingKind;$r.unsafeToContinue=$true;throw 'The previous installer child is still active. No concurrent changes permitted.'}
 }
 $dma=Prepare-CameraDma $dmaPlan ($Mode -eq 'Install');$r.cameraDma=$dma
 $r.registryActions=$r.registryActions -or $dma.registryChanged
 if($dma.status -eq 'PENDING_RESTART'){$r.rebootRequired=$true};Save-State
 $logging=Prepare-NativeLogging ($Mode -eq 'Install')
 if($dma.status -eq 'PENDING_RESTART' -or $logging -eq 'PENDING_RESTART'){
  $r.status='PENDING253R2_CAMERA_DMA_RESTART';$r.rebootRequired=$true;$r.registryActions=$r.registryActions -or $dma.registryChanged;$exitCode=3010
  Note 'RESTART REQUIRED: camera DMA or native logging startup settings prepared. Restart Windows normally, then run Install-QualcommISP.cmd again. No automatic restart or live SMMU reset is performed.'
  exit $exitCode
 }
 $recovery=Get-CameraRecoveryAction $previous $r.bootIdentity $r.boot
 if($recovery -eq 'RESTART_REQUIRED'){
  $r.unsafeToContinue=$true;$r.rebootRequired=$true;$r.status='PENDING253_PREVIOUS_SESSION_RESTART';$exitCode=3010
  Note 'RESTART REQUIRED: a previous camera test did not prove safe closure in this Windows kernel session. Restart Windows normally, then run Install-QualcommISP.cmd again. The previous failure is preserved. This run has not rebound the camera drivers.'
  exit $exitCode
 }
 if($recovery -eq 'NEW_BOOT'){$r.previousSessionRetiredByNewBoot=$true}
 Note ('Verified package '+$profile.id);Assert-Idle;Memory-Guard
 if($Mode -eq 'Install'){
  Stop-Gpu
  # Fresh same-opening release proof is mandatory before rebinding native cameras.
  if((Find-Physical 'avs').problem -eq 0 -and (Find-Physical 'avs').service -eq 'QCCamAvs'){
   $inventory=Get-CameraInventory 'pre-release-native'
   foreach($side in 'rear','front'){
    $device=Select-NativeCamera $inventory (Find-Physical 'avs').id $side -AllowAbsent
    if($device){Capture-One $side ('pre-release-native-'+$side) $true $true}
    else{Note ('Native '+$side+' interface is not exposed; enumeration completed without opening a camera. Proceeding to install; both images must pass after binding.')}
   }
  }
  Assert-Idle;Memory-Guard
  $cache=Join-Path $base ('Packages\'+$profile.id+'-'+$manifest.payloadId)
  if(-not(Test-Path -LiteralPath $cache)){New-Item -ItemType Directory -Path $cache -Force|Out-Null}
  foreach($f in $manifest.files){
   $dest=Join-Path $cache $f.path
   if(Test-Path -LiteralPath $dest){if((Get-FileHash -LiteralPath $dest).Hash -eq $f.sha256){continue}}
   New-Item -ItemType Directory -Path (Split-Path -Parent $dest) -Force|Out-Null;Copy-Item -LiteralPath (Join-Path $root $f.path) -Destination $dest -Force
   if((Get-FileHash -LiteralPath $dest).Hash -ne $f.sha256){throw 'Stable cache copy verification failed'}
  }
  Copy-Item -LiteralPath (Join-Path $root 'MANIFEST-SHA256.json') -Destination $cache -Force
  Install-Certificates
  Set-NativeLogging
  foreach($v in @(Virtual-Nodes)){Enable-Node $v.id $false ('disable-virtual-'+($v.id -replace '[^A-Za-z0-9]','_'))}
  foreach($slot in 'avs','front','rear'){Enable-Node (Find-Physical $slot).id $false ('disable-'+$slot)}
  $bound=@{}
  $orderedDrivers=if($profile.id -eq 'QISP253'){foreach($slot in 'isp','mipi','avs','front','rear'){$profile.drivers|Where-Object slot -eq $slot}}else{$profile.drivers}
  foreach($spec in $orderedDrivers){$bound[$spec.slot]=Bind $spec}
  if($profile.id -eq 'QISP253'){
   foreach($slot in 'front','rear'){Enable-Node (Find-Physical $slot).id $false ('sensor-before-data-'+$slot)}
   Set-QispData
   foreach($slot in 'isp','mipi','avs','front','rear'){Enable-Node (Find-Physical $slot).id $true ('enable-'+$slot)}
  }else{
   foreach($slot in 'mipi','rear','virtualfront','virtualrear'){Enable-Node $bound[$slot] $true ('enable-'+$slot)}
   if($profile.id -eq 'GPUISP'){Start-Gpu}
  }
 }else{
  $cache=Join-Path $base ('Packages\'+$profile.id+'-'+$manifest.payloadId);if(-not(Test-Path -LiteralPath $cache)){throw 'Run Setup first'}
 }
 Verify-Active
 foreach($side in 'rear','front'){Capture-WithWarmStartRetry $side ('verify-'+$side) ($profile.id -eq 'QISP253')}
 Assert-Idle;Verify-Active
 $r.status='PASS_PUBLIC_PREVIEW_'+$profile.id+'_INSTALLED_BOTH_CAMERAS_VERIFIED';$r.completed=(Get-Date -Format o);$exitCode=0;Note ('SUCCESS '+$profile.title+'. Both cameras produced frames and closed normally. No restart invoked.')
}catch{
 $r.errors+=@($_.Exception.Message);$r.status='FAILED253R2_RETAIN_CURRENT_STATE'
 if($Mode -eq 'Install' -and $r.driverActions -and $r.Contains('activeVerificationPassed') -and $r.activeVerificationPassed){
  $r.status='INSTALLED253_VERIFICATION_INCOMPLETE';$exitCode=2
  Write-Host ('DRIVERS INSTALLED; CAMERA VERIFICATION INCOMPLETE: '+$_.Exception.Message) -ForegroundColor Yellow
 }else{Write-Host ('ERROR: '+$_.Exception.Message) -ForegroundColor Red}
 if($run){Append-SetupLog $_.ScriptStackTrace}
}finally{
 Save-State;foreach($lock in $locks){$lock.Dispose()}
}
if($run){Write-Host ('Log: '+$run)}
exit $exitCode
