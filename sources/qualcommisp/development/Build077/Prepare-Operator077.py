from pathlib import Path
import json,hashlib
R=Path(__file__).resolve().parents[2];B=R/'Build077'
s=(R/'Build074/tools/Profile-Ev074c.ps1').read_text(encoding='utf-8-sig')
s=s.replace("[ValidateSet('original','candidate','restore')][string]$Phase='original'","[ValidateSet('preview')][string]$Phase='preview'")
s=s.replace("$build=Join-Path $repo 'Build074'","$build=Join-Path $repo 'Build077'")
s=s.replace('ev-observer-manifest074c.json','torch-observer-manifest077.json').replace('results\\ev-','results\\torch-')
s=s.replace("AFTriggered=$false;flashTriggered=$false","AFTriggered=$false;shortTorchRequested=$true;photoFlashTriggered=$false")
s=s.replace("'QispCapture*','EvStep*'","'QispCapture*','QispFocus*','TorchPulse*','DbgEngWitness*','EvStep*'")
s=s.replace("(Get-CimInstance Win32_OperatingSystem).LastBootUpTime.ToString('o') -ne $manifest.boot","[datetimeoffset](Get-CimInstance Win32_OperatingSystem).LastBootUpTime -ne [datetimeoffset]$manifest.boot")
s=s.replace("$filterExe=Join-Path $build 'bin\\TraceFilter074.exe'","$filterExe=Join-Path $repo 'Build074\\bin\\TraceFilter074.exe'")
s=s.replace("foreach($side in 'front','rear')","foreach($side in 'rear')")
s=s.replace('src\\EvStep074.exe','src\\TorchPulse077.exe').replace('ev.log','torch.log').replace('ev.err','torch.err')
s=s.replace('C:\\ProgramData\\CodexQispBuild062\\active\\tools\\Check-NativeClose.ps1','C:\\ProgramData\\CodexQispBuild075\\active\\tools\\Check-NativeClose.ps1')
s=s.replace("$log -notmatch 'EV_RESTORED value=' -or $log -notmatch 'ACTION_DONE ev_restore_final'","$log -notmatch 'TORCH077_RESTORED enabled=0 expected=0' -or $log -notmatch 'TORCH077_OFF_DONE enabled=0'")
s=s.replace('EV test failed but native closure completed','Torch interface test failed but native closure completed')
s=s.replace('PASS074_EV_STEPS_RESTORED_NATIVE_CLOSE','PASS077_TORCH_API_PULSE_OFF_RESTORED_NATIVE_CLOSE').replace('PASS074_BOTH_EV_RESPONSE_NATIVE_CLOSE','PASS077_REAR_TORCH_API_PULSE_OFF_RESTORED_NATIVE_CLOSE')
s=s.replace('latest-ev074.txt','latest-torch077.txt')
(B/'tools/Profile-Torch077.ps1').write_text(s)
state=json.loads(Path(r'C:\ProgramData\CodexQispBuild075\active-install.json').read_text(encoding='utf-8-sig'))
assets=[B/'src/TorchPulse077.cpp',B/'src/TorchPulse077.exe',B/'tools/Profile-Torch077.ps1',R/'Build074/bin/TraceFilter074.exe']
manifest=dict(status='SEALED077_SHORT_TORCH_OBSERVER_NOT_EXECUTED',boot=state['boot'],driver=75,onHoldMs=250,targetPowerPercentIfSupported=5,files=[dict(path=str(p),bytes=p.stat().st_size,sha256=hashlib.sha256(p.read_bytes()).hexdigest()) for p in assets])
(B/'research/torch-observer-manifest077.json').write_text(json.dumps(manifest,indent=2)+'\n')
print('PASS077_OPERATOR_AND_ASSETS_SEALED_NOT_EXECUTED')
