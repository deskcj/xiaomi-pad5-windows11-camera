$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$repo='H:\Vlad\Codex\QualcommISP';$lock=$null
try{
 . (Join-Path $repo 'Build207\tools\Query-QispTraces207.ps1') -ImportOnly
 $null=Assert-NoQispTraces207
 if(@(Get-Process WindowsCamera,'QispCapture*','ReadMft*',tracerpt -ErrorAction SilentlyContinue).Count -or (Get-Service FrameServer).Status -ne 'Stopped'){throw 'Closed capture/decoder required before packaging'}
 $lock=[IO.File]::Open('C:\ProgramData\CodexQispBuild062\operation.lock',[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
 & 'C:\Users\Xiaomi\AppData\Local\Python\pythoncore-3.15-64\python.exe' (Join-Path $repo 'Build243\tools\Adopt-Reference243.py') *> (Join-Path $repo 'Build243\research\adopt243.log')
 if($LASTEXITCODE){throw 'Adoption243 stopped; inspect exact journal/log before retry'}
}finally{if($lock){$lock.Dispose()}}
