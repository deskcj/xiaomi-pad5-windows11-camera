$ErrorActionPreference='Stop'
$b=Split-Path $PSScriptRoot
$wpr='C:\Windows\System32\wpr.exe'
$status=& $wpr -status 2>&1;$exit=$LASTEXITCODE
$status|Set-Content -LiteralPath (Join-Path $b 'research\wpr-preflight079.txt') -Encoding UTF8
[ordered]@{time=(Get-Date -Format o);exit=$exit;status=($status -join "`n");elevated=([Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $b 'research\wpr-preflight079.json') -Encoding UTF8
& $wpr -profiles (Join-Path $b 'research\QispCpu079.wprp') *> (Join-Path $b 'research\wpr-profile-validation079.log')
'WPR_PROFILE_VALIDATION_EXIT='+$LASTEXITCODE|Add-Content -LiteralPath (Join-Path $b 'research\wpr-profile-validation079.log')
$assembly='H:\Vlad\EWDK\Program Files\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\PrivateAssemblies\Microsoft.Diagnostics.Tracing.TraceEvent.dll'
$a=[Reflection.Assembly]::LoadFrom($assembly)
$types=@('Microsoft.Diagnostics.Tracing.Etlx.TraceLog','Microsoft.Diagnostics.Tracing.Etlx.TraceCodeAddress','Microsoft.Diagnostics.Tracing.Etlx.TraceProcess','Microsoft.Diagnostics.Tracing.ETWTraceEventSource')
$lines=@('Assembly: '+$a.FullName)
foreach($name in $types){$t=$a.GetType($name,$true);$lines+=@($name);$lines+=@($t.GetMethods()|Where-Object Name -match 'Open|Convert|CallStack|Process|CodeAddress|Events|Thread|FileName'|ForEach-Object ToString);$lines+=@($t.GetProperties()|ForEach-Object ToString)}
$lines|Set-Content -LiteralPath (Join-Path $b 'research\traceevent-api079.txt') -Encoding UTF8
