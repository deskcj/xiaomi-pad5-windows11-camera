from pathlib import Path
import json,re,sys,hashlib,shutil,datetime
run=Path(sys.argv[1]);B=Path(__file__).resolve().parents[1];R=B.parent
a=json.loads((run/'torch-analysis077.json').read_text())
m=json.loads((run/'rear/mft-analysis.json').read_text())
f=json.loads((run/'rear/mft-flash076.json').read_text())
on=[e for e in f['selected'] if re.search(r'Flash Fire ReqId=63 Operation=1 LEDs=2',e['text'])]
off=[e for e in f['selected'] if re.search(r'Flash Fire ReqId=66 Operation=0 LEDs=2',e['text'])]
assert len(on)==len(off)==1 and any('300 : 300' in e['text'] for e in f['selected'])
meta=[r for r in m['rows'] if r['kind']=='metadata']
before=[r for r in meta if 59<=r['request']<=62];during=[r for r in meta if 63<=r['request']<=66]
product=lambda r:r['exposure']*r['gain']*r['isp']
assert len(before)==len(during)==4
ref=min(map(product,before));assert max(map(product,during))<=ref*(1+1e-6)
assert a['peakDeltaY']>100 and abs(a['afterLate']['mean']-a['baselineLate']['mean'])<1
assert m['header']['EventsLost']=='0' and a['nativeClose']=='PASS'
peak=[r for r in a['frames'] if r['y']>a['baselineLate']['mean']+50]
proof=dict(status='PASS077_REAR_HARDWARE_TORCH_OPTICAL_RESPONSE_AND_NATIVE_PMIC_COMMANDS',run=str(run),driver=75,driverChanged=False,mode='Continuous-light torch through standard WinRT API; short pulse test only',apiOnOffRestore=True,nativePMICOn=dict(request=63,operation=1,ledCount=2,loggedCurrents=[300,300],event=on[0]),nativePMICOff=dict(request=66,operation=0,event=off[0]),baselineMeanY=a['baselineLate']['mean'],peakMeanY=a['arrivalWindowOnPlus1000ms']['max'],restoredMeanY=a['afterLate']['mean'],firstBrightArrivalMs=peak[0]['timeFromOnMs'],lastBrightArrivalMs=peak[-1]['timeFromOnMs'],nativeSensitivityNeverIncreasedDuringPulse=True,nativeBefore=before,nativeDuring=during,ETLEvents=m['events'],eventsLost=0,nativeClose='PASS',pending=False,limits=['Photo shutter flash/strobe synchronization has not been tested.','Cold-boot cause of older062 Flash initfailed remains unproven; current075 ordinary start was already successful.','PowerSupported=false; intensity adjustment was not offered by public API.'])
(run/'torch-optical-proof077.json').write_text(json.dumps(proof,indent=2)+'\n')
(B/'research/torch-optical-proof077.json').write_text(json.dumps(proof,indent=2)+'\n')
note=f'''# Подсветка задней камеры077 проверена на установленной075

Драйвер/данные не менялись. API TorchControl.Supported=true, PowerSupported=false, исходно OFF. Короткий ON около{a['onIntervalMs']:.1f}ms, OFF и восстановление исходногоOFF прошли.120 полученных WinRT кадров,0callbackerrors; nativeclosePASS/ETLloss0/pendingfalse/новыхFrameServercrashнет.

Физическая световая реакция подтверждена сочетанием трёх независимых наблюдений: publicAPI readback ON/OFF; native CamXFlash::FirePMIC ReqId63 Operation1/2LED/current300:300 и ReqId66 Operation0/OFF; яркость Y{proof['baselineMeanY']:.2f}→{proof['peakMeanY']:.2f}→{proof['restoredMeanY']:.2f}. Первая светлая рамка пришла примерно{proof['firstBrightArrivalMs']:.1f}ms после API_ON. Native экспозиция/аналоговое/ISP усиление во время этого перехода НЕ увеличились: req63 прежние70ms/17.05/5.63, затем req64..66 экспозиция и gain снижаются. Следовательно яркость выросла от света, а не от подъёма AEgain. Это функциональная проверка torch, не лабораторное измерение мощности.

Фото-вспышка по кнопке затвора и синхронизация strobe ещё НЕ проверены. Причина старого062 Flashinitfailed не доказана: в обычном075011724 и077, безdebugger, init ужеSUCCESSdevice4. Не добавлять выдуманные PMIC/power tables: существующая цепочка действительно управляетLED. В077 нет нового драйвера; текущий эталон075+074data сохранён в C075/retained075 и readonly Build075/QualcommISP-Build075-Reference-NABU.zip.

Источник: {run}. torch077.json/native-close.json/torch-optical-proof077.json, полныйWPP124sensor metadata и native PMIC events сохранены. Состояние torch восстановленоOFF, MediaCapture/reader закрыты, глобальный debugfilter возвращён. Следующий безопасный smoke — обе камеры после torch; затем измерять скорость, учитывая возможное влияние самого kernelDEBUGtrace, прежде чем менять настройки камеры. Front поAndroid не имеетLED/механическогоAF.
'''
(B/'research/OUTCOME077-RU.md').write_text(note,encoding='utf-8')
out=Path(r'C:\Users\Xiaomi\Documents\Codex\2026-10-23\new-chat\outputs');dest=out/('Night077-checkpoint-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S'));dest.mkdir()
for folder in ('src','tools','research'):
 shutil.copytree(B/folder,dest/'Build077'/folder,ignore=shutil.ignore_patterns('*.obj','*.lib','*.exp'))
for p in [run/'torch-analysis077.json',run/'torch-optical-proof077.json',run/'result.json',run/'rear/torch.log',run/'rear/frames.csv',run/'rear/native-close.json',run/'rear/mft-analysis.json']:
 target=dest/'evidence'/p.name;target.parent.mkdir(exist_ok=True);shutil.copyfile(p,target)
manifest=[dict(path=str(p.relative_to(dest)),bytes=p.stat().st_size,sha256=hashlib.sha256(p.read_bytes()).hexdigest()) for p in sorted(dest.rglob('*')) if p.is_file()]
(dest/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
for item in manifest:assert hashlib.sha256((dest/item['path']).read_bytes()).hexdigest()==item['sha256']
(out/'NIGHT077-CHECKPOINT-LATEST.txt').write_text(str(dest)+'\n')
print(json.dumps(dict(status=proof['status'],baseline=proof['baselineMeanY'],peak=proof['peakMeanY'],restored=proof['restoredMeanY'],checkpoint=str(dest)),indent=2))
