from pathlib import Path
R=Path(__file__).resolve().parents[1]
s=(R/'tools/Prepare-FocusPackage075.py').read_text()
s=s.replace('focus-get-proof075.json','focus-get-proof075b.json').replace('QcDeviceMFT8150-focus075-UNSIGNED.dll','QcDeviceMFT8150-focus075b-UNSIGNED.dll')
s=s.replace('PASS075_ORIGINAL_FOCUS_GET_STALE_FLAGS_REPRODUCED_AND_CORRECTED','PASS075B_FOCUS_GET_MODE_RANGE_FIXED_AUTO_LOCK_PRESERVED')
s=s.replace('len(corrected)==90','len(corrected)==198').replace("B/'candidate/AVStream'","B/'candidate-focus-b/AVStream'")
s=s.replace('prepared-focus075.json','prepared-focus075b.json').replace('PASS075_OFFLINE_PACKAGE_PREPARED_UNSIGNED_NOT_INSTALLED','PASS075B_OFFLINE_PACKAGE_PREPARED_UNSIGNED_NOT_INSTALLED')
s=s.replace('print(json.dumps(report,indent=2))',"print('PASS075B_UNSIGNED_PACKAGE_PREPARED')")
exec(compile(s,str(R/'tools/Prepare-FocusPackage075.py'),'exec'),{})
sign=(R/'tools/Sign-Focus075.ps1').read_text().replace('candidate\\AVStream','candidate-focus-b\\AVStream').replace('prepared-focus075.json','prepared-focus075b.json').replace('PASS075_OFFLINE_PACKAGE_PREPARED_UNSIGNED_NOT_INSTALLED','PASS075B_OFFLINE_PACKAGE_PREPARED_UNSIGNED_NOT_INSTALLED')
sign=sign.replace('signed-candidate075.json','signed-candidate075b.json').replace('075.log','075b.log').replace('verify075-','verify075b-')
(R/'tools/Sign-Focus075b.ps1').write_text(sign)
