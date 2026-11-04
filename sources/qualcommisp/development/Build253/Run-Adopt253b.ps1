$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$b='H:\Vlad\Codex\QualcommISP\Build253';$lock=$null
$r=[ordered]@{status='PREPARING253_SNAPSHOT';pid=$PID;started=(Get-Date -Format o);driverActions=$false;tuningWrites=$false;errors=@();androidParity=$false}
try{
 $seal=Get-Content -LiteralPath (Join-Path $b 'research\seal253b.json') -Raw|ConvertFrom-Json
 foreach($f in $seal.files){if((Get-FileHash -LiteralPath $f.path).Hash -ne $f.sha256){throw 'Frozen adoption source differs'}}
 & (Join-Path $b 'tools\Pre-Adopt253.ps1')
 $pre=Get-Content -LiteralPath (Join-Path $b 'research\pre-adopt-state253.json') -Raw|ConvertFrom-Json
 if($pre.status -notlike 'PASS249*' -or $pre.errors.Count){throw 'Current249 audit failed'}
 $lock=[IO.File]::Open('C:\ProgramData\CodexQispBuild062\operation.lock',[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
 if((Get-Service FrameServer).Status -ne 'Stopped'){throw 'Camera owner appeared'}
 & 'C:\Users\Xiaomi\AppData\Local\Python\pythoncore-3.15-64\python.exe' (Join-Path $b 'tools\Adopt-Reference253b.py') *> (Join-Path $b 'research\adopt253b.log')
 if($LASTEXITCODE){throw 'Adoption253 script failed; preserve partial snapshot and inspect log'}
 $lock.Dispose();$lock=$null
 & (Join-Path $b 'tools\Final-State253.ps1')
 $post=Get-Content -LiteralPath (Join-Path $b 'research\final-state253.json') -Raw|ConvertFrom-Json
 if($post.status -notlike 'PASS253*' -or $post.errors.Count){throw 'Final253 audit failed'}
 $r.status='PASS253_SNAPSHOT_BOTH_CAMERAS_CURRENT_NO_NEW_CRASH_NO_OWNER'
}catch{$r.status='FAILED253_SNAPSHOT_NO_DEVICE_OR_LIVE_TUNING_ACTION';$r.errors+=@($_.Exception.Message);$r.position=$_.InvocationInfo.PositionMessage}
finally{if($lock){$lock.Dispose()};$r.completed=(Get-Date -Format o);$r|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $b 'research\adoption-operator253b.json') -Encoding UTF8}
