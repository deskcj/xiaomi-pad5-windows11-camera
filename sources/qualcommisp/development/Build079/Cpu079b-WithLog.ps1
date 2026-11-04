$ErrorActionPreference='Stop'
Start-Sleep -Seconds 5
Start-Transcript -LiteralPath 'H:\Vlad\Codex\QualcommISP\Build079\research\cpu079b-transcript.log'
try{& 'H:\Vlad\Codex\QualcommISP\Build079\tools\Run-Cpu079b.ps1'}
catch{$_|Format-List * -Force|Out-String|Set-Content -LiteralPath 'H:\Vlad\Codex\QualcommISP\Build079\research\cpu079b-error.txt';throw}
finally{Stop-Transcript}
