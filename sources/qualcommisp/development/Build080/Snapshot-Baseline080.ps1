param([string]$OutputDirectory='')
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$root=Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'CameraState.ps1')
if(-not $OutputDirectory){$OutputDirectory=Join-Path $root ('rollback\baseline-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))}
if(Test-Path -LiteralPath $OutputDirectory){throw 'Preserve existing baseline'}
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
$specs=@(
 @('avs','DISPLAY\QCOM_AVSTREAM_8150\3&13F80E4D&0&UID32768','VIDEO\QCOM_AVStream_8150','QCCamAvs'),
 @('isp','ACPI\QCOM0528\16','ACPI\QCOM0528','qcISP'),
 @('front-hw','ACPI\QCOM05A4\18','ACPI\QCOM05A4','qcCameraMipiCsi'),
 @('rear-hw','ACPI\QCOM0529\15','ACPI\QCOM0529','CameraRearSensor'),
 @('front-sensor','ACPI\QCOM0506\1A','ACPI\QCOM0506','CameraFrontSensor'),
 @('front-virtual','ROOT\CAMERA\0006','ROOT\NABUCAMVIRTUALFRONT','NabuCamAvStreamFrontVirtual'),
 @('rear-virtual','ROOT\CAMERA\0007','ROOT\NABUCAMVIRTUALREAR','NabuCamAvStreamRearVirtual')
)
$drivers=@(Get-CimInstance Win32_SystemDriver)
$packages=@()
foreach($spec in $specs){
 $device=Get-PnpDevice -InstanceId $spec[1] -ErrorAction Stop
 $properties=@(Get-PnpDeviceProperty -InstanceId $spec[1] -KeyName DEVPKEY_Device_DriverVersion,DEVPKEY_Device_DriverInfPath,DEVPKEY_Device_ProblemCode,DEVPKEY_Device_Service,DEVPKEY_Device_HardwareIds)
 function Value([string]$name){return @($properties|Where-Object KeyName -eq $name)[0].Data}
 $service=Value 'DEVPKEY_Device_Service'
 $hardwareIds=@(Value 'DEVPKEY_Device_HardwareIds')
 if($hardwareIds -notcontains $spec[2]){throw "Unexpected hardware IDs: $($spec[1]) = $hardwareIds"}
 if($service -ne $spec[3]){throw "Unexpected service: $($spec[1]) = $service"}
 $driver=@($drivers|Where-Object Name -eq $service)[0]
 $path=$driver.PathName.Replace('\SystemRoot',$env:windir)
 if($path.StartsWith('\??\')){$path=$path.Substring(4)}
 $source=Split-Path $path -Parent
 if(-not $source.StartsWith(($env:windir+'\System32\DriverStore\FileRepository\'),[StringComparison]::OrdinalIgnoreCase)){throw 'Baseline is not a DriverStore package'}
 $infs=@(Get-ChildItem -LiteralPath $source -Filter '*.inf' -File)
 if($infs.Count -ne 1){throw "Ambiguous baseline INF: $source"}
 $destination=Join-Path $OutputDirectory $spec[0]
 New-Item -ItemType Directory -Path $destination|Out-Null
 $files=@()
 foreach($file in Get-ChildItem -LiteralPath $source -File){
  if($file.Extension -notin '.sys','.inf','.cat','.elf','.dll','.bin'){continue}
  $target=Join-Path $destination $file.Name
  Copy-Item -LiteralPath $file.FullName -Destination $target
  $hash=(Get-FileHash -LiteralPath $file.FullName).Hash
  if((Get-FileHash -LiteralPath $target).Hash -ne $hash){throw 'Baseline copy hash mismatch'}
  $files+=@([ordered]@{name=$file.Name;bytes=$file.Length;sha256=$hash;source=$file.FullName})
 }
 $packages+=@([ordered]@{label=$spec[0];id=$spec[1];hardware=$spec[2];hardwareIds=$hardwareIds;service=$service;version=(Value 'DEVPKEY_Device_DriverVersion');publishedInf=(Value 'DEVPKEY_Device_DriverInfPath');problem=(QispReadProblemCM $spec[1]);problemSource='CM_Get_DevNode_Status actual read-only';inf=$infs[0].Name;sys=(Split-Path $path -Leaf);files=$files})
}
$registry=@()
foreach($id in 'ACPI\QCOM0506\1A','ACPI\QCOM0529\15'){
 $path='Registry::HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Enum\'+$id+'\Device Parameters'
 $key=Get-Item -LiteralPath $path
 $exists=$key.GetValueNames() -contains 'CameraImagesPath'
 $registry+=@([ordered]@{path=$path;name='CameraImagesPath';exists=$exists;kind=$(if($exists){$key.GetValueKind('CameraImagesPath').ToString()}else{'String'});value=$(if($exists){$key.GetValue('CameraImagesPath',$null,[Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)}else{$null})})
}
$os=Get-CimInstance Win32_OperatingSystem
$identity=[Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
$report=[ordered]@{schema=1;build='QISP pre080 exact installed075 snapshot';created=(Get-Date).ToString('o');administrator=$identity.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator);lastBoot=$os.LastBootUpTime.ToString('o');freeRAMMiB=[math]::Round($os.FreePhysicalMemory/1024);frameServer=(Get-Service FrameServer).Status.ToString();gpuService=(Get-Service CodexNabuGpuIsp).Status.ToString();packages=$packages;registry=$registry;newDriverInstalled=$false}
$report|ConvertTo-Json -Depth 7|Set-Content -LiteralPath (Join-Path $OutputDirectory 'baseline.json') -Encoding UTF8
$OutputDirectory|Set-Content -LiteralPath (Join-Path $root 'rollback\latest-baseline.txt') -Encoding UTF8
'QISP_BASELINE_SAVED '+$OutputDirectory
