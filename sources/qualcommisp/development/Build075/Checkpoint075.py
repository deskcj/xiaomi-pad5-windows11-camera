from pathlib import Path
import json,hashlib,shutil,re,datetime
R=Path(__file__).resolve().parents[2];B=R/'Build075'
ret=json.loads((B/'research/retained075.json').read_text(encoding='utf-8-sig'))
test=json.loads((B/'research/validation075.json').read_text(encoding='utf-8-sig'))
assert ret['status'].startswith('PASS075_RETAINED') and test['status'].startswith('PASS075_') and not test['pending']
rows=[]
for run in test['runs']:
 j=json.loads((Path(run['run'])/'result.json').read_text(encoding='utf-8-sig'))
 for s in j['sessions']:
  row=dict(mode=run['mode'],pulse=run['focusPulse'],camera=s['camera'],nativeClose=s['nativeCloseEvidencePassed'])
  for key in ['unique_fps','timestamp_fps','first_frame_ms','max_gap_ms','read_failures','duplicates']:
   row[key]=float(re.search(r'\b'+key+r'=([\d.]+)',s['resultLine'])[1])
  row['run']=run['run'];rows.append(row)
(B/'research/review075.json').write_text(json.dumps(dict(status='PASS075_SIX_CAMERA_SESSIONS',rows=rows),indent=2)+'\n')
text='''# Рабочая сборка075 после ночных проверок

24 октября2026, около01:26 +05. Установлен AVStream1.0.2324.75/oem413.inf, MFT SHA256 `78f49dab2d22be71589d0f1ffc3e03cc0c59f0cc22cfac3dfa38c7579eb8e5f3`. NativeISP/MIPI/сенсоры остаются061; обе камеры используют аппаратные IFE/IPE. Перезагрузки и намеренных native-сбоев не было. Boot22:55 прежний.

Реальное улучшение075: устранено накопление несовместимых флагов GET фокуса. Было20101=AUTO|CONT|NORMAL, SDK SetFlags отвергал80070057. Стало20100=CONT|NORMAL, точный SetFlags без нормализации успешен. На задней реальный AF pulse и возвратCONT проходят. Это НЕ доказательство ускорения оптическогоAF или устранения потемнения при тапе. Источники/396 приватных ARM64 cases/byte comparison в FOCUS075-RU.md; использовать только candidate-focus-b, первый candidate075a отклонён из-за потери AUTO|LOCK.

Аппаратно выполнены Preview+Video+Preview(AF pulse), обе камеры:6 сессий PASS, nativeclose/loss0/pendingfalse, реальные cached tuning+индивидуальные EEPROM совпали, новых FrameServer APPCRASH нет. Полные результаты validation075.json, review075.json. Кадры сохранены RAW в results, свет уже выключен; дневной цвет и оптическую резкость честно оценить нельзя.

Сохраняется композиция075 + data074. 074 содержит полный Android defaultConvergence8.0.1 graph поверх retained070 (rear Android exposure). Его реальная совместимость обеих камер/Preview/Video/EV проверена отдельным074 A/B. Устойчивого выигрыша AE/FPS в темноте074 не показал. Более близкие Android параметры сохранены для утренней проверки, а не названы доказанным улучшением цвета. ACTIVE-TUNING-LATEST.json теперь указывает075; предыдущий070 record сохранён отдельно.

Новый внутренний эталон C:/ProgramData/CodexQispBuild075/retained075.json и retained075/: все7 актуальных пакетов + точные активные front/rear data. Откат AVS075→062 с сохранением074data: C:/ProgramData/CodexQispBuild075/rollback062-20261024-011451/Restore-Avs062.ps1. Откат tuning→070 — отдельный Restore-LastProven070.ps1; он может проверять старуюверсию062, поэтому сначала читать его guards и собирать свежий scope. Immutable061 ZIP и старые062/070 копии сохранены. Не использовать publicUNINSTALL061 (возвращаетGPU040).

Очередь измерителя4 не дала устойчивого выигрыша против обратного2 (front27.19/rear23.94 на2); штатно оставить2. TraceOff также не повысил FPS. Не объявлять ускорение клиентского теста изменением драйвера. SourceReader timestamps не glass-to-glass latency.

Следующая работа: вспышка остаётся неисправной. OfflineGhidra актуального qccamflash40e3648... экспортировал acquire1790/close1ac0/ioctl21d0 в Build075/research/ghidra-flash075/export. Query0x2326ab возвращает указатель из device context; требуется разбор consumer/ABI. Старая InvalidFileObject только при закрытии, не доказанная причина Flash initfailed. Не добавлять PMIC/power таблицы и не менять FlashData наугад.

Build076 — пока ТОЛЬКО исследование наблюдателя, не драйвер. Standalone CDB selftests на собственном CdbSelf076 были неуспешны из-за вложенных commandfiles; аппаратные процессы отладчиком НЕ затрагивались. Собственные cdb/selftest процессы очищены, cdb-selftest-cleanup076.json. НЕ запускать этот CDB path на FrameServer без доказанной штатной detach-процедуры. Планируется типизированный DbgEng witness с ограниченным временем, проверяемый сначала на собственном процессе. Никаких debugger/file/kernel patches в FrameServer ещё не было.

Прежние важные исправления сохраняются: ROI065 two-tag swap неверен; AEC type22=StatsCapability и не причина overshoot; 0xc00d6d72=MF_E_TRANSFORM_NEED_MORE_INPUT, не нулевой IPE; исходный no-sample контракт не маскировать. Qoder/From android read-only, пять stagedAV426 файлов не менять. Предпочитать elevated WindowsPowerShell5.1 для legacy оператора; boot сравнивать DateTimeOffset в новых PowerShell7.
'''
(B/'research/OUTCOME075-RU.md').write_text(text,encoding='utf-8')
header='''<!-- NIGHT075_ACTIVE -->
# Активна рабочая075: обе камеры IFE/IPE, GET фокуса исправлен

24 октября2026,01:26 +05: AVStream075/oem413 + tuning074, обе камеры Preview/Video/AF restore/nativeclose/cache PASS. Читать `QualcommISP/Build075/research/OUTCOME075-RU.md` (из repo: `Build075/research/OUTCOME075-RU.md`) и `research/night073-20261023/CHECKPOINT-RU.md`. Рабочая копия C:/ProgramData/CodexQispBuild075/retained075.json.075 — реальная новая MFT-сборка;074 — tuning-only;076 пока наблюдатель без аппаратного запуска. Цвет/оптическийAF/flash/Androidпаритет остаются открыты. Старые заголовки ниже — история.
<!-- END_NIGHT075_ACTIVE -->

'''
for p in [R.parent/'START-HERE.md',R/'HANDOFF.md']:
 old=p.read_text(encoding='utf-8-sig');marker='<!-- NIGHT075_ACTIVE -->'
 if marker in old:old=old.split('<!-- END_NIGHT075_ACTIVE -->',1)[1].lstrip()
 p.write_text(header+old,encoding='utf-8')
