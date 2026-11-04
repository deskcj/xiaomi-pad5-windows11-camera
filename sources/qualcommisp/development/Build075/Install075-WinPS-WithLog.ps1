$ErrorActionPreference='Stop'
Start-Sleep -Seconds 10
Start-Transcript -LiteralPath 'H:\Vlad\Codex\QualcommISP\Build075\research\install075-winps-transcript.log'
try{& 'H:\Vlad\Codex\QualcommISP\Build075\tools\Install-Focus075b.ps1'}
catch{$_|Format-List * -Force|Out-String|Set-Content -LiteralPath 'H:\Vlad\Codex\QualcommISP\Build075\research\install075-winps-error.txt';throw}
finally{Stop-Transcript}
