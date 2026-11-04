$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$taskRoot=Split-Path -Parent $PSScriptRoot;$root=$taskRoot
. (Join-Path $root 'CameraDma.ps1')
. (Join-Path $root 'CameraSelection.ps1')
$passed=[Collections.Generic.List[string]]::new()
function Assert([bool]$Condition,[string]$Name){if(-not $Condition){throw ('FAIL '+$Name)};$passed.Add($Name)}
$boot=[pscustomobject]@{recordId=100;timeUtc='2026-11-04T01:00:00Z'}
$newBoot=[pscustomobject]@{recordId=101;timeUtc='2026-11-04T02:00:00Z'}
$previous=[pscustomobject]@{pendingPid=0;unsafeToContinue=$true;captureClosed=$false;fullMftDecoded=$false;rebootRequired=$false;bootIdentity=$boot}
Assert ((Get-CameraRecoveryAction $previous $boot '') -eq 'RESTART_REQUIRED') 'Recorded previous unsafe failure requires restart in same kernel session'
Assert ((Get-CameraRecoveryAction $previous $newBoot '') -eq 'NEW_BOOT') 'Genuine new boot permits fresh qualification without declaring old failure successful'
Assert ((Get-CameraRecoveryAction $null $boot '') -eq 'NONE') 'No previous record permits first installation'
$safe=[pscustomobject]@{pendingPid=0;unsafeToContinue=$false;captureClosed=$true;fullMftDecoded=$true;rebootRequired=$false}
Assert ((Get-CameraRecoveryAction $safe $boot '') -eq 'NONE') 'Safe completed record allows same-session installation'
$legacy=[pscustomobject]@{pendingPid=0;unsafeToContinue=$true;captureClosed=$false;fullMftDecoded=$false;rebootRequired=$false;boot='2026-11-04T06:00:00+05:00'}
Assert ((Get-CameraRecoveryAction $legacy $boot '2026-11-04T06:00:00+05:00') -eq 'RESTART_REQUIRED') 'Legacy boot record preserves same-session hold'
Assert ((Get-CameraRecoveryAction $legacy $newBoot '2026-11-04T07:00:00+05:00') -eq 'NEW_BOOT') 'Legacy real changed boot permits fresh qualification'

