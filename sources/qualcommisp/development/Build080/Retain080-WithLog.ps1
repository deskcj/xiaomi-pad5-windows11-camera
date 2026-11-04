$ErrorActionPreference='Stop'
Start-Transcript -LiteralPath 'H:\Vlad\Codex\QualcommISP\Build080\research\retain080-transcript.log'
try{& 'H:\Vlad\Codex\QualcommISP\Build080\tools\Retain-Release080.ps1'}
catch{$_|Format-List * -Force|Out-String|Set-Content -LiteralPath 'H:\Vlad\Codex\QualcommISP\Build080\research\retain080-error.txt';throw}
finally{Stop-Transcript}
