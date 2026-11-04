$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
if($PSVersionTable.PSEdition -eq 'Desktop'){$env:PSModulePath=(Join-Path $PSHOME 'Modules')+';'+[Environment]::GetEnvironmentVariable('PSModulePath',[EnvironmentVariableTarget]::Machine)}
$taskRoot=Split-Path -Parent $PSScriptRoot
$root=$taskRoot
. (Join-Path $root 'CameraDma.ps1')
. (Join-Path $root 'CameraSelection.ps1')
$passed=[Collections.Generic.List[string]]::new()
function Assert([bool]$Condition,[string]$Name){if(-not $Condition){throw ('FAIL '+$Name)};$passed.Add($Name)}
function Expect-Reject([scriptblock]$Action,[string]$Name){$rejected=$false;try{& $Action|Out-Null}catch{$rejected=$true};Assert $rejected $Name}
$snapshot=[pscustomobject]@{device='test-only';driverKey='Registry::test-only';service='qcsmmu';sha256='2fa1883f3252d2e50d63911fc9ff435f88e75210f429e86686e395e0827e1299';chipSetVersion=2;problem=0;maps=@([pscustomobject]@{suffix='0\S2CB\0x01\0x04\0x16';hex='ff20800020'},[pscustomobject]@{suffix='0\S2CB\0x01\0x04\0x11';hex='ff20c00000'})}
$plan=Get-CameraDmaPlanFromSnapshot $snapshot
Assert ($plan.updates.Count -eq 2) 'Original BSP requires exactly two updates'
$snapshot.maps[0].hex='ff20800020ff21000020';$snapshot.maps[1].hex='ff20c00000ff21400000'
Assert ((Get-CameraDmaPlanFromSnapshot $snapshot).updates.Count -eq 0) 'Prepared mappings are idempotent'
$snapshot.maps[1].hex='ff20c00000'
Assert ((Get-CameraDmaPlanFromSnapshot $snapshot).updates.Count -eq 1) 'Partial known preparation changes only remaining entry'
$snapshot.maps[0].hex='ff10800020'
Expect-Reject {Get-CameraDmaPlanFromSnapshot $snapshot} 'Wrong silicon baseline rejected'
$snapshot.maps[0].hex='ff20800020';$snapshot.sha256='invalid'
Expect-Reject {Get-CameraDmaPlanFromSnapshot $snapshot} 'Unknown SMMU driver rejected'
$snapshot.sha256='2fa1883f3252d2e50d63911fc9ff435f88e75210f429e86686e395e0827e1299'
$snapshot.maps+=@([pscustomobject]@{suffix='0\S2CB\0x02\0x04\0x01';hex='ff21000020'})
Expect-Reject {Get-CameraDmaPlanFromSnapshot $snapshot} 'Cross-context routing collision rejected'
$snapshot.maps=$snapshot.maps[0..1];$snapshot.maps+=@([pscustomobject]@{suffix='0\S2CB\0x01\0x04\0x10';hex='ff1234'})
Expect-Reject {Get-CameraDmaPlanFromSnapshot $snapshot} 'Malformed native MAP rejected'
Expect-Reject {Convert-CameraDmaHex 'ffzz'} 'Invalid binary value rejected'
$boot=[pscustomobject]@{recordId=10;timeUtc='2026-11-04T00:00:00Z'}
Assert (-not(Test-CameraNewBoot $boot $boot)) 'Same kernel session never satisfies restart gate'
Assert (Test-CameraNewBoot $boot ([pscustomobject]@{recordId=11;timeUtc='2026-11-04T00:01:00Z'})) 'New kernel startup satisfies restart gate'

# Exhaustively check that the two additions change only the three intended SIDs.
$changed=@()
for($sid=0;$sid -lt 32768;$sid++){
 $before22=(($sid -bxor 0x2080) -band (0x7fff -bxor 0x20)) -eq 0
 $after22=$before22 -or ((($sid -bxor 0x2100) -band (0x7fff -bxor 0x20)) -eq 0)
 $before17=$sid -eq 0x20c0;$after17=$before17 -or $sid -eq 0x2140
 if($before22 -ne $after22 -or $before17 -ne $after17){$changed+=@($sid)}
}
Assert (($changed -join ',') -eq (@(0x2100,0x2120,0x2140)-join ',')) '32768 StreamIDs: only 2100,2120,2140 change'

