from pathlib import Path
R=Path(__file__).resolve().parents[2];B=R/'Build080'
s=(R/'Build079/tools/Analyze-KernelCadence079.py').read_text().replace("run=B/'results/Video-default-20261024-025610'", "run=Path(json.loads((B/'research/cpu080.json').read_text(encoding='utf-8-sig'))['run'])")
s=s.replace('079','080');(B/'tools/Analyze-KernelCadence080.py').write_text(s)
s=(R/'Build076/tools/Verify-Raw076.py').read_text().replace('PASS076_RETAINED_RAW_FULL_FNV_NATIVE_NV12','PASS080_RETAINED_RAW_FULL_FNV_NATIVE_NV12').replace('raw-proof076.json','raw-proof080.json')
(B/'tools/Verify-Raw080.py').write_text(s)
print('PASS080_OFFLINE_KERNEL_AND_RAW_TOOLS_PREPARED')
