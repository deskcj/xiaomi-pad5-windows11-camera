from pathlib import Path
R=Path(__file__).resolve().parents[2];B=R/'Build080';T=B/'tools'
(B/'rollback').mkdir(exist_ok=True)
src=R/'Build075/tools'
for name in ('CameraState.ps1',): (T/name).write_bytes((src/name).read_bytes())
s=(src/'Snapshot-Baseline075.ps1').read_text(encoding='utf-8-sig').replace('pre075 exact installed062','pre080 exact installed075')
(T/'Snapshot-Baseline080.ps1').write_text(s,encoding='utf-8-sig')
s=(src/'Restore-Avs062.ps1').read_text(encoding='utf-8-sig')
s=s.replace("[Reflection.Assembly]::LoadFrom('H:\\Vlad\\Codex\\QualcommISP\\Build074\\bin\\ObserverTypes074.dll')", "[Reflection.Assembly]::LoadFrom((Join-Path $PSScriptRoot 'ObserverTypes074.dll'))")
s=s.replace('CodexQispBuild075','CodexQispBuild080').replace('AVStream062','AVStream075').replace('pre075','pre080').replace('avs062','avs075').replace('AVSTREAM062_EXACT_PRE075','AVSTREAM075_EXACT_PRE080').replace('restore062.json','restore075.json')
(T/'Restore-Avs075.ps1').write_text(s,encoding='utf-8-sig')
s=(src/'Install-Focus075b.ps1').read_text(encoding='utf-8-sig')
s=s.replace(". 'H:\\Vlad\\Codex\\QualcommISP\\Build062\\tools\\CameraState.ps1'", ". (Join-Path $PSScriptRoot 'CameraState.ps1')")
s=s.replace('CodexQispBuild075','CodexQispBuild080').replace('rollback062-','rollback075-')
s=s.replace('Shared focus GET producer only; native sensors/ISP/IFE/IPE unchanged','Restore single stock IPE input buffer release call; disable unused relay producer; MFT075/data074 unchanged')
s=s.replace("'H:\\Vlad\\Codex\\QualcommISP\\Build073\\research\\postboot073-installation-scope.json'", "'C:\\ProgramData\\CodexQispBuild075\\active-install.json'")
s=s.replace("$report.boot=$os.LastBootUpTime.ToString('o')", "$report.boot=$os.LastBootUpTime.ToString('o');if($state.status -ne 'PASS_NATIVE075_AVS_ACTIVE' -or $state.pending){throw 'Validated075 state required'}")
s=s.replace('signed-candidate075b.json','signed080.json').replace('PASS_SIGNED_CANDIDATE_NOT_INSTALLED','PASS080_SIGNED_CANDIDATE_NOT_INSTALLED').replace('1.0.2324.75','1.0.2324.80').replace('candidate-focus-b','candidate')
start=s.index(" $proof=Get-Content");end=s.index(" if((Get-Service CodexNabuGpuIsp)",start)
s=s[:start]+''' $proof=Get-Content -Raw -LiteralPath (Join-Path $root 'research\\signed-pe-proof080.json')|ConvertFrom-Json
 if($proof.status -ne 'PASS080_SIGNED_PE_ONLY_ONE_STOCK_RELEASE_INSTRUCTION_PLUS_SIGNATURE' -or -not $proof.mft075Exact -or -not $proof.layoutExceptionUnwindImportsAndAllOtherCodeUnchanged){throw 'Signed one-instruction proof required'}
 $replay=Get-Content -Raw -LiteralPath (Join-Path $root 'research\\release-replay080.json')|ConvertFrom-Json
 if($replay.status -ne 'PASS080_EXACT_ORIGINAL_ARM64_IPE_FLUSH_RELEASE_REPLAY' -or @($replay.cases).Count -ne 21){throw 'Exact native release replay required'}
 $previous=Get-Content -Raw -LiteralPath 'H:\\Vlad\\Codex\\QualcommISP\\Build079\\results\\Video-default-20261024-025610\\result.json'|ConvertFrom-Json
 if($previous.pending -or $previous.status -notlike 'PASS*' -or @($previous.sessions).Count -ne 2 -or @($previous.sessions|Where-Object {-not $_.nativeCloseEvidencePassed}).Count){throw 'Both-camera native close evidence required'}
'''+s[end:]
s=s.replace('Snapshot-Baseline075.ps1','Snapshot-Baseline080.ps1').replace('Restore-Avs062.ps1','Restore-Avs075.ps1')
s=s.replace(" Copy-Item -LiteralPath 'H:\\Vlad\\Codex\\QualcommISP\\Build061\\release\\tools\\devcon.exe'", " Copy-Item -LiteralPath 'H:\\Vlad\\Codex\\QualcommISP\\Build074\\bin\\ObserverTypes074.dll' -Destination $script:qispRecovery\n Copy-Item -LiteralPath 'H:\\Vlad\\Codex\\QualcommISP\\Build061\\release\\tools\\devcon.exe'")
s=s.replace('075-disable-','080-disable-').replace('075-bind-avs','080-bind-avs').replace('075-enable-','080-enable-')
s=s.replace('PASS_NATIVE075_AVS_ACTIVE_NOT_YET_CAMERA_VALIDATED','PASS_NATIVE080_AVS_ACTIVE_NOT_YET_CAMERA_VALIDATED').replace('install-result075.json','install-result080.json').replace('$report.restored062','$report.restored075')
(T/'Install-Release080.ps1').write_text(s,encoding='utf-8-sig')
# Same native measurement client, dimensions, duration, queue and trace gates as CPU079b.
s=(R/'Build079/tools/Profile-Cpu079.ps1').read_text(encoding='utf-8-sig')
s=s.replace('CodexQispBuild075\\active-install.json','CodexQispBuild080\\active-install.json').replace('PASS_NATIVE075_AVS_ACTIVE','PASS_NATIVE080_AVS_ACTIVE')
s=s.replace('Build079\\results','Build080\\results').replace('latest-profile079.txt','latest-profile080.txt').replace('PASS_BOTH079_PROFILE_INSTALLED075','PASS_BOTH080_PROFILE_INSTALLED080')
s=s.replace('QISP focus075b validation installed075 with data074','QISP stock release080 with MFT075 data074')
(T/'Profile-Both080.ps1').write_text(s,encoding='utf-8-sig')
wrapper='''$ErrorActionPreference='Stop'
Start-Transcript -LiteralPath 'H:\\Vlad\\Codex\\QualcommISP\\Build080\\research\\install080-transcript.log'
try{& 'H:\\Vlad\\Codex\\QualcommISP\\Build080\\tools\\Install-Release080.ps1'}
catch{$_|Format-List * -Force|Out-String|Set-Content -LiteralPath 'H:\\Vlad\\Codex\\QualcommISP\\Build080\\research\\install080-error.txt';throw}
finally{Stop-Transcript}
'''
(T/'Install080-WithLog.ps1').write_text(wrapper,encoding='utf-8-sig')
print('PASS080_OPERATORS_PREPARED_REVIEW_BEFORE_EXECUTION')