# Parse every shipped PowerShell file under Windows PowerShell 5.1.
foreach($file in @(Get-ChildItem -LiteralPath $root -Recurse -Filter '*.ps1' -File)){
 $tokens=$null;$errors=$null;$ast=[Management.Automation.Language.Parser]::ParseFile($file.FullName,[ref]$tokens,[ref]$errors)
 Assert (@($errors).Count -eq 0) ('PS51 syntax '+$file.Name)
 if($file.Name -eq 'Setup.ps1'){$installerAst=$ast}
}

# Run the real Capture-One orchestration with recorded friend output and mocked
# children. No camera, trace session, native filter or registry is touched.
$function=$installerAst.FindAll({param($node)$node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Capture-One'},$true)[0]
Invoke-Expression $function.Extent.Text
Add-Type @'
public static class PadCameraNative {
 public static int NtQueryDebugFilterState(uint c,uint l){return 1;}
 public static int NtSetDebugFilterState(uint c,uint l,byte e){return 0;}
}
'@
function Assert-Idle{}
function Memory-Guard{}
function Save-State{}
function Note([string]$Text){}
function Write-AtomicJson([string]$Path,[string]$Json){[IO.File]::WriteAllText($Path,$Json)}
function Find-Physical([string]$Slot){[pscustomobject]@{id='DISPLAY\QCOM_AVStream_8150\test'}}
function Get-CameraInventory([string]$Label){[pscustomobject]@{devices=@([pscustomobject]@{id='\\?\DISPLAY#QCOM_AVStream_8150#test#{e5323777-f976-4f5b-9b55-b94699c46e44}\{5584F823-3830-4CFD-947F-78DE17A8B14C}';instanceId='DISPLAY\QCOM_AVStream_8150\test';name='Renamed camera';enabled=$true;panel=2})}}
$run=Join-Path $PSScriptRoot ('capture-mock-'+[guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $run|Out-Null
$cache='';$sessions=@();$mftDecoded=$false
$r=[ordered]@{pendingPid=0;unsafeToContinue=$false;captureClosed=$true;fullMftDecoded=$true;captures=@();errors=@()}
$friendLog=Join-Path $PSScriptRoot 'fixtures\green-closed.txt'
function Run-Child([string]$Label,[string]$Exe,[string[]]$Arguments,[int]$Timeout=60000){
 if($Label -like '*-probe'){$content=Get-Content -LiteralPath $friendLog -Raw;Set-Content -LiteralPath (Join-Path $run ($Label+'.out.log')) -Value $content;return $content}
 if($Label -like '*-native-close'){throw 'Recorded missing native close evidence'}
 if($Label -like '*-mft-decode'){
  $script:mftDecoded=$true
  [ordered]@{status='PASS_DECODE';eventsLost=0}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $run 'verify-rear\mft-decode.json')
 }
 return ''
}
Expect-Reject {Capture-One 'rear' 'verify-rear' $true} 'Friend empty frame remains failure, never promoted to success'
Assert ($mftDecoded -and $r.fullMftDecoded) 'Full MFT decoded even after native close rejection'
Assert ($r.unsafeToContinue -and $r.captureClosed) 'Unqualified native close blocks further same-session work'
Assert (@($r.errors|Where-Object {$_ -match 'empty pixels'}).Count -eq 1) 'Empty/green image cause recorded alongside trace failure'
Assert ($r.captures.Count -eq 0) 'Failed rear test cannot enter successful camera list'

# Regression for the new friend failure: enumeration error before object creation.
$script:mockText="CLOCK256 frequency=19200000`nERROR256 hr=8000ffff message=Exactly one selected camera required`nSTATE256 captureCreated=0 initializeAttempted=0 readerCreated=0 startAttempted=0 readerStarted=0`nRETIRE256 neverOpened=1 readerStopped=1 readerClosed=1 captureClosed=1`nRESULT256 hr=8000ffff frames=0 signal=false saved=false controlsChanged=false"
$script:nativeCloseCalled=$false;$script:mftDecoded=$false
function Run-Child([string]$Label,[string]$Exe,[string[]]$Arguments,[int]$Timeout=60000){
 if($Label -like '*-probe'){Set-Content -LiteralPath (Join-Path $run ($Label+'.out.log')) -Value $script:mockText;throw 'Probe exited 5'}
 if($Label -like '*-native-close'){$script:nativeCloseCalled=$true;throw 'Missing native close evidence'}
 if($Label -like '*-mft-decode'){$script:mftDecoded=$true;[ordered]@{status='PASS_DECODE';eventsLost=0}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $run 'no-opening\mft-decode.json')}
 return ''
}
$r=[ordered]@{pendingPid=0;unsafeToContinue=$false;captureClosed=$true;fullMftDecoded=$true;captures=@();errors=@()}
$failure='';try{Capture-One 'rear' 'no-opening' $true}catch{$failure=$_.Exception.Message}
Assert ($failure -match 'Exactly one selected camera' -and $failure -notmatch 'Capture close not proven') ('Original enumeration error preserved; never-opened state is not mistaken for close failure: '+$failure)
Assert ($mftDecoded -and $r.fullMftDecoded -and -not $nativeCloseCalled) 'Never-opened failure decodes full MFT without inventing native initialization'
Assert ($r.captureClosed -and -not $r.unsafeToContinue -and $r.captures.Count -eq 0) 'Explicit never-opened failure remains safe and is never an image success'
Assert (@($r.errors|Where-Object {$_ -match 'empty pixels'}).Count -eq 0) 'Zero frames on enumeration failure are not misreported as green hardware output'

# Absence of lifecycle evidence must still hold, but both decodes must execute.
$script:mockText="ERROR256 hr=8000ffff message=Exactly one named camera required`nRESULT256 hr=8000ffff frames=0 signal=false saved=false controlsChanged=false"
$script:mftDecoded=$false;$script:nativeCloseCalled=$false
function Run-Child([string]$Label,[string]$Exe,[string[]]$Arguments,[int]$Timeout=60000){
 if($Label -like '*-probe'){Set-Content -LiteralPath (Join-Path $run ($Label+'.out.log')) -Value $script:mockText;throw 'Probe exited 5'}
 if($Label -like '*-native-close'){$script:nativeCloseCalled=$true;throw 'Missing native close evidence'}
 if($Label -like '*-mft-decode'){$script:mftDecoded=$true;[ordered]@{status='PASS_DECODE';eventsLost=0}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $run 'missing-lifecycle\mft-decode.json')}
 return ''
}
$r=[ordered]@{pendingPid=0;unsafeToContinue=$false;captureClosed=$true;fullMftDecoded=$true;captures=@();errors=@()}
Expect-Reject {Capture-One 'rear' 'missing-lifecycle' $true} 'Legacy absence of close or explicit lifecycle is still rejected'
Assert ($mftDecoded -and $nativeCloseCalled -and $r.unsafeToContinue -and -not $r.captureClosed) 'Unsafe close still decodes diagnostics before retaining the unsafe state'

# Pre-install release can repair a bad old image only after real close evidence.
$script:mockText=Get-Content -LiteralPath $friendLog -Raw
function Run-Child([string]$Label,[string]$Exe,[string[]]$Arguments,[int]$Timeout=60000){
 if($Label -like '*-probe'){Set-Content -LiteralPath (Join-Path $run ($Label+'.out.log')) -Value $script:mockText;throw 'Empty signal exited 5'}
 if($Label -like '*-mft-decode'){$script:mftDecoded=$true;$directory=$Label -replace '-mft-decode$','';[ordered]@{status='PASS_DECODE';eventsLost=0}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $run ($directory+'\mft-decode.json'))}
 return ''
}
$r=[ordered]@{pendingPid=0;unsafeToContinue=$false;captureClosed=$true;fullMftDecoded=$true;captures=@();errors=@()}
Capture-One 'rear' 'old-green-release' $true $true
Assert ($r.captureClosed -and -not $r.unsafeToContinue -and $r.captures.Count -eq 0) 'Closed old green stream permits repair but is not recorded as successful camera image'
Expect-Reject {Capture-One 'rear' 'new-green-verify' $true} 'Same green output after installation remains failure'

[ordered]@{status='PASS_INSTALLER_R2_REGRESSIONS';powershell=[string]$PSVersionTable.PSVersion;tests=$passed;cameraOpened=$false;driverChanged=$false;registryChanged=$false}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $PSScriptRoot 'regressions.json') -Encoding UTF8
Write-Output ('PASS_INSTALLER_R2_REGRESSIONS count='+$passed.Count)
