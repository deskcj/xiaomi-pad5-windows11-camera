$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$b='H:\Vlad\Codex\QualcommISP\Build249';$repo=Split-Path -Parent $b;$cache='C:\ProgramData\CodexQispBuild249'
$out=Join-Path $b 'research\deploy249.json'
if((Test-Path -LiteralPath $cache) -or (Test-Path -LiteralPath $out)){throw 'Single-use249; preserve existing stage'}
$previous=Get-Content -LiteralPath (Join-Path $repo 'Build247\research\trial247b.json') -Raw|ConvertFrom-Json
if($previous.status -notlike 'PASS247B*' -or $previous.pending -or -not $previous.ownerCloseVerified -or -not $previous.fullMftDecoded){throw 'Both baseline cameras must be closed/decode complete'}
if(Get-Process -Id $previous.pid -ErrorAction SilentlyContinue){throw 'Baseline writer still active'}
& (Join-Path $b 'tools\Preflight-Stage249b.ps1')
$pre=Get-Content -LiteralPath (Join-Path $b 'research\preflight-stage249b.json') -Raw|ConvertFrom-Json
if($pre.status -notlike 'PASS243*' -or -not $pre.captureMemoryGateCurrentlyPass){throw 'Current243 preflight failed'}
$seal=Get-Content -LiteralPath (Join-Path $b 'research\seal249b.json') -Raw|ConvertFrom-Json
foreach($f in $seal.files){if((Get-FileHash -LiteralPath $f.path).Hash -ne $f.sha256){throw ('Frozen asset differs: '+$f.path)}}
foreach($file in @('native-proof248.json','container-proof248.json')){
 $proof=Get-Content -LiteralPath (Join-Path $repo ('Build248\research\metering\'+$file)) -Raw|ConvertFrom-Json
 if($proof.status -notlike 'PASS248*'){throw 'Offline native proof failed'}
}
$manifest=Get-Content -LiteralPath (Join-Path $repo 'Build248\research\metering\candidate248.json') -Raw|ConvertFrom-Json
if($manifest.sessions.Count -ne 1 -or $manifest.sessions[0].camera -ne 'rear'){throw 'Rear-only complete graph required'}
$item=$manifest.sessions[0]
$refPath='C:\ProgramData\CodexQispBuild243\retained243.json';$ref=Get-Content -LiteralPath $refPath -Raw|ConvertFrom-Json
if((Get-FileHash -LiteralPath $refPath).Hash -ne '189A8A210932E0CE060CD7F04169C75807CF898B57F8EC7B918203619F0FFA13'){throw 'Accepted243 identity differs'}
$rear=@($ref.files|Where-Object {$_.side -eq 'RearData' -and $_.name -like 'com.qti.tuned.*'});if($rear.Count -ne 1 -or $item.sourceSHA -ne $rear[0].sha256){throw 'Candidate baseline not current243'}
$toCopy=@()
foreach($name in @('Apply-Data249.ps1','CameraState196.ps1','Query-QispTraces207.ps1')){$p=Join-Path $b ('tools\'+$name);$toCopy+=@([ordered]@{name=$name;source=$p;sha256=(Get-FileHash -LiteralPath $p).Hash;bytes=(Get-Item -LiteralPath $p).Length})}
foreach($v in @(@('original-rear243.bin',$rear[0].backup,$rear[0].sha256),@('candidate-rear249.bin',$item.candidate,$item.candidateSHA))){
 if((Get-FileHash -LiteralPath $v[1]).Hash -ne $v[2]){throw 'Data source SHA differs'}
 $toCopy+=@([ordered]@{name=$v[0];source=$v[1];sha256=$v[2];bytes=(Get-Item -LiteralPath $v[1]).Length})
}
$total=0L;foreach($f in $toCopy){$total+=[long]$f.bytes}
if([IO.DriveInfo]::new('C:\').AvailableFreeSpace-$total -lt 1GB){throw 'Preserve1GiB after stage'}
[IO.Directory]::CreateDirectory($cache)|Out-Null
foreach($f in $toCopy){Copy-Item -LiteralPath $f.source -Destination (Join-Path $cache $f.name);if((Get-FileHash -LiteralPath (Join-Path $cache $f.name)).Hash -ne $f.sha256){throw 'C stage SHA readback failed'}}
$assets=[ordered]@{reference243SHA=(Get-FileHash -LiteralPath $refPath).Hash;files=$toCopy;data=@([ordered]@{live=$rear[0].live;original=(Join-Path $cache 'original-rear243.bin');originalSHA=$rear[0].sha256;candidateName='candidate-rear249.bin';candidateSHA=$item.candidateSHA})}
$assets|ConvertTo-Json -Depth 12|Set-Content -LiteralPath (Join-Path $cache 'assets249.json') -Encoding UTF8
foreach($f in $ref.files){if($f.live -eq $rear[0].live){$f.sha256=$item.candidateSHA;$f.backup=Join-Path $cache 'candidate-rear249.bin';$f.bytes=$item.bytes}}
$ref.dataReference=249;$ref.status='TEST249_REAR_ANDROID_METERING_ON243_NOT_ACCEPTED';$ref.accepted=$false;$ref.pending=$false;$ref.androidParity=$false
$ref|ConvertTo-Json -Depth 30|Set-Content -LiteralPath (Join-Path $cache 'test-reference249.json') -Encoding UTF8
[ordered]@{status='STAGED249_ORIGINAL243';phase='original';refreshPending=$false;copyStarted=$false;pendingPid=0;capturePending=$false;ownerCloseVerified=$true;fullMftDecoded=$true;captureJournal='';captureStatus='NOT_STARTED';errors=@();reference243SHA=$assets.reference243SHA;staged=(Get-Date -Format o)}|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $cache 'state249.json') -Encoding UTF8
& (Join-Path $cache 'Apply-Data249.ps1')
$state=Get-Content -LiteralPath (Join-Path $cache 'state249.json') -Raw|ConvertFrom-Json
$state|ConvertTo-Json -Depth 12|Set-Content -LiteralPath $out -Encoding UTF8
if($state.status -ne 'PASS249_CAMERA_DATA_PHASE_CANDIDATE_NO_REBOOT'){throw 'Candidate249 not completely applied'}
& (Join-Path $b 'tools\Run-Roi249.ps1')
