$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$repo='H:\Vlad\Codex\QualcommISP';$lock=$null
try{
 . (Join-Path $repo 'Build207\tools\Query-QispTraces207.ps1') -ImportOnly
 $null=Assert-NoQispTraces207
 if(@(Get-Process WindowsCamera,'QispCapture*','ReadMft*',tracerpt -ErrorAction SilentlyContinue).Count -or (Get-Service FrameServer).Status -ne 'Stopped'){throw 'Closed capture/decoder required before packaging'}
 $lock=[IO.File]::Open('C:\ProgramData\CodexQispBuild062\operation.lock',[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
 $p=Start-Process -FilePath 'C:\Users\Xiaomi\AppData\Local\Python\pythoncore-3.15-64\python.exe' -ArgumentList (Join-Path $repo 'Build243\tools\Adopt-Reference243.py') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $repo 'Build243\research\adopt243c-stdout.txt') -RedirectStandardError (Join-Path $repo 'Build243\research\adopt243c-stderr.txt')
 $null=$p.Handle;$p.WaitForExit();$exit=$p.ExitCode
 [ordered]@{pid=$p.Id;exit=$exit;completed=(Get-Date -Format o)}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $repo 'Build243\research\adopt243c-exit.json') -Encoding UTF8
 if($exit -ne 0){throw 'Adoption243 stopped; inspect full native stderr before retry'}
}finally{if($lock){$lock.Dispose()}}
