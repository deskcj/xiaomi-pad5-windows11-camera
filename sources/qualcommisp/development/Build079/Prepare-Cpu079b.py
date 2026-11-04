from pathlib import Path
R=Path(__file__).resolve().parents[1]
x=(R/'research/QispCpu079.wprp').read_text().replace('QispCpu079','QispCpu079b').replace('<Stack Value="SampledProfile"/>','<Stack Value="SampledProfile"/><Stack Value="CSwitch"/>')
(R/'research/QispCpu079b.wprp').write_text(x)
s=(R/'tools/Run-Cpu079.ps1').read_text(encoding='utf-8-sig').replace('cpu079.json','cpu079b.json').replace('results\\cpu-','results\\cpu-b-').replace('QispCpu079.wprp','QispCpu079b.wprp').replace('!QispCpu079.Light','!QispCpu079b.Light').replace("-notmatch 'QispCpu079'","-notmatch 'QispCpu079b'").replace("'cpu079.etl'","'cpu079b.etl'")
(R/'tools/Run-Cpu079b.ps1').write_text(s)
w=(R/'tools/Cpu079-WithLog.ps1').read_text(encoding='utf-8-sig').replace('cpu079-transcript','cpu079b-transcript').replace('cpu079-error','cpu079b-error').replace('Run-Cpu079.ps1','Run-Cpu079b.ps1')
(R/'tools/Cpu079b-WithLog.ps1').write_text(w)
print('PASS079B_FRESH_CPU_AND_SWITCH_STACK_PROFILE_PREPARED')
