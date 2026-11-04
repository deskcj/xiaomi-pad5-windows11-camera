$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$build=Split-Path $PSScriptRoot;$repo=Split-Path $build
$cache='C:\ProgramData\CodexQispBuild075';$dest=Join-Path $cache 'retained075'
$lock=$null
try{
 $lock=[IO.File]::Open('C:\ProgramData\CodexQispBuild062\operation.lock',[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
 if(Test-Path -LiteralPath $dest){throw 'Preserve existing retained075'}
 if(@(Get-Process -Name 'WindowsCamera','QispCapture*','QispFocus*','CamProbe*','devcon','pnputil' -ErrorAction SilentlyContinue).Count){throw 'Camera/device owner active'}
 $validation=Get-Content -Raw -LiteralPath (Join-Path $build 'research\validation075.json')|ConvertFrom-Json
 if($validation.status -ne 'PASS075_BOTH_PREVIEW_VIDEO_SDK_FOCUS_ROUNDTRIP_REAR_AF_RESTORE_NATIVE_CLOSE' -or $validation.pending){throw 'Completed tests required'}
 $state=Get-Content -Raw -LiteralPath (Join-Path $cache 'active-install.json')|ConvertFrom-Json
 [Reflection.Assembly]::LoadFrom('H:\Vlad\Codex\QualcommISP\Build074\bin\ObserverTypes074.dll')|Out-Null
 & (Join-Path $PSScriptRoot 'Snapshot-Baseline075.ps1') -OutputDirectory (Join-Path $dest 'packages')
 . (Join-Path $PSScriptRoot 'CameraState.ps1')
 $snapshot=Get-Content -Raw -LiteralPath (Join-Path $dest 'packages\baseline.json')|ConvertFrom-Json
 foreach($p in $snapshot.packages){QispCheckSavedPackage $p (Join-Path $dest 'packages') -Current}
 $avs=@($snapshot.packages|Where-Object label -eq 'avs')[0]
 if($avs.version -ne '1.0.2324.75' -or $avs.publishedInf -ne 'oem413.inf'){throw 'Actual075 state differs'}
 $files=@()
 foreach($side in 'FrontData','RearData'){
  $dir='C:\ProgramData\CodexQispBuild061\active\package\Unified\'+$side;$out=Join-Path $dest $side
  New-Item -ItemType Directory -Path $out|Out-Null
  foreach($f in Get-ChildItem -LiteralPath $dir -File){
   $sha=(Get-FileHash -LiteralPath $f.FullName).Hash;Copy-Item -LiteralPath $f.FullName -Destination $out
   if((Get-FileHash -LiteralPath (Join-Path $out $f.Name)).Hash -ne $sha){throw 'Retained data differs'}
   $files+=@([ordered]@{side=$side;name=$f.Name;live=$f.FullName;backup=(Join-Path $out $f.Name);sha256=$sha;bytes=$f.Length})
  }
 }
 $r=[ordered]@{status='PASS075_RETAINED_FOCUS_GET_FIX_BOTH_CAMERAS_DATA074';retainedAt=(Get-Date -Format o);installedDriver=75;boot=$state.boot;nodes=$state.nodes;files=$files;retained=$dest;packages=(Join-Path $dest 'packages\baseline.json');validation=(Join-Path $build 'research\validation075.json');signedMFTSHA256='78f49dab2d22be71589d0f1ffc3e03cc0c59f0cc22cfac3dfa38c7579eb8e5f3';rollback062=(Join-Path $state.recovery 'Restore-Avs062.ps1');rollback070='C:\ProgramData\CodexQispBuild070\Restore-LastProven070.ps1';androidParity=$false;improvement='Invalid AUTO|CONT focus GET flags20101 replaced with valid20100; SDK exact flags now accepted, real AF pulse/restore passed';tuning='Full Android Convergence graph074 on retained070 exposure; compatible in both preview/video and EV tests; optical/FPS gain not proven';limits=@('Optical focus speed and daylight color not yet evaluated','Flash still unresolved','Low-light measured FPS includes exposure and host CPU/memory constraints; no new FPS gain claimed','No reboot or intentional native crash performed')}
 $r|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $cache 'retained075.json') -Encoding UTF8
 $r|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $build 'research\retained075.json') -Encoding UTF8
 Copy-Item -LiteralPath (Join-Path $repo 'research\ACTIVE-TUNING-LATEST.json') -Destination (Join-Path $build 'research\previous-ACTIVE-TUNING-070.json')
 $r|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $repo 'research\ACTIVE-TUNING-LATEST.json') -Encoding UTF8
 $state.status='PASS_NATIVE075_AVS_ACTIVE';$state|Add-Member -NotePropertyName validation -NotePropertyValue $r.validation -Force
 $state|Add-Member -NotePropertyName retained -NotePropertyValue (Join-Path $cache 'retained075.json') -Force
 $state|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $cache 'active-install.json') -Encoding UTF8
 $r.status
}finally{if($lock){$lock.Dispose()}}
