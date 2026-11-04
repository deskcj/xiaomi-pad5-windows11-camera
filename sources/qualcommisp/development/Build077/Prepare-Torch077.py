from pathlib import Path
R=Path(__file__).resolve().parents[2];B=R/'Build077'
for p in ('src','bin','tests','research','results'): (B/p).mkdir(parents=True,exist_ok=True)
s=(R/'Build074/src/EvStep074.cpp').read_text(encoding='utf-8-sig')
s=s.replace('// Public WinRT exposure-compensation response observer; no AF/flash/ROI mutations.','// Public WinRT short torch pulse. One owner; restore OFF/power before disposal.')
s=s.replace('EvStep074','TorchPulse077')
s=s.replace('MediaCapture camera{nullptr};MediaFrameReader reader{nullptr};ExposureCompensationControl ev{nullptr};float oldEV=0;bool evChanged=false;', 'MediaCapture camera{nullptr};MediaFrameReader reader{nullptr};TorchControl torch{nullptr};\n    bool oldTorch=false,torchTouched=false,powerChanged=false,controlClean=true;float oldPower=0;')
start=s.index('        ev=camera.VideoDeviceController().ExposureCompensationControl();')
end=s.index('        reader=camera.CreateFrameReaderAsync',start)
s=s[:start]+'''        torch=camera.VideoDeviceController().TorchControl();
        bool supported=torch.Supported();
        printf("TORCH077_CAPS supported=%d qpc=%lld\\n",supported,Qpc());fflush(stdout);
        if(!supported)throw hresult_error(E_NOTIMPL,L"Torch unsupported by public WinRT");
        oldTorch=torch.Enabled();const bool powerSupported=torch.PowerSupported();
        printf("TORCH077_OLD enabled=%d powerSupported=%d qpc=%lld\\n",oldTorch,powerSupported,Qpc());fflush(stdout);
        if(oldTorch)throw hresult_error(HRESULT_FROM_WIN32(ERROR_BUSY),L"Pre-existing torch ON; pulse skipped");
        if(powerSupported){
            oldPower=torch.PowerPercent();
            if(!std::isfinite(oldPower) || oldPower<0 || oldPower>100)throw hresult_error(E_INVALIDARG,L"Invalid torch power");
            powerChanged=true;torch.PowerPercent(5.f);
            printf("TORCH077_POWER old=%.6f target=5 actual=%.6f\\n",oldPower,torch.PowerPercent());fflush(stdout);
        }
''' +s[end:]
start=s.index('        auto exposure=camera.VideoDeviceController().ExposureControl();')
end=s.index('\n    } catch(hresult_error const& e)',start)
s=s[:start]+'''        auto wait=[&meter](int phase,int milliseconds){
            meter.phase=phase;printf("PHASE_BEGIN phase=%d qpc=%lld\\n",phase,Qpc());fflush(stdout);
            std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
            printf("PHASE_DONE phase=%d qpc=%lld\\n",phase,Qpc());fflush(stdout);
        };
        wait(0,4000);if(!meter.acquired)throw hresult_error(E_FAIL,L"No frames before torch mutation");
        meter.phase=1;torchTouched=true;
        printf("TORCH077_ON_BEGIN qpc=%lld\\n",Qpc());fflush(stdout);
        torch.Enabled(true);
        printf("TORCH077_ON_DONE enabled=%d qpc=%lld\\n",torch.Enabled(),Qpc());fflush(stdout);
        if(!torch.Enabled())throw hresult_error(E_FAIL,L"Torch ON readback mismatch");
        wait(1,250);
        printf("TORCH077_OFF_BEGIN qpc=%lld\\n",Qpc());fflush(stdout);
        torch.Enabled(false);
        printf("TORCH077_OFF_DONE enabled=%d qpc=%lld\\n",torch.Enabled(),Qpc());fflush(stdout);
        if(torch.Enabled())throw hresult_error(E_FAIL,L"Torch OFF readback mismatch");
        wait(2,4000);
        printf("TORCH077_DIAGNOSTIC publicAPI=true onHoldMs=250 no_focus=true no_ev=true no_snapshot=true no_roi_set=true\\n");
''' +s[end:]
start=s.index('    try {\n        if(ev && sessionReady && evChanged){')
end=s.index('    try {if(reader && started)',start)
s=s[:start]+'''    try {
        if(torch && sessionReady && torchTouched){
            torch.Enabled(oldTorch);
            const bool value=torch.Enabled();
            printf("TORCH077_RESTORED enabled=%d expected=%d qpc=%lld\\n",value,oldTorch,Qpc());fflush(stdout);
            if(value!=oldTorch)controlClean=false;
        }
    }catch(hresult_error const& e){controlClean=false;printf("TORCH077_OFF_RESTORE_ERROR hr=0x%08x\\n",unsigned(e.code()));fflush(stdout);try{if(torch)torch.Enabled(false);}catch(...){}}
     catch(...){controlClean=false;try{if(torch)torch.Enabled(false);}catch(...){}}
    try {if(torch && powerChanged){torch.PowerPercent(oldPower);printf("TORCH077_POWER_RESTORED value=%.6f expected=%.6f\\n",torch.PowerPercent(),oldPower);fflush(stdout);}}
    catch(...){controlClean=false;}
    // Even a control failure must proceed through normal reader/camera close;
    // do not deliberately keep a possibly enabled lamp owner alive forever.
''' +s[end:]
s=s.replace('reader=nullptr;ev=nullptr;','reader=nullptr;torch=nullptr;')
s=s.replace('fclose(meter.out);uninit_apartment();return clean?result:7;','fclose(meter.out);uninit_apartment();if(!controlClean && result==0)result=8;return clean?result:7;')
assert 'ev.' not in s and 'evChanged' not in s
(B/'src/TorchPulse077.cpp').write_text(s)
build=(R/'Build074/tools/Build-EvStep074.ps1').read_text(encoding='utf-8-sig').replace('EvStep074','TorchPulse077')
(B/'tools/Build-TorchPulse077.ps1').write_text(build)
print('PASS077_TORCH_OBSERVER_PREPARED_NOT_EXECUTED')
