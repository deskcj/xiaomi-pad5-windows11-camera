from pathlib import Path
R=Path(__file__).resolve().parents[2];B=R/'Build243';(B/'research').mkdir(parents=True,exist_ok=True)
s=(R/'Build239/tools/Archive-HistoricalCopies239.ps1').read_text(encoding='utf-8-sig')
s=s.replace("$pairs=@(@('Night080-checkpoint-20261024-035123','NIGHT080-CHECKPOINT-LATEST.txt'),@('Night085-checkpoint-20261024-044610','NIGHT085-CHECKPOINT-LATEST.txt'))","$pairs=,@('Night079-checkpoint-20261024-032243','NIGHT079-CHECKPOINT-LATEST.txt')")
s=s.replace('(?:80|85)','(?:79)').replace('C-historical-notes239','C-historical-notes243').replace('Build239','Build243').replace('archive239.json','archive243.json').replace('PASS239','PASS243')
(B/'tools/Archive-OldCopy243.ps1').write_text(s,encoding='utf-8-sig')
s=(R/'Build242/tools/Final-State242b.ps1').read_text(encoding='utf-8-sig').replace('Build242','Build243').replace('final-state242b.json','pre-adopt-state243.json')
(B/'tools/Pre-Adopt243.ps1').write_text(s,encoding='utf-8-sig')
print('PREPARED243_READONLY_ADOPTION_PREFLIGHT_AND_OWN_DUPLICATE_ARCHIVE')
