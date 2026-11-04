$ErrorActionPreference='Stop'
Start-Transcript -LiteralPath 'H:\Vlad\Codex\QualcommISP\Build075\research\retained075-transcript.log'
try{& 'H:\Vlad\Codex\QualcommISP\Build075\tools\Retain-Focus075.ps1'}
catch{$_|Format-List * -Force|Out-String|Set-Content -LiteralPath 'H:\Vlad\Codex\QualcommISP\Build075\research\retained075-error.txt';throw}
finally{Stop-Transcript}
