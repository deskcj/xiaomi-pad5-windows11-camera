$ErrorActionPreference='Stop'
Start-Sleep -Seconds 5
Start-Transcript -LiteralPath 'H:\Vlad\Codex\QualcommISP\Build079\research\cpu079-transcript.log'
try{& 'H:\Vlad\Codex\QualcommISP\Build079\tools\Run-Cpu079.ps1'}
catch{$_|Format-List * -Force|Out-String|Set-Content -LiteralPath 'H:\Vlad\Codex\QualcommISP\Build079\research\cpu079-error.txt';throw}
finally{Stop-Transcript}
