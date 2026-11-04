# Shared state checks and bounded PnP commands. No commands run on import.
Set-StrictMode -Version Latest
function QispReadProblemCM([string]$Id){
 if(-not ('Qisp053NodeStatus' -as [type])){
 Add-Type @'
using System; using System.Runtime.InteropServices;
public static class Qisp053NodeStatus {
 [DllImport("cfgmgr32.dll",CharSet=CharSet.Unicode)] public static extern uint CM_Locate_DevNodeW(out uint devinst,string id,uint flags);
 [DllImport("cfgmgr32.dll")] public static extern uint CM_Get_DevNode_Status(out uint status,out uint problem,uint devinst,uint flags);
}
'@
 }
 $devinst=[uint32]0;$status=[uint32]0;$problem=[uint32]0
 if([Qisp053NodeStatus]::CM_Locate_DevNodeW([ref]$devinst,$Id,0) -ne 0 -or [Qisp053NodeStatus]::CM_Get_DevNode_Status([ref]$status,[ref]$problem,$devinst,0) -ne 0){throw ('Actual node status unavailable: '+$Id)}
 return [int]$problem
}
function QispNode([string]$Id){
 $p=@(Get-PnpDeviceProperty -InstanceId $Id -KeyName DEVPKEY_Device_DriverVersion,DEVPKEY_Device_DriverInfPath,DEVPKEY_Device_ProblemCode,DEVPKEY_Device_Service)
 function V($n){$entry=@($p|Where-Object KeyName -eq $n);if($entry.Count -ne 1 -or -not $entry[0].PSObject.Properties['Data'] -or -not $entry[0].Data){throw ('Required node property missing: '+$n)};return $entry[0].Data}
 [pscustomobject]@{id=$Id;version=(V 'DEVPKEY_Device_DriverVersion');inf=(V 'DEVPKEY_Device_DriverInfPath');problem=(QispReadProblemCM $Id);service=(V 'DEVPKEY_Device_Service')}
}
function QispServiceImage([string]$Service){
 $path=(Get-ItemProperty -LiteralPath ('Registry::HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Services\'+$Service)).ImagePath
 $path=$path.Replace('\SystemRoot',$env:windir)
 if($path.StartsWith('\??\')){$path=$path.Substring(4)}
 $path.Trim('"')
}
function QispCheckSavedPackage($Package,[string]$BackupRoot,[switch]$Current){
 foreach($file in $Package.files){
  $path=Join-Path (Join-Path $BackupRoot $Package.label) $file.name
  if((Get-Item -LiteralPath $path).Length -ne $file.bytes -or (Get-FileHash -LiteralPath $path).Hash -ne $file.sha256){throw ('Backup asset changed: '+$path)}
 }
 if($Current){
  $node=QispNode $Package.id
  if($node.version -ne $Package.version -or $node.problem -ne $Package.problem -or $node.service -ne $Package.service -or $node.inf -ne $Package.publishedInf){throw ('Current camera baseline changed: '+$Package.id)}
  $folder=Split-Path (QispServiceImage $Package.service) -Parent
  foreach($file in $Package.files){
   if((Get-FileHash -LiteralPath (Join-Path $folder $file.name)).Hash -ne $file.sha256){throw ('Bound package bytes differ: '+$Package.id+' '+$file.name)}
  }
 }
}
function QispSaveRegistry([string]$Path,[string]$Name){
 $key=Get-Item -LiteralPath $Path
 $exists=$key.GetValueNames() -contains $Name
 [pscustomobject]@{path=$Path;name=$Name;exists=$exists;kind=$(if($exists){$key.GetValueKind($Name).ToString()}else{'DWord'});value=$(if($exists){$key.GetValue($Name,$null,[Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)}else{$null})}
}
function QispRestoreRegistry($Item){
 if($Item.exists){New-ItemProperty -LiteralPath $Item.path -Name $Item.name -Value $Item.value -PropertyType $Item.kind -Force|Out-Null}
 elseif((Get-Item -LiteralPath $Item.path).GetValueNames() -contains $Item.name){Remove-ItemProperty -LiteralPath $Item.path -Name $Item.name}
 $after=QispSaveRegistry $Item.path $Item.name
 if($after.exists -ne $Item.exists -or ($Item.exists -and ($after.kind -ne $Item.kind -or $after.value -cne $Item.value))){throw ('Registry restoration differs: '+$Item.path+' '+$Item.name)}
}
function QispNote([string]$Text){
 $line=(Get-Date -Format o)+' '+$Text
 Add-Content -LiteralPath (Join-Path $script:qispRun 'run.log') -Value $line -Encoding UTF8
 if($script:qispRecovery){Add-Content -LiteralPath (Join-Path $script:qispRecovery 'run.log') -Value $line -Encoding UTF8}
 Write-Host $line
}
function QispCommand([string]$Label,[string]$Exe,[string[]]$Arguments){
 if($script:qispBusy -or $script:qispRebootRequired){throw 'Previous PnP operation prevents overlapping recovery'}
 $out=Join-Path $script:qispRecovery ($Label+'.out.log');$err=Join-Path $script:qispRecovery ($Label+'.err.log')
 QispNote ('DEVICE_BEGIN '+$Label)
 $process=Start-Process -FilePath $Exe -ArgumentList $Arguments -WindowStyle Hidden -RedirectStandardOutput $out -RedirectStandardError $err -PassThru
 $handle=$process.Handle
 if(-not $process.WaitForExit(45000)){$script:qispBusy=$true;$script:qispPendingPid=$process.Id;throw ('Device command remains pending: '+$Label)}
 $process.WaitForExit();$process.Refresh()
 if($null -eq $process.ExitCode){$script:qispBusy=$true;throw 'Missing device command exit code'}
 $content=[IO.File]::ReadAllText($out)+[IO.File]::ReadAllText($err)
 if($process.ExitCode -eq 3011 -or ((Split-Path $Exe -Leaf) -ieq 'devcon.exe' -and $process.ExitCode -eq 1) -or $content -match '(?i)reboot required|reboot is required|must be restarted|restart (the|your) computer'){
  $script:qispRebootRequired=$true;throw ('Device command requested reboot: '+$Label)
 }
 if($process.ExitCode -ne 0){throw ('Device command failed: '+$Label+' exit='+$process.ExitCode+' '+$content.Trim())}
 QispNote ('DEVICE_END '+$Label+' '+$content.Trim())
}
function QispEnable([string]$Id,[bool]$Enabled,[string]$Label){
 $problem=(QispNode $Id).problem
 if($Enabled){if($problem -eq 22){QispCommand $Label (Join-Path $env:windir 'System32\pnputil.exe') @('/enable-device',('"'+$Id+'"'))};if((QispNode $Id).problem -ne 0){throw ('Device failed to start: '+$Id)}}
 else{if($problem -ne 22){QispCommand $Label (Join-Path $env:windir 'System32\pnputil.exe') @('/disable-device',('"'+$Id+'"'))};if((QispNode $Id).problem -ne 22){throw ('Device failed to stop: '+$Id)}}
}
function QispBind([string]$Label,[string]$Id,[string]$Inf,[string]$Hardware,[string]$Version,[string]$SysHash,[string]$Devcon){
 QispCommand $Label $Devcon @('update',('"'+$Inf+'"'),$Hardware)
 $node=QispNode $Id
 if($node.version -ne $Version){throw ('Bound INF version differs: '+$Id)}
 if((Get-FileHash -LiteralPath (QispServiceImage $node.service)).Hash -ne $SysHash){throw ('Bound SYS bytes differ: '+$Id)}
}
