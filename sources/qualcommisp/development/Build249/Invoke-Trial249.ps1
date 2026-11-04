$ErrorActionPreference='Stop'
try{& 'H:\Vlad\Codex\QualcommISP\Build249\tools\Stage-Apply-Capture249.ps1'}catch{
 [ordered]@{status='FAILED249_STAGE_OR_LAUNCH_INSPECT_JOURNALS';pid=$PID;time=(Get-Date -Format o);error=$_.Exception.Message;position=$_.InvocationInfo.PositionMessage;automaticRecovery=$false}|ConvertTo-Json -Depth 6|Set-Content -LiteralPath 'H:\Vlad\Codex\QualcommISP\Build249\research\launch-failure249.json' -Encoding UTF8
}
