from pathlib import Path
R=Path(__file__).resolve().parents[2];B=R/'Build079'
for d in ('tools','results','src','bin'):(B/d).mkdir(exist_ok=True)
s=(R/'Build078/tools/Profile-Gate078.ps1').read_text(encoding='utf-8-sig')
s=s.replace("'H:\\Vlad\\Codex\\QualcommISP\\Build078\\results\\'","'H:\\Vlad\\Codex\\QualcommISP\\Build079\\results\\'")
s=s.replace('Build078\\results\\latest-profile078.txt','Build079\\results\\latest-profile079.txt')
s=s.replace('PASS_BOTH078_PROFILE_INSTALLED075','PASS_BOTH079_PROFILE_INSTALLED075')
s=s.replace('camera=$side;captureDirectory=$capture;','camera=$side;clientPid=$process.Id;captureDirectory=$capture;')
(B/'tools/Profile-Cpu079.ps1').write_text(s)
print('PASS079_CPU_PROFILE_CAPTURE_PREPARED_NOT_EXECUTED')
