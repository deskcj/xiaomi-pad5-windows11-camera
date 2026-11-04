from pathlib import Path
R=Path(__file__).resolve().parents[2];B=R/'Build080';O=R/'Build079'
s=(O/'tools/Run-Cpu079b.ps1').read_text(encoding='utf-8-sig')
s=s.replace('cpu079b.json','cpu080.json').replace('driver=75','driver=80').replace('cpu-b-','cpu-080-')
s=s.replace("'C:\\ProgramData\\CodexQispBuild075\\active-install.json'", "'C:\\ProgramData\\CodexQispBuild080\\active-install.json'")
s=s.replace("$state.status -ne 'PASS_NATIVE075_AVS_ACTIVE'", "$state.status -notin @('PASS_NATIVE080_AVS_ACTIVE_NOT_YET_CAMERA_VALIDATED','PASS_NATIVE080_AVS_ACTIVE')")
s=s.replace('Retained075 required','Installed080 required').replace('Profile-Cpu079.ps1','Profile-Both080.ps1').replace('latest-profile079.txt','latest-profile080.txt').replace('PASS_BOTH079_PROFILE_INSTALLED075','PASS_BOTH080_PROFILE_INSTALLED080')
s=s.replace('QispCpu079b','QispCpu080').replace('cpu079b.etl','cpu080.etl').replace("'QISP079'","'QISP080'").replace('PASS079_BOTH_NATIVE_VIDEO_CLOSED_CPU_TRACE_RECORDED','PASS080_BOTH_NATIVE_VIDEO_CLOSED_CPU_TRACE_RECORDED')
(B/'tools/Run-Cpu080.ps1').write_text(s,encoding='utf-8-sig')
s=(O/'research/QispCpu079b.wprp').read_text().replace('QispCpu079b','QispCpu080')
(B/'research/QispCpu080.wprp').write_text(s)
s=(O/'tools/Cpu079b-WithLog.ps1').read_text(encoding='utf-8-sig').replace('Start-Sleep -Seconds 5','').replace('Build079','Build080').replace('cpu079b','cpu080').replace('Run-Cpu079b','Run-Cpu080')
(B/'tools/Cpu080-WithLog.ps1').write_text(s,encoding='utf-8-sig')
print('PASS080_SAME_CPU_VIDEO_PROFILE_PREPARED')
