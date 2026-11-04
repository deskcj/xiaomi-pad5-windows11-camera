$ErrorActionPreference='Stop'
Start-Sleep -Seconds 5
Start-Transcript -LiteralPath 'H:\Vlad\Codex\QualcommISP\Build077\research\torch077-transcript.log'
try{& 'H:\Vlad\Codex\QualcommISP\Build077\tools\Run-Torch077.ps1'}
catch{$_|Format-List * -Force|Out-String|Set-Content -LiteralPath 'H:\Vlad\Codex\QualcommISP\Build077\research\torch077-error.txt';throw}
finally{Stop-Transcript}