checkpoint=R/'research/night073-20261023/CHECKPOINT-RU.md'
old=checkpoint.read_text(encoding='utf-8-sig')
if '<!-- NIGHT075_CHECKPOINT -->' not in old:
 checkpoint.write_text('<!-- NIGHT075_CHECKPOINT -->\n# Актуально075: установлена и сохранена, обе камеры проверены\n\nЧитать `Build075/research/OUTCOME075-RU.md`. AVStream075/oem413 +074data; GET focus20100 принят SDK без нормализации; rearAF/restore PASS.6 hardware sessions/nativeclose/cache PASS, новый retained075 на C:. Вспышка/дневноекачество ещё открыты.076 только offline observer selftests, на FrameServer debugger ещё НЕ запускался. Предыдущий текст ниже — история, не текущий active-state.\n\n'+old,encoding='utf-8')
outroot=Path(r'C:\Users\Xiaomi\Documents\Codex\2026-10-23\new-chat\outputs')
dest=outroot/('Night075-checkpoint-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
dest.mkdir()
for sub in ['tools','src','bin']:
 shutil.copytree(B/sub,dest/'Build075'/sub,ignore=shutil.ignore_patterns('*.obj','*.exp','*.lib'))
notes=dest/'notes';notes.mkdir()
for p in [B/'research/OUTCOME075-RU.md',B/'research/FOCUS075-RU.md',B/'research/retained075.json',B/'research/validation075.json',B/'research/install-result075.json',B/'research/signed-candidate075b.json',B/'research/signed-pe-proof075b.json',B/'research/review075.json',B/'research/depth4075-review.json',B/'research/focus/focus-get-proof075b.json',checkpoint,R/'research/ACTIVE-TUNING-LATEST.json']:
 shutil.copyfile(p,notes/p.name)
for folder in ['ghidra-focus075','ghidra-focus-set075','ghidra-flash075']:
 src=B/'research'/folder/'export'
 if src.exists():shutil.copytree(src,notes/folder)
shutil.copytree(R/'Build076/src',dest/'Build076/src');shutil.copytree(R/'Build076/tools',dest/'Build076/tools')
manifest=[]
for p in sorted(dest.rglob('*')):
 if p.is_file():manifest.append(dict(path=str(p.relative_to(dest)),bytes=p.stat().st_size,sha256=hashlib.sha256(p.read_bytes()).hexdigest()))
(dest/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
for f in manifest:assert hashlib.sha256((dest/f['path']).read_bytes()).hexdigest()==f['sha256']
(outroot/'NIGHT075-CHECKPOINT-LATEST.txt').write_text(str(dest)+'\n')
print(json.dumps(dict(status='PASS075_DURABLE_NOTES_AND_VERIFIED_INTERNAL_SOURCE_CHECKPOINT',path=str(dest),files=len(manifest),bytes=sum(x['bytes'] for x in manifest),retained=ret['retained']),indent=2))
