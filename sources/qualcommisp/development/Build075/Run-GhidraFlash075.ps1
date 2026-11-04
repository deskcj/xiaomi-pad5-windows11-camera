$ErrorActionPreference='Stop'
$scope='H:\Vlad\Codex\QualcommISP\Build075\research\ghidra-flash075'
if(Test-Path -LiteralPath $scope){throw 'Preserve previous scope'}
if(@(Get-Process -Name 'QispCapture*','QispFocus*','CamProbe*','WindowsCamera','java','devcon','pnputil' -ErrorAction SilentlyContinue).Count){throw 'Wait until camera/native and other offline owners exit'}
$source='C:\Windows\System32\DriverStore\FileRepository\qccamflash8150.inf_arm64_1325844af82335fd\qccamflash8150.sys'
$sha='40e3648ab45028cbaf5898a3980ed5bd84e12194061e4ee151df42034ce1e89e'
if((Get-FileHash -LiteralPath $source).Hash -ne $sha){throw 'Flash source differs'}
foreach($p in 'profile','tmp','project','export'){New-Item -ItemType Directory -Path (Join-Path $scope $p) -Force|Out-Null}
$old='H:\Vlad\Codex\QualcommISP\Build075\research\ghidra-focus075'
$r=Get-Content -Raw -LiteralPath (Join-Path $old 'run.json')|ConvertFrom-Json
$argsList=@($r.args|ForEach-Object {$_.Replace($old,$scope).Replace('Focus075','Flash075')})
$i=[array]::IndexOf($argsList,'-import');$argsList[$i+1]=$source
$i=[array]::IndexOf($argsList,'focus_init:307d0:1d0')
if($i -lt 0){throw 'Original invocation differs'}
$argsList=@($argsList[0..($i-1)])+@('flash_acquire:1790:330','flash_close:1ac0:1a0','flash_ioctl:21d0:280','-deleteProject')
$jdk='H:\Vlad\Codex\Win11Pad5\tools\microsoft-jdk21-arm64\jdk21\bin\java.exe'
$p=Start-Process -FilePath $jdk -ArgumentList $argsList -WindowStyle Hidden -RedirectStandardOutput (Join-Path $scope 'stdout.log') -RedirectStandardError (Join-Path $scope 'stderr.log') -PassThru
[ordered]@{status='RUNNING_OFFLINE_ONLY';pid=$p.Id;source=$source;sha256=$sha;started=(Get-Date).ToString('o');args=$argsList}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $scope 'run.json') -Encoding UTF8
'GHIDRA_FLASH_PID='+$p.Id