# Execute the actual Setup restart branch in isolated child processes. Only mock
# boot/state/logs are used; no HKLM, live journal, drivers or cameras are touched.
$tokens=$null;$errors=$null;$ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $root 'Setup.ps1'),[ref]$tokens,[ref]$errors)
Assert ($errors.Count -eq 0) 'Public Setup parses in PowerShell 5.1'
$branch=$ast.FindAll({param($n)$n -is [Management.Automation.Language.IfStatementAst] -and $n.Extent.Text.StartsWith("if(`$recovery -eq 'RESTART_REQUIRED')")},$true)
Assert ($branch.Count -eq 1) 'Exactly one public restart handling branch'
$temp=Join-Path $PSScriptRoot ('recovery-mock-'+[guid]::NewGuid().ToString('N').Substring(0,8));New-Item -ItemType Directory -Path $temp|Out-Null
$prefix=@'
param([string]$ToolRoot,[string]$OutputRoot,[ValidateSet('same','new')][string]$Case)
$ErrorActionPreference='Stop'
. (Join-Path $ToolRoot 'CameraDma.ps1')
. (Join-Path $ToolRoot 'CameraSelection.ps1')
$previous=[pscustomobject]@{pendingPid=0;unsafeToContinue=$true;captureClosed=$false;fullMftDecoded=$false;rebootRequired=$false;bootIdentity=[pscustomobject]@{recordId=100;timeUtc='2026-11-04T01:00:00Z'}}
$now=if($Case -eq 'same'){$previous.bootIdentity}else{[pscustomobject]@{recordId=101;timeUtc='2026-11-04T02:00:00Z'}}
$r=[ordered]@{unsafeToContinue=$false;rebootRequired=$false;status='PREPARING';bootIdentity=$now;boot='2026-11-04T06:00:00+05:00';driverActions=$false}
$exitCode=1
function Note([string]$Text){$r|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $OutputRoot 'state.json') -Encoding UTF8;Write-Output $Text}
$recovery=Get-CameraRecoveryAction $previous $r.bootIdentity $r.boot
'@
$suffix=@'
if($recovery -eq 'NEW_BOOT'){$r.previousSessionRetiredByNewBoot=$true}
$r|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $OutputRoot 'state.json') -Encoding UTF8
Set-Content -LiteralPath (Join-Path $OutputRoot 'past-restart-gate.txt') -Value 'No actual driver operation; execution reached mock next step'
exit 0
'@
$script=Join-Path $temp 'Run-RecoveryBranch.ps1';[IO.File]::WriteAllText($script,$prefix+"`n"+$branch[0].Extent.Text+"`n"+$suffix,[Text.UTF8Encoding]::new($true))
foreach($case in 'same','new'){
 $dir=Join-Path $temp $case;New-Item -ItemType Directory -Path $dir|Out-Null
 $p=Start-Process -FilePath "$env:windir\System32\WindowsPowerShell\v1.0\powershell.exe" -ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-File',('"'+$script+'"'),'-ToolRoot',('"'+$root+'"'),'-OutputRoot',('"'+$dir+'"'),'-Case',$case) -WindowStyle Hidden -RedirectStandardOutput (Join-Path $dir 'out.txt') -RedirectStandardError (Join-Path $dir 'err.txt') -PassThru;$null=$p.Handle
 if(-not $p.WaitForExit(15000)){throw 'Mock recovery child still running'};$p.WaitForExit();$p.Refresh()
 $state=Get-Content -LiteralPath (Join-Path $dir 'state.json') -Raw|ConvertFrom-Json
 if($case -eq 'same'){
  Assert ($p.ExitCode -eq 3010 -and $state.status -eq 'PENDING253_PREVIOUS_SESSION_RESTART' -and $state.unsafeToContinue -and $state.rebootRequired) 'Exact public same-boot branch returns restart 3010 and preserves unsafe state'
  Assert (-not (Test-Path -LiteralPath (Join-Path $dir 'past-restart-gate.txt')) -and -not $state.driverActions) 'No continuation or simulated binding in held session'
  Assert ((Get-Content -LiteralPath (Join-Path $dir 'out.txt') -Raw) -match '^RESTART REQUIRED:') 'Required restart is printed explicitly'
 }else{
  Assert ($p.ExitCode -eq 0 -and $state.previousSessionRetiredByNewBoot -and (Test-Path -LiteralPath (Join-Path $dir 'past-restart-gate.txt'))) 'Exact public new-boot branch can proceed to fresh qualification'
 }
}

# Exercise the actual public CMD with a stub PowerShell target returning 3010.
$cmdDir=Join-Path $temp 'cmd';New-Item -ItemType Directory -Path $cmdDir|Out-Null
Copy-Item -LiteralPath (Join-Path $root 'Setup.cmd') -Destination $cmdDir
Copy-Item -LiteralPath (Join-Path $root 'Install-QualcommISP.cmd') -Destination $cmdDir
Set-Content -LiteralPath (Join-Path $cmdDir 'Setup.ps1') -Value "Write-Output 'RESTART REQUIRED: mock previous session';exit 3010" -Encoding ASCII
$p=Start-Process -FilePath $env:ComSpec -ArgumentList @('/d','/c',('""'+(Join-Path $cmdDir 'Install-QualcommISP.cmd')+'" <nul"')) -WindowStyle Hidden -RedirectStandardOutput (Join-Path $cmdDir 'out.txt') -RedirectStandardError (Join-Path $cmdDir 'err.txt') -PassThru;$null=$p.Handle
if(-not $p.WaitForExit(15000)){throw 'Mock public CMD still active'};$p.WaitForExit();$p.Refresh();$text=Get-Content -LiteralPath (Join-Path $cmdDir 'out.txt') -Raw
Assert ($p.ExitCode -eq 3010) 'Public Install CMD propagates restart exit code'
Assert ($text -match 'In Windows select Restart' -and $text -notmatch 'SUCCESS|SETUP STOPPED') 'Public CMD clearly displays restart instructions without marking failure or success'
[ordered]@{status='PASS_R23_PREVIOUS_SESSION_RESTART_RECOVERY';date='2026-11-04';tests=$passed;mockWindowsBoot=$true;liveStateChanged=$false;driverChanged=$false;cameraOpened=$false}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $PSScriptRoot 'restart-recovery-regression.json') -Encoding UTF8
Write-Output ('PASS_R23_PREVIOUS_SESSION_RESTART_RECOVERY count='+$passed.Count)
