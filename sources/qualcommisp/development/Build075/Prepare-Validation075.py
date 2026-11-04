from pathlib import Path
import shutil
R=Path(__file__).resolve().parents[1]
shutil.copyfile(R.parent/'Build062/tools/CameraState.ps1',R/'tools/CameraState.ps1')
snap=(R.parent/'Build062/tools/Snapshot-Baseline.ps1').read_text(encoding='utf-8-sig').replace("build='QISP Build061'","build='QISP pre075 exact installed062 snapshot'")
(R/'tools/Snapshot-Baseline075.ps1').write_text(snap)
(R/'rollback').mkdir(exist_ok=True)
src=R/'src/focus-audit';src.mkdir(exist_ok=True)
for f in (R/'src/capture').iterdir():
 if f.is_file():shutil.copyfile(f,src/f.name)
p=src/'NativeControls061.h';s=p.read_text(encoding='utf-8-sig')
needle='    void Open(IMFMediaSource* source, bool apply, DWORD activeStream) {'
assert s.count(needle)==1
method='''    void AuditFocusFlags075() {
        if(!focus_){printf("FOCUS075_SDK_ROUNDTRIP unsupported=true commit=false\\n");return;}
        const ULONGLONG before=focus_->GetFlags();
        const HRESULT hr=focus_->SetFlags(before);
        printf("FOCUS075_SDK_ROUNDTRIP original=0x%llx set_hr=0x%08lx readback=0x%llx normalization=false commit=false\\n",before,(unsigned long)hr,focus_->GetFlags());
        fflush(stdout);
    }
'''
p.write_text(s.replace(needle,method+needle))
p=src/'CamProbe409.cpp';s=p.read_text(encoding='utf-8-sig')
s=s.replace('static bool g_queueDepthTest075 = false;','static bool g_queueDepthTest075 = false;\nstatic bool g_focusFlagsAudit075 = false;')
needle='    if (SUCCEEDED(hr)) controls.Open(source, g_nativeAutoControls, g_captureStream);'
assert s.count(needle)==1
s=s.replace(needle,needle+'\n    if(SUCCEEDED(hr) && g_focusFlagsAudit075) controls.AuditFocusFlags075();')
needle='        } else if (_wcsicmp(argv[i], L"--focus-pulse") == 0) {'
assert s.count(needle)==1
s=s.replace(needle,'        } else if (_wcsicmp(argv[i], L"--focus-sdk-roundtrip075") == 0) {\n            g_focusFlagsAudit075=true;\n'+needle)
p.write_text(s)
build=(R/'tools/Build-Capture075.ps1').read_text().replace("src\\capture","src\\focus-audit").replace('Capture075.obj','FocusAudit075.obj').replace('QispCapture075.exe','QispFocus075.exe')
(R/'tools/Build-FocusAudit075.ps1').write_text(build)
profile=(R/'tools/Profile-Both075.ps1').read_text(encoding='utf-8-sig')
profile=profile.replace("C:\\ProgramData\\CodexQispBuild062\\active-install.json","C:\\ProgramData\\CodexQispBuild075\\active-install.json")
profile=profile.replace("$state.status -ne 'PASS_NATIVE062_AVS_ACTIVE'","$state.status -notlike 'PASS_NATIVE075_AVS_ACTIVE*'")
profile=profile.replace('QispCapture075.exe','QispFocus075.exe').replace('latest-profile075.txt','latest-focus-profile075.txt')
profile=profile.replace('PASS_BOTH063_PROFILE_INSTALLED062','PASS_BOTH075_PROFILE_INSTALLED075').replace('QISP Build074 convergence trial profiling installed062','QISP focus075b validation installed075 with data074')
profile=profile.replace("$argsList+=@('--inspect-controls')","$argsList+=@('--inspect-controls','--focus-sdk-roundtrip075')")
(R/'tools/Profile-Focus075.ps1').write_text(profile)
print('PASS075_VALIDATION_TOOLS_PREPARED_NOT_EXECUTED')
