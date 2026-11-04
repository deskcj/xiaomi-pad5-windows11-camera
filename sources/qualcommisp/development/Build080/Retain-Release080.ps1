$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$b=Split-Path $PSScriptRoot;$repo=Split-Path $b;$cache='C:\ProgramData\CodexQispBuild080';$dest=Join-Path $cache 'retained080';$lock=$null
try{
 $lock=[IO.File]::Open('C:\ProgramData\CodexQispBuild062\operation.lock',[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
 if(Test-Path -LiteralPath $dest){throw 'Preserve retained080'}
 if(@(Get-Process -Name 'WindowsCamera','QispCapture*','QispFocus*','CamProbe*','TorchPulse*','devcon','pnputil','tracerpt','wpr' -ErrorAction SilentlyContinue).Count){throw 'Native owner pending'}
 $validation=Get-Content -Raw -LiteralPath (Join-Path $b 'research\validation080.json')|ConvertFrom-Json
 $speed=Get-Content -Raw -LiteralPath (Join-Path $b 'research\speed080.json')|ConvertFrom-Json
 if($validation.status -ne 'PASS080_BOTH_PREVIEW_REPEAT_VIDEO_FOCUS_ROUNDTRIP_AF_RESTORE_NATIVE_CLOSE' -or $validation.pending -or $speed.status -ne 'PASS080_BOTH_VIDEO_30FPS_UNUSED_RELAY_CPU_REMOVED'){throw 'Completed native and speed tests required'}
 $state=Get-Content -Raw -LiteralPath (Join-Path $cache 'active-install.json')|ConvertFrom-Json
 if($state.status -ne 'PASS_NATIVE080_AVS_ACTIVE' -or $state.pending){throw 'Validated active080 required'}
 [Reflection.Assembly]::LoadFrom('H:\Vlad\Codex\QualcommISP\Build074\bin\ObserverTypes074.dll')|Out-Null
 & (Join-Path $PSScriptRoot 'Snapshot-Baseline080.ps1') -OutputDirectory (Join-Path $dest 'packages')
 . (Join-Path $PSScriptRoot 'CameraState.ps1')
 $snapshot=Get-Content -Raw -LiteralPath (Join-Path $dest 'packages\baseline.json')|ConvertFrom-Json
 foreach($p in $snapshot.packages){QispCheckSavedPackage $p (Join-Path $dest 'packages') -Current}
 $avs=@($snapshot.packages|Where-Object label -eq 'avs')[0]
 if($avs.version -ne '1.0.2324.80' -or $avs.publishedInf -ne 'oem414.inf'){throw 'Actual080 state differs'}
 $files=@()
 foreach($side in 'FrontData','RearData'){
  $dir='C:\ProgramData\CodexQispBuild061\active\package\Unified\'+$side;$out=Join-Path $dest $side
  New-Item -ItemType Directory -Path $out|Out-Null
  foreach($f in Get-ChildItem -LiteralPath $dir -File){
   $sha=(Get-FileHash -LiteralPath $f.FullName).Hash;Copy-Item -LiteralPath $f.FullName -Destination $out
   if((Get-FileHash -LiteralPath (Join-Path $out $f.Name)).Hash -ne $sha){throw 'Data copy differs'}
   $files+=@([ordered]@{side=$side;name=$f.Name;live=$f.FullName;backup=(Join-Path $out $f.Name);sha256=$sha;bytes=$f.Length})
  }
 }
 $r=[ordered]@{status='PASS080_RETAINED_BOTH_CAMERAS_VIDEO30FPS_MFT075_DATA074';retainedAt=(Get-Date -Format o);installedDriver=80;boot=$state.boot;nodes=$state.nodes;files=$files;retained=$dest;packages=(Join-Path $dest 'packages\baseline.json');validation=(Join-Path $b 'research\validation080.json');speedProof=(Join-Path $b 'research\speed080.json');signedKernelSHA256='cfda67e6369beecb864dbb1bdd552c220e99b460124abcac1b1e97b0e904f3bb';signedMFTSHA256='78f49dab2d22be71589d0f1ffc3e03cc0c59f0cc22cfac3dfa38c7579eb8e5f3';rollback075=(Join-Path $state.recovery 'Restore-Avs075.ps1');previousReference075='C:\ProgramData\CodexQispBuild075\retained075.json';androidParity=$false;improvement='One original IPE input-buffer release call restored, unused TP10 relay copy removed: front27.996→29.938/rear23.417→30.014FPS, repeated29.939/30.020; loop CPU12.4s→0';tuning='Unchanged074 exact Android Convergence graph on070 exposure; daylight optics and color not yet evaluated';limits=@('Dark Preview remains exposure-limited around12FPS','Torch077 physically proven on075; photo shutter flash and cold boot not verified','No optical Android parity claim','No reboot or intentional native crash performed')}
 $r|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $cache 'retained080.json') -Encoding UTF8
 $r|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $b 'research\retained080.json') -Encoding UTF8
 Copy-Item -LiteralPath (Join-Path $repo 'research\ACTIVE-TUNING-LATEST.json') -Destination (Join-Path $b 'research\previous-ACTIVE-TUNING-075.json')
 $r|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $repo 'research\ACTIVE-TUNING-LATEST.json') -Encoding UTF8
 $state|Add-Member -NotePropertyName retained -NotePropertyValue (Join-Path $cache 'retained080.json') -Force
 $state|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $cache 'active-install.json') -Encoding UTF8
 $r.status
}finally{if($lock){$lock.Dispose()}}
