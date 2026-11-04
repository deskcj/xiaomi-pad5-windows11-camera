param([ValidateSet('ru-RU','en-US')][string]$Locale)
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
[Threading.Thread]::CurrentThread.CurrentCulture=[Globalization.CultureInfo]::GetCultureInfo($Locale)
[Threading.Thread]::CurrentThread.CurrentUICulture=[Globalization.CultureInfo]::GetCultureInfo($Locale)
$package=Split-Path -Parent $PSScriptRoot
. (Join-Path $package 'CameraSelection.ps1')
$tokens=$null;$errors=$null;$ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $package 'Setup.ps1'),[ref]$tokens,[ref]$errors)
if($errors.Count){throw 'Invalid installer syntax'}
foreach($name in 'Can-RetryNativeEmptyStart','Capture-WithWarmStartRetry'){
 $f=$ast.FindAll({param($n)$n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq $name},$true)
 if($f.Count -ne 1){throw 'Missing warm start function'};Invoke-Expression $f[0].Extent.Text
}
$passed=[Collections.Generic.List[string]]::new()
function Assert([bool]$condition,[string]$name){if(-not $condition){throw ('FAIL '+$name)};$passed.Add($name)}
$text=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'fixtures\empty-start-closed.txt') -Raw
$close=[pscustomobject]@{status='PASS';eventsLost=0};$decode=[pscustomobject]@{status='PASS_DECODE';eventsLost=0}
$state=@{pendingPid=0;unsafeToContinue=$false;captureClosed=$true;fullMftDecoded=$true}
Assert (Can-RetryNativeEmptyStart $text $close $decode $state) 'Recorded zero-frame front startup eligible only after clean retirement and full trace'
foreach($field in 'pendingPid','unsafeToContinue','captureClosed','fullMftDecoded'){
 $bad=$state.Clone();$bad[$field]=if($field -in 'captureClosed','fullMftDecoded'){$false}else{1}
 Assert (-not(Can-RetryNativeEmptyStart $text $close $decode $bad)) ('Blocked state cannot retry '+$field)
}
Assert (-not(Can-RetryNativeEmptyStart ($text.Replace('frames=0','frames=1')) $close $decode $state)) 'Green or empty-pixel frames remain terminal'
Assert (-not(Can-RetryNativeEmptyStart ($text+"`nERROR256 hr=80004005 message=failure") $close $decode $state)) 'WinRT error cannot retry'
Assert (-not(Can-RetryNativeEmptyStart ($text.Replace('controlsChanged=false','controlsChanged=true')) $close $decode $state)) 'Changed camera controls cannot retry'
Assert (-not(Can-RetryNativeEmptyStart ($text.Replace('reader_stop hr=00000000','reader_stop hr=80004005')) $close $decode $state)) 'Failed reader stop cannot retry'
Assert (-not(Can-RetryNativeEmptyStart $text ([pscustomobject]@{status='HOLD';eventsLost=0}) $decode $state)) 'Missing native close cannot retry'
Assert (-not(Can-RetryNativeEmptyStart $text $close ([pscustomobject]@{status='PASS';eventsLost=1}) $state)) 'Lost trace events cannot retry'
$run=Join-Path $PSScriptRoot ('verification\warm-start-'+$Locale)
New-Item -ItemType Directory -Path (Join-Path $run 'primary') -Force|Out-Null
[IO.File]::WriteAllText((Join-Path $run 'primary-probe.out.log'),$text)
$close|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $run 'primary\native-close.json')
$decode|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $run 'primary\mft-decode.json')
$Mode='Install';$r=[ordered]@{pendingPid=0;unsafeToContinue=$false;captureClosed=$true;fullMftDecoded=$true}
$script:count=0;$script:labels=@();$script:failSecond=$false
function Note([string]$message){}
function Capture-One([string]$side,[string]$label,[bool]$native){$script:count++;$script:labels+=@($label);if($script:count -eq 1 -or $script:failSecond){throw 'Recorded first startup, zero frames'}}
Capture-WithWarmStartRetry 'front' 'primary' $true
Assert ($script:count -eq 2 -and $script:labels[1] -eq 'primary-retry1' -and $r.verificationRetries.Count -eq 1) 'Production orchestration retries exactly once and records first failure'
$script:count=0;$script:labels=@();$script:failSecond=$true;$caught=$false
try{Capture-WithWarmStartRetry 'front' 'primary' $true}catch{$caught=$true}
Assert ($caught -and $script:count -eq 2) 'Second failure terminal, no third retry'
$script:count=0;$Mode='Diagnose';$caught=$false
try{Capture-WithWarmStartRetry 'front' 'primary' $true}catch{$caught=$true}
Assert ($caught -and $script:count -eq 1) 'Diagnostics preserve original failure without retry'
$script:count=0;$Mode='Install';$caught=$false
try{Capture-WithWarmStartRetry 'front' 'primary' $false}catch{$caught=$true}
Assert ($caught -and $script:count -eq 1) 'CPU/GPU paths have no native retry'
$result=[ordered]@{status='PASS_BOUNDED_NATIVE_WARM_START_WITH_SAFE_GATES';locale=$Locale;tests=$passed;machineRegistryChanged=$false;cameraOpened=$false}
$result|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $run 'result.json') -Encoding UTF8
'PASS warm-start '+$Locale+' tests='+$passed.Count
