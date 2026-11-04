$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot
$wrapper=Join-Path $PSScriptRoot 'Install075-WithLog.ps1'
if(Test-Path -LiteralPath $wrapper){throw 'Preserve existing install launcher'}
@'
$ErrorActionPreference='Stop'
Start-Transcript -LiteralPath 'H:\Vlad\Codex\QualcommISP\Build075\research\install075-transcript.log'
try{& 'H:\Vlad\Codex\QualcommISP\Build075\tools\Install-Focus075b.ps1'}
catch{$_|Format-List * -Force|Out-String|Set-Content -LiteralPath 'H:\Vlad\Codex\QualcommISP\Build075\research\install075-error.txt';throw}
finally{Stop-Transcript}
'@|Set-Content -LiteralPath $wrapper -Encoding UTF8
$p=Start-Process -FilePath (Get-Process -Id $PID).Path -ArgumentList @('-NoProfile','-File',$wrapper) -WindowStyle Hidden -PassThru
[ordered]@{pid=$p.Id;launched=(Get-Date).ToString('o');script=$wrapper}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $root 'research\install075-launch.json') -Encoding UTF8
'INSTALL075_PID='+$p.Id
