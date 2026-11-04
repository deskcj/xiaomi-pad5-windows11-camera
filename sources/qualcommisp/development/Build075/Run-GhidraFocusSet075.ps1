$ErrorActionPreference='Stop'
$scope='H:\Vlad\Codex\QualcommISP\Build075\research\ghidra-focus-set075'
if(Test-Path -LiteralPath $scope){throw 'Preserve previous Ghidra scope'}
foreach($p in 'profile','tmp','project','export'){New-Item -ItemType Directory -Path (Join-Path $scope $p)-Force|Out-Null}
$old='H:\Vlad\Codex\QualcommISP\Build075\research\ghidra-focus075'
$r=Get-Content -Raw -LiteralPath (Join-Path $old 'run.json')|ConvertFrom-Json
$argsList=@($r.args|ForEach-Object {$_.Replace($old,$scope).Replace('Focus075','FocusSet075')})
$i=[array]::IndexOf($argsList,'focus_init:307d0:1d0')
if($i -lt 0){throw 'Expected original invocation'}
$argsList=@($argsList[0..($i-1)])+@('capture_focus_set:16cfd0:720','-deleteProject')
$jdk='H:\Vlad\Codex\Win11Pad5\tools\microsoft-jdk21-arm64\jdk21\bin\java.exe'
$p=Start-Process -FilePath $jdk -ArgumentList $argsList -WindowStyle Hidden -RedirectStandardOutput (Join-Path $scope 'stdout.log') -RedirectStandardError (Join-Path $scope 'stderr.log') -PassThru
[ordered]@{status='RUNNING_OFFLINE_ONLY';pid=$p.Id;started=(Get-Date).ToString('o');args=$argsList}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $scope 'run.json') -Encoding UTF8
'GHIDRA_FOCUS_SET_PID='+$p.Id
