[CmdletBinding()]
param([switch]$ListOnly,[switch]$Elevated,[string]$BackupRoot='')
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
if($PSVersionTable.PSEdition -eq 'Desktop'){$env:PSModulePath=(Join-Path $PSHOME 'Modules')+';'+[Environment]::GetEnvironmentVariable('PSModulePath',[EnvironmentVariableTarget]::Machine)}
$admin=([Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if(-not $admin -and -not $ListOnly){
 $args=@('-NoProfile','-ExecutionPolicy','Bypass','-File',('"'+$PSCommandPath+'"'),'-Elevated')
 if($BackupRoot){$args+=@('-BackupRoot',('"'+$BackupRoot+'"'))}
 $p=Start-Process -FilePath "$env:windir\System32\WindowsPowerShell\v1.0\powershell.exe" -ArgumentList $args -Verb RunAs -WindowStyle Hidden -PassThru -Wait
 Write-Host ('Backup process exit: '+$p.ExitCode+'. See C:\ProgramData\XiaomiPad5Camera\Backups for the saved package.')
 exit $p.ExitCode
}
$pattern='^(ACPI\\QCOM(0528|05A4|0506|0529|052A|0536|0535)\\|DISPLAY\\QCOM_AVSTREAM_8150\\)'
$nodes=@(Get-CimInstance Win32_PnPSignedDriver|Where-Object {$_.DeviceID -match $pattern}|Select-Object DeviceID,DeviceName,InfName,DriverVersion)
foreach($prefix in 'ACPI\QCOM0528\','ACPI\QCOM05A4\','ACPI\QCOM0506\','ACPI\QCOM0529\','DISPLAY\QCOM_AVSTREAM_8150\'){
 if(@($nodes|Where-Object {$_.DeviceID.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)}).Count -ne 1){throw ('Required NABU camera device missing or ambiguous: '+$prefix)}
}
if($ListOnly){[pscustomobject]@{status='PASS_BACKUP_PLAN_NO_CHANGES';nodes=$nodes;driverChanges=$false;registryChanges=$false;filesWritten=$false}|ConvertTo-Json -Depth 5;exit 0}
foreach($n in $nodes){if($n.InfName -notmatch '^oem[0-9]+\.inf$'){throw ('Unrecognized driver package name: '+$n.InfName)}}
if((Get-PSDrive C).Free -lt 1GB){throw 'At least 1 GiB free space on C: is required before backup.'}
if(-not $BackupRoot){$BackupRoot=Join-Path 'C:\ProgramData\XiaomiPad5Camera\Backups' ('Before-QISP253-'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[guid]::NewGuid().ToString('N').Substring(0,8))}
$BackupRoot=[IO.Path]::GetFullPath($BackupRoot)
if(Test-Path -LiteralPath $BackupRoot){throw 'Use a new empty backup path; existing backups will not be overwritten.'}
New-Item -ItemType Directory -Path $BackupRoot|Out-Null
$r=[ordered]@{status='FAILED_BACKUP';created=(Get-Date -Format o);nodes=$nodes;exports=@();registry=@();data=@();driverChanges=$false;registryChanges=$false;errors=@()}
try{
 foreach($inf in @($nodes.InfName|Sort-Object -Unique)){
  $dest=Join-Path $BackupRoot ('Drivers\'+$inf);New-Item -ItemType Directory -Path $dest|Out-Null
  $p=Start-Process -FilePath "$env:windir\System32\pnputil.exe" -ArgumentList @('/export-driver',$inf,('"'+$dest+'"')) -WindowStyle Hidden -PassThru -Wait -RedirectStandardOutput (Join-Path $BackupRoot ($inf+'.log')) -RedirectStandardError (Join-Path $BackupRoot ($inf+'.err.log'))
  if($p.ExitCode -ne 0 -or -not @(Get-ChildItem -LiteralPath $dest -Recurse -Filter '*.inf' -File).Count){throw ('Driver export failed: '+$inf)}
  $r.exports+=@([ordered]@{inf=$inf;directory=$dest;exit=$p.ExitCode})
 }
 $keys=@('HKLM\SYSTEM\CurrentControlSet\Control\Qualcomm\Camera','HKLM\SOFTWARE\Classes\CLSID\{4C2331F0-66BE-4177-9841-2FCBA8CCF5CA}\InprocServer32')
 foreach($node in $nodes){$keys+=@('HKLM\SYSTEM\CurrentControlSet\Enum\'+$node.DeviceID+'\Device Parameters')}
 for($i=0;$i -lt $keys.Count;$i++){
  $key=$keys[$i];$provider=$key.Replace('HKLM\','Registry::HKEY_LOCAL_MACHINE\')
  if(-not(Test-Path -LiteralPath $provider)){continue}
  $dest=Join-Path $BackupRoot ('registry-'+$i+'.reg')
  $p=Start-Process -FilePath "$env:windir\System32\reg.exe" -ArgumentList @('export',('"'+$key+'"'),('"'+$dest+'"'),'/y') -WindowStyle Hidden -PassThru -Wait -RedirectStandardOutput (Join-Path $BackupRoot ('registry-'+$i+'.log')) -RedirectStandardError (Join-Path $BackupRoot ('registry-'+$i+'.err.log'))
  if($p.ExitCode -ne 0){throw ('Registry export failed: '+$key)}
  $r.registry+=@([ordered]@{key=$key;file=$dest})
 }
 foreach($node in $nodes){
  $provider='Registry::HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Enum\'+$node.DeviceID+'\Device Parameters'
  $key=Get-Item -LiteralPath $provider -ErrorAction SilentlyContinue
  if(-not $key){continue};$dataPath=[string]$key.GetValue('CameraImagesPath','')
  if(-not $dataPath -or -not(Test-Path -LiteralPath $dataPath -PathType Container)){continue}
  $label=$node.DeviceID -replace '[^A-Za-z0-9_-]','_';$dest=Join-Path $BackupRoot ('CameraData\'+$label);New-Item -ItemType Directory -Path $dest|Out-Null
  foreach($file in @(Get-ChildItem -LiteralPath $dataPath -File|Where-Object {$_.Extension -in '.bin','.elf'})){
   Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $dest $file.Name)
   if((Get-FileHash -LiteralPath (Join-Path $dest $file.Name)).Hash -ne (Get-FileHash -LiteralPath $file.FullName).Hash){throw 'Backup data hash mismatch'}
  }
  $r.data+=@([ordered]@{device=$node.DeviceID;originalPath=$dataPath;backupPath=$dest})
 }
 $r.files=@(Get-ChildItem -LiteralPath $BackupRoot -Recurse -File|ForEach-Object {[ordered]@{path=$_.FullName.Substring($BackupRoot.TrimEnd('\').Length+1);bytes=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName).Hash.ToLowerInvariant()}})
 $r.status='PASS_BACKUP_SAVED_NO_DRIVER_OR_REGISTRY_CHANGES'
}catch{$r.errors+=@($_.Exception.Message)}
finally{$r.completed=(Get-Date -Format o);$r|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $BackupRoot 'BACKUP-INFO.json') -Encoding UTF8}
Write-Host ('Backup: '+$BackupRoot)
if($r.status -notlike 'PASS*'){throw ($r.errors-join '; ')}
