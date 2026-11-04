$ErrorActionPreference='Stop'
$scope='H:\Vlad\Codex\QualcommISP\Build079\research\ghidra-avs-flow079'
if(Test-Path -LiteralPath $scope){throw 'Preserve previous scope'}
if(@(Get-Process -Name 'QispCapture*','QispFocus*','CamProbe*','WindowsCamera','java','devcon','pnputil' -ErrorAction SilentlyContinue).Count){throw 'Other owner present'}
$source='H:\Vlad\Codex\QualcommISP\Build075\candidate-focus-b\AVStream\qccamavs8150.sys'
$index=Get-Content -Raw -LiteralPath 'H:\Vlad\Codex\QualcommISP\Build079\research\flow-avs079c\index.json'|ConvertFrom-Json
if((Get-FileHash -LiteralPath $source).Hash -ne $index.sha256){throw 'Indexed source changed'}
foreach($n in 'profile','tmp','project','export'){New-Item -ItemType Directory -Path (Join-Path $scope $n) -Force|Out-Null}
$old='H:\Vlad\Codex\QualcommISP\Build075\research\ghidra-focus075'
$r=Get-Content -Raw -LiteralPath (Join-Path $old 'run.json')|ConvertFrom-Json
$argsList=@($r.args|ForEach-Object {$_.Replace($old,$scope).Replace('Focus075','AvsFlow079')})
$i=[array]::IndexOf($argsList,'-import');$argsList[$i+1]=$source
$i=[array]::IndexOf($argsList,'focus_init:307d0:1d0');if($i -lt 0){throw 'Original invocation differs'}
$argsList=@($argsList[0..($i-1)])+@('process_stats:5010:90','send_packet:81c0:6ec','metadata:7bfb8:284','ife_request:7d458:5ec','isp_worker:757d0:44','-deleteProject')
$jdk='H:\Vlad\Codex\Win11Pad5\tools\microsoft-jdk21-arm64\jdk21\bin\java.exe'
$p=Start-Process -FilePath $jdk -ArgumentList $argsList -WindowStyle Hidden -RedirectStandardOutput (Join-Path $scope 'stdout.log') -RedirectStandardError (Join-Path $scope 'stderr.log') -PassThru
[ordered]@{status='RUNNING_OFFLINE_ONLY';pid=$p.Id;source=$source;sha256=$index.sha256;started=(Get-Date).ToString('o');args=$argsList}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $scope 'run.json') -Encoding UTF8
'GHIDRA_AVS_FLOW_PID='+$p.Id
