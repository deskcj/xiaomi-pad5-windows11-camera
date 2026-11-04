$ErrorActionPreference='Stop'
Start-Sleep -Seconds 10
Start-Transcript -LiteralPath 'H:\Vlad\Codex\QualcommISP\Build075\research\validation075-transcript.log'
try{& 'H:\Vlad\Codex\QualcommISP\Build075\tools\Validate-Focus075.ps1'}
catch{$_|Format-List * -Force|Out-String|Set-Content -LiteralPath 'H:\Vlad\Codex\QualcommISP\Build075\research\validation075-error.txt';throw}
finally{Stop-Transcript}
