from pathlib import Path
R=Path(__file__).resolve().parents[2];B=R/'Build081'
for n in ('src','tools','bin','tests','research','results'):(B/n).mkdir(parents=True,exist_ok=True)
s=(R/'Build077/src/TorchPulse077.cpp').read_text(encoding='utf-8-sig').replace('TorchPulse077','FlashAudit081')
s=s.replace('MediaCapture camera{nullptr};MediaFrameReader reader{nullptr};TorchControl torch{nullptr};\n    bool oldTorch=false,torchTouched=false,powerChanged=false,controlClean=true;float oldPower=0;', 'MediaCapture camera{nullptr};MediaFrameReader reader{nullptr};bool controlClean=true;')
start=s.index('        torch=camera.VideoDeviceController().TorchControl();');end=s.index('        reader=camera.CreateFrameReaderAsync',start)
s=s[:start]+'''        auto controller=camera.VideoDeviceController();
        auto flash=controller.FlashControl();auto torch=controller.TorchControl();
        const bool fs=flash.Supported(),ts=torch.Supported();
        printf("FLASH081_CAPS flashSupported=%d torchSupported=%d videoInitialization=true readonly=true\\n",fs,ts);fflush(stdout);
        if(fs){
            printf("FLASH081_STATE enabled=%d auto=%d redEyeSupported=%d redEye=%d powerSupported=%d assistantSupported=%d assistantEnabled=%d\\n",
              flash.Enabled(),flash.Auto(),flash.RedEyeReductionSupported(),flash.RedEyeReduction(),flash.PowerSupported(),flash.AssistantLightSupported(),flash.AssistantLightEnabled());fflush(stdout);
            if(flash.PowerSupported())printf("FLASH081_POWER value=%.6f readonly=true\\n",flash.PowerPercent());
        }
        if(ts){printf("TORCH081_STATE enabled=%d powerSupported=%d readonly=true\\n",torch.Enabled(),torch.PowerSupported());fflush(stdout);}
''' +s[end:]
start=s.index('        auto wait=[&meter]');end=s.index('\n    } catch(hresult_error const& e)',start)
s=s[:start]+'''        std::this_thread::sleep_for(std::chrono::milliseconds(2500));
        if(!meter.acquired)throw hresult_error(E_FAIL,L"No preview frames during read-only control query");
        printf("FLASH081_READONLY_DONE no_torch_set=true no_flash_set=true no_photo=true no_focus=true no_ev=true\\n");fflush(stdout);
''' +s[end:]
start=s.index('    // Cleanup runs');end=s.index('    // Even a control failure',start)
s=s[:start]+'    // Read-only controls: no setters or restoration required.\n'+s[end:]
s=s.replace('reader=nullptr;torch=nullptr;','reader=nullptr;')
s=s.replace('// Public WinRT short torch pulse. One owner; restore OFF/power before disposal.','// Public WinRT read-only Flash/Torch capabilities. No control setters or photo capture.')
assert 'torch.Enabled(true)' not in s and 'torch.Enabled(false)' not in s and 'PowerPercent(old' not in s and 'torchTouched' not in s
(B/'src/FlashAudit081.cpp').write_text(s,encoding='utf-8-sig')
s=(R/'Build077/tools/Build-TorchPulse077.ps1').read_text(encoding='utf-8-sig').replace('TorchPulse077','FlashAudit081')
(B/'tools/Build-FlashAudit081.ps1').write_text(s,encoding='utf-8-sig')
s=(R/'Build077/tools/Profile-Torch077.ps1').read_text(encoding='utf-8-sig').replace('Build077','Build081').replace('torch-observer-manifest077.json','flash-observer-manifest081.json').replace('torch-'+"'",'flash-audit-'+"'")
s=s.replace("foreach($side in 'rear')", "foreach($side in 'front','rear')").replace('shortTorchRequested=$true','shortTorchRequested=$false')
s=s.replace('src\\TorchPulse077.exe','src\\FlashAudit081.exe').replace('torch.log','flash-audit.log').replace('torch.err','flash-audit.err')
s=s.replace('CodexQispBuild075\\active','CodexQispBuild080\\active')
s=s.replace("$log -notmatch 'TORCH077_RESTORED enabled=0 expected=0' -or $log -notmatch 'TORCH077_OFF_DONE enabled=0'", "$log -notmatch 'FLASH081_READONLY_DONE no_torch_set=true no_flash_set=true no_photo=true'")
s=s.replace('PASS077_TORCH_API_PULSE_OFF_RESTORED_NATIVE_CLOSE','PASS081_READONLY_FLASH_CAPS_NATIVE_CLOSE').replace('PASS077_REAR_TORCH_API_PULSE_OFF_RESTORED_NATIVE_CLOSE','PASS081_BOTH_READONLY_FLASH_CAPS_NATIVE_CLOSE').replace('latest-torch077.txt','latest-flash081.txt').replace("'TorchPulse*'","'TorchPulse*','FlashAudit*'")
(B/'tools/Profile-Flash081.ps1').write_text(s,encoding='utf-8-sig')
s=(R/'Build077/tools/Run-Torch077.ps1').read_text(encoding='utf-8-sig')
s=s.replace('torch077.json','flash081.json').replace('driver=75','driver=80').replace('CodexQispBuild075','CodexQispBuild080').replace('PASS_NATIVE075_AVS_ACTIVE','PASS_NATIVE080_AVS_ACTIVE').replace('Build075\\tools','Build080\\tools').replace('retained075.json','retained080.json')
s=s.replace('Profile-Torch077.ps1','Profile-Flash081.ps1').replace('latest-torch077.txt','latest-flash081.txt').replace("'PASS077*'","'PASS081*'").replace('PASS077_PUBLIC_TORCH_API_PULSE_OFF_RESTORE_NATIVE_CLOSE_OPTICAL_REVIEW_REQUIRED','PASS081_BOTH_READONLY_FLASH_CAPABILITIES_NATIVE_CLOSE')
s=s.replace('Short rear public WinRT TorchControl pulse; ON hold250ms; OFF/restore; native close','Both cameras read-only public WinRT Flash/Torch capabilities; no setters, no photo or emitted light; native close')
s=s.replace("'TorchPulse*'","'TorchPulse*','FlashAudit*'")
(B/'tools/Run-Flash081.ps1').write_text(s,encoding='utf-8-sig')
(B/'tools/Flash081-WithLog.ps1').write_text("""$ErrorActionPreference='Stop'
Start-Transcript -LiteralPath 'H:\\Vlad\\Codex\\QualcommISP\\Build081\\research\\flash081-transcript.log'
try{& 'H:\\Vlad\\Codex\\QualcommISP\\Build081\\tools\\Run-Flash081.ps1'}
catch{$_|Format-List * -Force|Out-String|Set-Content -LiteralPath 'H:\\Vlad\\Codex\\QualcommISP\\Build081\\research\\flash081-error.txt';throw}
finally{Stop-Transcript}
""",encoding='utf-8-sig')
print('PASS081_READONLY_OBSERVER_SOURCE_PREPARED_NO_DRIVER_CHANGE')
