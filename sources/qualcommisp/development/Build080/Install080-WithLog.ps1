$ErrorActionPreference='Stop'
Start-Transcript -LiteralPath 'H:\Vlad\Codex\QualcommISP\Build080\research\install080-transcript.log'
try{& 'H:\Vlad\Codex\QualcommISP\Build080\tools\Install-Release080.ps1'}
catch{$_|Format-List * -Force|Out-String|Set-Content -LiteralPath 'H:\Vlad\Codex\QualcommISP\Build080\research\install080-error.txt';throw}
finally{Stop-Transcript}
