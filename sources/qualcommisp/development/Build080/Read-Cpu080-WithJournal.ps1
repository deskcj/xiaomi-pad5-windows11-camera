$ErrorActionPreference='Stop'
$b=Split-Path $PSScriptRoot;$journal=Join-Path $b 'research\cpu-reader080.json'
if(Test-Path -LiteralPath $journal){throw 'Preserve reader journal'}
$capture=Get-Content -Raw -LiteralPath (Join-Path $b 'research\cpu080.json')|ConvertFrom-Json
if($capture.status -ne 'PASS080_BOTH_NATIVE_VIDEO_CLOSED_CPU_TRACE_RECORDED' -or $capture.pending){throw 'Completed CPU trace required'}
$output=Join-Path $b 'research\cpu-top080.json'
$p=Start-Process -FilePath 'H:\Vlad\Codex\QualcommISP\Build079\bin\ReadCpu079f.exe' -ArgumentList @($capture.etl,$output) -WindowStyle Hidden -RedirectStandardOutput (Join-Path $b 'research\cpu-reader080.log') -RedirectStandardError (Join-Path $b 'research\cpu-reader080.err') -PassThru
$null=$p.Handle
$r=[ordered]@{status='RUNNING_OFFLINE';pid=$p.Id;started=(Get-Date -Format o);source=$capture.etl;output=$output;pending=$true}
$r|ConvertTo-Json|Set-Content -LiteralPath $journal -Encoding UTF8
if($p.WaitForExit(60000)){$p.WaitForExit();$p.Refresh();$r.pending=$false;$r.exit=$p.ExitCode;$r.status=if($p.ExitCode -eq 0 -and (Test-Path -LiteralPath $output)){'PASS080_CPU_READER_COMPLETED'}else{'FAILED080_CPU_READER'};$r.completed=(Get-Date -Format o)}else{$r.status='OFFLINE_READER_PENDING_NO_NEW_CAPTURE'}
$r|ConvertTo-Json|Set-Content -LiteralPath $journal -Encoding UTF8
$r|ConvertTo-Json
