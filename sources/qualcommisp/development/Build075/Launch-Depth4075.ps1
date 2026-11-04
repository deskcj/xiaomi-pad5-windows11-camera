$ErrorActionPreference='Stop'
$b='H:\Vlad\Codex\QualcommISP\Build075'
$journal=Join-Path $b 'research\depth4075-launch.json'
$r=[ordered]@{status='WAITING_CALLER_EXIT';pid=$PID;started=(Get-Date -Format o);errors=@()}
$r|ConvertTo-Json|Set-Content -LiteralPath $journal -Encoding UTF8
Start-Sleep -Seconds 12
Start-Transcript -LiteralPath (Join-Path $b 'research\depth4075-launch.log') | Out-Null
try {
 & (Join-Path $b 'tools\Run-Depth4075.ps1')
 $r.status='CHILD_RETURNED'
 if(Test-Path -LiteralPath (Join-Path $b 'research\depth4075.json')){$child=Get-Content -LiteralPath (Join-Path $b 'research\depth4075.json') -Raw|ConvertFrom-Json;$r.status=$child.status}
} catch {$r.status='FAILED_BEFORE_OPERATOR_JOURNAL';$r.errors+=@($_|Out-String);$r.detail=($_|Format-List * -Force|Out-String)}
finally {$r.completed=(Get-Date -Format o);$r|ConvertTo-Json -Depth 6|Set-Content -LiteralPath $journal -Encoding UTF8;Stop-Transcript|Out-Null}